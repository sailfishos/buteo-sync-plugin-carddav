/*
 * This file is part of buteo-sync-plugin-carddav package
 *
 * Copyright (C) 2014 Jolla Ltd. and/or its subsidiary(-ies).
 *
 * Contributors: Chris Adams <chris.adams@jolla.com>
 *
 * This program/library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This program/library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program/library; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include "replyparser_p.h"
#include "syncer_p.h"
#include "carddav_p.h"

#include <LogMacros.h>

#include <QString>
#include <QList>
#include <QXmlStreamReader>
#include <QByteArray>
#include <QRegularExpression>

#include <QContactGuid>
#include <QContactSyncTarget>
#include <QContactExtendedDetail>

namespace {
    void debugDumpData(const QString &data)
    {
        if (Buteo::Logger::instance()->getLogLevel() < 7) {
            return;
        }

        QString dbgout;
        Q_FOREACH (const QChar &c, data) {
            if (c == '\r' || c == '\n') {
                if (!dbgout.isEmpty()) {
                    LOG_DEBUG(dbgout);
                    dbgout.clear();
                }
            } else {
                dbgout += c;
            }
        }
        if (!dbgout.isEmpty()) {
            LOG_DEBUG(dbgout);
        }
    }

    QVariantMap elementToVMap(QXmlStreamReader &reader)
    {
        QVariantMap element;

        // store the attributes of the element
        QXmlStreamAttributes attrs = reader.attributes();
        while (attrs.size()) {
            QXmlStreamAttribute attr = attrs.takeFirst();
            element.insert(attr.name().toString(), attr.value().toString());
        }

        while (reader.readNext() != QXmlStreamReader::EndElement) {
            if (reader.isCharacters()) {
                // store the text of the element, if any
                QString elementText = reader.text().toString();
                if (!elementText.isEmpty()) {
                    element.insert(QLatin1String("@text"), elementText);
                }
            } else if (reader.isStartElement()) {
                // recurse if necessary.
                QString subElementName = reader.name().toString();
                QVariantMap subElement = elementToVMap(reader);
                if (element.contains(subElementName)) {
                    // already have an element with this name.
                    // create a variantlist and append.
                    QVariant existing = element.value(subElementName);
                    QVariantList subElementList;
                    if (existing.type() == QVariant::Map) {
                        // we need to convert the value into a QVariantList
                        subElementList << existing.toMap();
                    } else if (existing.type() == QVariant::List) {
                        subElementList = existing.toList();
                    }
                    subElementList << subElement;
                    element.insert(subElementName, subElementList);
                } else {
                    // first element with this name.  insert as a map.
                    element.insert(subElementName, subElement);
                }
            }
        }

        return element;
    }

    QVariantMap xmlToVMap(QXmlStreamReader &reader)
    {
        QVariantMap retn;
        while (!reader.atEnd() && !reader.hasError() && reader.readNextStartElement()) {
            QString elementName = reader.name().toString();
            QVariantMap element = elementToVMap(reader);
            retn.insert(elementName, element);
        }
        return retn;
    }
}

ReplyParser::ReplyParser(Syncer *parent, CardDavVCardConverter *converter)
    : q(parent), m_converter(converter)
{
}

ReplyParser::~ReplyParser()
{
}

QString ReplyParser::parseUserPrincipal(const QByteArray &userInformationResponse, ReplyParser::ResponseType *responseType) const
{
    /* We expect a response of the form:
        HTTP/1.1 207 Multi-status
        Content-Type: application/xml; charset=utf-8

        <d:multistatus xmlns:d="DAV:">
            <d:response>
                <d:href>/</d:href>
                <d:propstat>
                    <d:prop>
                        <d:current-user-principal>
                            <d:href>/principals/users/johndoe/</d:href>
                        </d:current-user-principal>
                    </d:prop>
                    <d:status>HTTP/1.1 200 OK</d:status>
                </d:propstat>
            </d:response>
        </d:multistatus>

      Note however that some CardDAV servers return addressbook
      information instead of user principal information.
    */
    debugDumpData(QString::fromUtf8(userInformationResponse));
    QXmlStreamReader reader(userInformationResponse);
    QVariantMap vmap = xmlToVMap(reader);
    QVariantMap multistatusMap = vmap[QLatin1String("multistatus")].toMap();
    if (multistatusMap[QLatin1String("response")].type() == QVariant::List) {
        // This should not be the case for a UserPrincipal response.
        *responseType = ReplyParser::AddressbookInformationResponse;
        return QString();
    }

    // Only one response - this could be either a UserPrincipal response
    // or an AddressbookInformation response.
    QVariantMap response = multistatusMap[QLatin1String("response")].toMap();
    QString statusText = response.value("propstat").toMap().value("status").toMap().value("@text").toString();
    QString userPrincipal = response.value("propstat").toMap().value("prop").toMap()
            .value("current-user-principal").toMap().value("href").toMap().value("@text").toString();
    QString ctag = response.value("propstat").toMap().value("prop").toMap().value("getctag").toMap().value("@text").toString();

    if (!statusText.contains(QLatin1String("200 OK"))) {
        LOG_WARNING(Q_FUNC_INFO << "invalid status response to current user information request:" << statusText);
    } else if (userPrincipal.isEmpty() && !ctag.isEmpty()) {
        // this server has responded with an addressbook information response.
        LOG_DEBUG(Q_FUNC_INFO << "addressbook information response to current user information request:" << statusText);
        *responseType = ReplyParser::AddressbookInformationResponse;
        return QString();
    }

    *responseType = ReplyParser::UserPrincipalResponse;
    return userPrincipal;
}

QString ReplyParser::parseAddressbookHome(const QByteArray &addressbookUrlsResponse) const
{
    /* We expect a response of the form:
        HTTP/1.1 207 Multi-status
        Content-Type: application/xml; charset=utf-8

        <d:multistatus xmlns:d="DAV:" xmlns:card="urn:ietf:params:xml:ns:carddav">
            <d:response>
                <d:href>/</d:href>
                <d:propstat>
                    <d:prop>
                        <card:addressbook-home-set>
                            <d:href>/addressbooks/johndoe/</d:href>
                        </card:addressbook-home-set>
                    </d:prop>
                    <d:status>HTTP/1.1 200 OK</d:status>
                </d:propstat>
            </d:response>
        </d:multistatus>
    */
    debugDumpData(QString::fromUtf8(addressbookUrlsResponse));
    QXmlStreamReader reader(addressbookUrlsResponse);
    QString statusText;
    QString addressbookHome;

    while (!reader.atEnd() && !reader.hasError()) {
        QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            if (reader.name().toString() == QLatin1String("addressbook-home-set")) {
                if (reader.readNextStartElement() && reader.name().toString() == QLatin1String("href")) {
                    addressbookHome = reader.readElementText();
                }
            } else if (reader.name().toString() == QLatin1String("status")) {
                statusText = reader.readElementText();
            }
        }
    }

    if (reader.hasError()) {
        LOG_WARNING(Q_FUNC_INFO << "error parsing response to addressbook home request:" << reader.errorString());
    }

    if (!statusText.contains(QLatin1String("200 OK"))) {
        LOG_WARNING(Q_FUNC_INFO << "invalid status response to addressbook home request:" << statusText);
    }

    return addressbookHome;
}

QList<ReplyParser::AddressBookInformation> ReplyParser::parseAddressbookInformation(const QByteArray &addressbookInformationResponse, const QString &addressbooksHomePath) const
{
    /* We expect a response of the form:
        <d:multistatus xmlns:d="DAV:" xmlns:cs="http://calendarserver.org/ns/" xmlns:card="urn:ietf:params:xml:ns:carddav">
            <d:response>
                <d:href>/addressbooks/johndoe/contacts/</d:href>
                <d:propstat>
                    <d:prop>
                        <d:resourcetype>
                            <d:collection />
                            <card:addressbook />
                        </d:resourcetype>
                        <d:displayname>My Address Book</d:displayname>
                        <d:current-user-privilege-set>
                            <d:privilege><d:read /></d:privilege>
                            <d:privilege><d:write /></d:privilege>
                        </d:current-user-privilege-set>
                        <cs:getctag>3145</cs:getctag>
                        <d:sync-token>http://sabredav.org/ns/sync-token/3145</d:sync-token>
                    </d:prop>
                    <d:status>HTTP/1.1 200 OK</d:status>
                </d:propstat>
            </d:response>
        </d:multistatus>
    */
    debugDumpData(QString::fromUtf8(addressbookInformationResponse));
    QXmlStreamReader reader(addressbookInformationResponse);
    QList<ReplyParser::AddressBookInformation> infos;
    QList<ReplyParser::AddressBookInformation> possibleAddressbookInfos;
    QList<ReplyParser::AddressBookInformation> unlikelyAddressbookInfos;

    QVariantMap vmap = xmlToVMap(reader);
    QVariantMap multistatusMap = vmap[QLatin1String("multistatus")].toMap();
    QVariantList responses;
    if (multistatusMap[QLatin1String("response")].type() == QVariant::List) {
        // multiple addressbooks.
        responses = multistatusMap[QLatin1String("response")].toList();
    } else {
        // only one addressbook.
        QVariantMap response = multistatusMap[QLatin1String("response")].toMap();
        responses << response;
    }

    // parse the information about each addressbook (response element)
    Q_FOREACH (const QVariant &rv, responses) {
        QVariantMap rmap = rv.toMap();
        ReplyParser::AddressBookInformation currInfo;
        currInfo.url = QUrl::fromPercentEncoding(rmap.value("href").toMap().value("@text").toString().toUtf8());
        if (!addressbooksHomePath.isEmpty() &&
               (currInfo.url == addressbooksHomePath ||
                currInfo.url == QStringLiteral("%1/").arg(addressbooksHomePath) ||
                (!currInfo.url.endsWith('/') &&
                  addressbooksHomePath.endsWith('/') &&
                  currInfo.url == addressbooksHomePath.mid(0, addressbooksHomePath.size()-1)))) {
            LOG_DEBUG("ignoring addressbook-home-set response returned for addressbook information request:" << currInfo.url);
            continue;
        }

        // some services (e.g. Cozy) return multiple propstat elements in each response
        QVariantList propstats;
        if (rmap.value("propstat").type() == QVariant::List) {
            propstats = rmap.value("propstat").toList();
        } else {
            QVariantMap propstat = rmap.value("propstat").toMap();
            propstats << propstat;
        }

        // examine the propstat elements to find the features we're interested in
        enum ResourceStatus { StatusUnknown = 0,
                              StatusExplicitly2xxOk = 1,
                              StatusExplicitlyTrue = 1,
                              StatusExplicitlyNotOk = 2,
                              StatusExplicitlyFalse = 2 };
        ResourceStatus addressbookResourceSpecified = StatusUnknown; // valid values are Unknown/True/False
        ResourceStatus resourcetypeStatus = StatusUnknown;  // valid values are Unknown/2xxOk/NotOk
        ResourceStatus otherPropertyStatus = StatusUnknown; // valid values are Unknown/2xxOk/NotOk
        Q_FOREACH (const QVariant &vpropstat, propstats) {
            QVariantMap propstat = vpropstat.toMap();
            const QVariantMap &prop(propstat.value("prop").toMap());
            if (prop.contains("getctag")) {
                currInfo.ctag = prop.value("getctag").toMap().value("@text").toString();
            }
            if (prop.contains("sync-token")) {
                currInfo.syncToken = prop.value("sync-token").toMap().value("@text").toString();
            }
            if (prop.contains("displayname")) {
                currInfo.displayName = prop.value("displayname").toMap().value("@text").toString();
            }
            if (prop.contains("current-user-privilege-set")) {
                bool foundWrite = false;
                const QVariantList privileges = prop.value("current-user-privilege-set").toMap().value("privilege").toList();
                for (const QVariant &pv : privileges) {
                    const QVariantMap pvm = pv.toMap();
                    if (pvm.contains("write")) {
                        foundWrite = true;
                    }
                }
                currInfo.readOnly = !foundWrite;
            }
            bool thisPropstatIsForResourceType = false;
            if (prop.contains("resourcetype")) {
                thisPropstatIsForResourceType = true;
                const QStringList resourceTypeKeys = prop.value("resourcetype").toMap().keys();
                const bool resourcetypeText = resourceTypeKeys.contains(QStringLiteral("@text")); // non-empty element.
                const bool resourcetypePrincipal = resourceTypeKeys.contains(QStringLiteral("principal"), Qt::CaseInsensitive);
                const bool resourcetypeAddressbook = resourceTypeKeys.contains(QStringLiteral("addressbook"), Qt::CaseInsensitive);
                const bool resourcetypeCollection = resourceTypeKeys.contains(QStringLiteral("collection"), Qt::CaseInsensitive);
                const bool resourcetypeCalendar = resourceTypeKeys.contains(QStringLiteral("calendar"), Qt::CaseInsensitive);
                const bool resourcetypeWriteProxy = resourceTypeKeys.contains(QStringLiteral("calendar-proxy-write"), Qt::CaseInsensitive);
                const bool resourcetypeReadProxy = resourceTypeKeys.contains(QStringLiteral("calendar-proxy-read"), Qt::CaseInsensitive);
                if (resourcetypeCalendar) {
                    // the resource is explicitly described as a calendar resource, not an addressbook.
                    addressbookResourceSpecified = StatusExplicitlyFalse;
                    LOG_DEBUG(Q_FUNC_INFO << "have calendar resource:" << currInfo.url << ", ignoring");
                } else if (resourcetypeWriteProxy || resourcetypeReadProxy) {
                    // the resource is a proxy resource, we don't support these resources.
                    addressbookResourceSpecified = StatusExplicitlyFalse;
                    LOG_DEBUG(Q_FUNC_INFO << "have" << (resourcetypeWriteProxy ? "write" : "read") << "proxy resource:" << currInfo.url << ", ignoring");
                } else if (resourcetypeAddressbook) {
                    // the resource is explicitly described as an addressbook resource.
                    addressbookResourceSpecified = StatusExplicitlyTrue;
                    LOG_DEBUG(Q_FUNC_INFO << "have addressbook resource:" << currInfo.url);
                } else if (resourcetypeCollection) {
                    if (resourceTypeKeys.size() == 1 ||
                            (resourceTypeKeys.size() == 2 && resourcetypeText) ||
                            (resourceTypeKeys.size() == 3 && resourcetypeText && resourcetypePrincipal)) {
                        // This is probably a carddav addressbook collection.
                        // Despite section 5.2 of RFC6352 stating that a CardDAV
                        // server MUST return the 'addressbook' value in the resource types
                        // property, some CardDAV implementations (eg, Memotoo, Kerio) do not.
                        addressbookResourceSpecified = StatusUnknown;
                        LOG_DEBUG(Q_FUNC_INFO << "have probable addressbook resource:" << currInfo.url);
                    } else {
                        // we don't know how to handle this resource type.
                        addressbookResourceSpecified = StatusExplicitlyFalse;
                        LOG_DEBUG(Q_FUNC_INFO << "have unknown" << (resourcetypePrincipal ? "principal" : "") << "non-addressbook collection resource:" << currInfo.url);
                    }
                } else {
                    // we don't know how to handle this resource type.
                    addressbookResourceSpecified = StatusExplicitlyFalse;
                    LOG_DEBUG(Q_FUNC_INFO << "have unknown" << (resourcetypePrincipal ? "principal" : "") << "non-collection resource:" << currInfo.url);
                }
            }
            // Some services (e.g. Cozy) return multiple propstats
            // where only one will refer to the resourcetype property itself;
            // others will refer to incidental properties like displayname etc.
            // Each propstat will (should) contain a status code, which applies
            // only to the properties referred to within the propstat.
            // Thus, a 404 code may only apply to a displayname, etc.
            if (propstat.contains("status")) {
                static const QRegularExpression Http2xxOk("2[0-9][0-9]");
                QString status = propstat.value("status").toMap().value("@text").toString();
                bool statusOk = status.contains(Http2xxOk); // any HTTP 2xx OK response
                if (thisPropstatIsForResourceType) {
                    // This status applies to the resourcetype property.
                    if (statusOk) {
                        resourcetypeStatus = StatusExplicitly2xxOk; // explicitly ok
                    } else {
                        resourcetypeStatus = StatusExplicitlyNotOk; // explicitly not ok
                        LOG_DEBUG(Q_FUNC_INFO << "response has non-OK status:" << status
                                              << "for properties:" << prop.keys()
                                              << "for url:" << currInfo.url);
                    }
                } else {
                    // This status applies to some other property.
                    // In some cases (e.g. Memotoo) we may need
                    // to infer that this status refers to the
                    // entire response.
                    if (statusOk) {
                        otherPropertyStatus = StatusExplicitly2xxOk; // explicitly ok
                    } else {
                        otherPropertyStatus = StatusExplicitlyNotOk; // explicitly not ok
                        LOG_DEBUG(Q_FUNC_INFO << "response has non-OK status:" << status
                                              << "for non-resourcetype properties:" << prop.keys()
                                              << "for url:" << currInfo.url);
                    }
                }
            }
        }

        // now check to see if we have all of the required information
        if (addressbookResourceSpecified == StatusExplicitlyTrue && resourcetypeStatus == StatusExplicitly2xxOk) {
            // we definitely had a well-specified resourcetype response, with 200 OK status.
            LOG_DEBUG(Q_FUNC_INFO << "have addressbook resource with status OK:" << currInfo.url);
        } else if (propstats.count() == 1                          // only one response element
                && addressbookResourceSpecified == StatusUnknown   // resource type unknown
                && otherPropertyStatus == StatusExplicitly2xxOk) { // status was explicitly ok
            // we assume that this was an implicit Addressbook Collection resourcetype response.
            // append it to our list of possible addressbook infos, to be added if we have no "certain" addressbooks.
            LOG_DEBUG(Q_FUNC_INFO << "have possible addressbook resource with status OK:" << currInfo.url);
            possibleAddressbookInfos.append(currInfo);
            continue;
        } else if (addressbookResourceSpecified == StatusUnknown
                && resourcetypeStatus == StatusExplicitly2xxOk) {
            // workaround for Kerio servers.  The "principal" may be used as
            // the carddav addressbook url if no other urls are valid.
            LOG_DEBUG(Q_FUNC_INFO << "have unlikely addressbook resource with status OK:" << currInfo.url);
            unlikelyAddressbookInfos.append(currInfo);
            continue;
        } else {
            // we either cannot infer that this was an Addressbook Collection
            // or we were told explicitly that the collection status was NOT OK.
            LOG_DEBUG(Q_FUNC_INFO << "ignoring resource:" << currInfo.url << "due to type or status:"
                                  << addressbookResourceSpecified << resourcetypeStatus << otherPropertyStatus);
            continue;
        }

        // add the addressbook to our return list.  If we have no sync-token or c-tag, we do manual delta detection.
        if (currInfo.ctag.isEmpty() && currInfo.syncToken.isEmpty()) {
            LOG_DEBUG(Q_FUNC_INFO << "addressbook:" << currInfo.url << "has no sync-token or c-tag");
        } else {
            LOG_DEBUG(Q_FUNC_INFO << "found valid addressbook:" << currInfo.url << "with sync-token or c-tag");
        }
        infos.append(currInfo);
    }

    // if the server was returning malformed response (without 'addressbook' resource type)
    // we can still use the response path as an addressbook url in some cases (e.g. Memotoo).
    if (infos.isEmpty()) {
        LOG_DEBUG(Q_FUNC_INFO << "Have no certain addressbook resources; assuming possible resources are addressbooks!");
        infos = possibleAddressbookInfos;
        if (infos.isEmpty()) {
            LOG_DEBUG(Q_FUNC_INFO << "Have no possible addressbook resources; assuming unlikely resources are addressbooks!");
            infos = unlikelyAddressbookInfos;
        }
    }

    return infos;
}

QList<ReplyParser::ContactInformation> ReplyParser::parseSyncTokenDelta(
        const QByteArray &syncTokenDeltaResponse,
        const QString &addressbookUrl,
        QString *newSyncToken) const
{
    /* We expect a response of the form:
        <?xml version="1.0" encoding="utf-8" ?>
        <d:multistatus xmlns:d="DAV:">
            <d:response>
                <d:href>/addressbooks/johndoe/contacts/newcard.vcf</d:href>
                <d:propstat>
                    <d:prop>
                        <d:getetag>"33441-34321"</d:getetag>
                    </d:prop>
                    <d:status>HTTP/1.1 200 OK</d:status>
                </d:propstat>
            </d:response>
            <d:response>
                <d:href>/addressbooks/johndoe/contacts/updatedcard.vcf</d:href>
                <d:propstat>
                    <d:prop>
                        <d:getetag>"33541-34696"</d:getetag>
                    </d:prop>
                    <d:status>HTTP/1.1 200 OK</d:status>
                </d:propstat>
            </d:response>
            <d:response>
                <d:href>/addressbooks/johndoe/contacts/deletedcard.vcf</d:href>
                <d:status>HTTP/1.1 404 Not Found</d:status>
            </d:response>
            <d:sync-token>http://sabredav.org/ns/sync/5001</d:sync-token>
         </d:multistatus>
    */
    debugDumpData(QString::fromUtf8(syncTokenDeltaResponse));
    QList<ReplyParser::ContactInformation> info;
    QXmlStreamReader reader(syncTokenDeltaResponse);
    QVariantMap vmap = xmlToVMap(reader);
    QVariantMap multistatusMap = vmap[QLatin1String("multistatus")].toMap();
    if (newSyncToken) {
        *newSyncToken = multistatusMap.value("sync-token").toMap().value("@text").toString();
    }

    QVariantList responses;
    if (multistatusMap[QLatin1String("response")].type() == QVariant::List) {
        // multiple updates in the delta.
        responses = multistatusMap[QLatin1String("response")].toList();
    } else {
        // only one update in the delta.
        QVariantMap response = multistatusMap[QLatin1String("response")].toMap();
        responses << response;
    }

    Q_FOREACH (const QVariant &rv, responses) {
        QVariantMap rmap = rv.toMap();
        ReplyParser::ContactInformation currInfo;
        currInfo.uri = QUrl::fromPercentEncoding(rmap.value("href").toMap().value("@text").toString().toUtf8());
        currInfo.etag = rmap.value("propstat").toMap().value("prop").toMap().value("getetag").toMap().value("@text").toString();
        QString status = rmap.value("status").toMap().value("@text").toString();
        if (status.isEmpty()) {
            status = rmap.value("propstat").toMap().value("status").toMap().value("@text").toString();
        }
        if (status.contains(QLatin1String("200 OK"))) {
            if (currInfo.uri.endsWith(QChar('/'))) {
                // this is probably a response for the addressbook resource,
                // rather than for a contact resource within the addressbook.
                LOG_DEBUG(Q_FUNC_INFO << "ignoring non-contact (addressbook?) resource:" << currInfo.uri << currInfo.etag << status);
                continue;
            } else if (currInfo.uri.length() > 5
                    && (currInfo.uri.at(currInfo.uri.length()-4) == QChar('.')
                           || currInfo.uri.at(currInfo.uri.length()-3) == QChar('.'))
                    && !currInfo.uri.endsWith(QStringLiteral(".vcf"), Qt::CaseInsensitive)) {
                // the uri has a file suffix like .ics or .eml rather than .vcf.
                // this is probably not a contact resource, but instead some other file reported erroneously.
                LOG_DEBUG(Q_FUNC_INFO << "ignoring non-contact resource:" << currInfo.uri << currInfo.etag << status);
                continue;
            }
            const QString oldEtag = q->m_localContactUrisEtags[addressbookUrl].value(currInfo.uri);
            currInfo.modType = oldEtag.isEmpty() ? ReplyParser::ContactInformation::Addition
                             : (currInfo.etag != oldEtag) ? ReplyParser::ContactInformation::Modification
                             : ReplyParser::ContactInformation::Unmodified;
        } else if (status.contains(QLatin1String("404 Not Found"))) {
            currInfo.modType = ReplyParser::ContactInformation::Deletion;
        } else {
            LOG_WARNING(Q_FUNC_INFO << "unknown response:" << currInfo.uri << currInfo.etag << status);
        }

        // only append the info if some valid info was contained in the response.
        if (!(currInfo.uri.isEmpty() && currInfo.etag.isEmpty() && status.isEmpty())) {
            info.append(currInfo);
        }
    }

    return info;
}

QList<ReplyParser::ContactInformation> ReplyParser::parseContactMetadata(
        const QByteArray &contactMetadataResponse,
        const QString &addressbookUrl,
        const QHash<QString, QString> &contactUriToEtag) const
{
    /* We expect a response of the form:
        HTTP/1.1 207 Multi-status
        Content-Type: application/xml; charset=utf-8

        <d:multistatus xmlns:d="DAV:" xmlns:card="urn:ietf:params:xml:ns:carddav">
            <d:response>
                <d:href>/addressbooks/johndoe/contacts/abc-def-fez-123454657.vcf</d:href>
                <d:propstat>
                    <d:prop>
                        <d:getetag>"2134-888"</d:getetag>
                    </d:prop>
                    <d:status>HTTP/1.1 200 OK</d:status>
                </d:propstat>
            </d:response>
            <d:response>
                <d:href>/addressbooks/johndoe/contacts/acme-12345.vcf</d:href>
                <d:propstat>
                    <d:prop>
                        <d:getetag>"9999-2344""</d:getetag>
                    </d:prop>
                    <d:status>HTTP/1.1 200 OK</d:status>
                </d:propstat>
            </d:response>
        </d:multistatus>
    */
    debugDumpData(QString::fromUtf8(contactMetadataResponse));
    QList<ReplyParser::ContactInformation> info;
    QXmlStreamReader reader(contactMetadataResponse);
    QVariantMap vmap = xmlToVMap(reader);
    QVariantMap multistatusMap = vmap[QLatin1String("multistatus")].toMap();
    QVariantList responses;
    if (multistatusMap[QLatin1String("response")].type() == QVariant::List) {
        // multiple updates in the delta.
        responses = multistatusMap[QLatin1String("response")].toList();
    } else {
        // only one update in the delta.
        QVariantMap response = multistatusMap[QLatin1String("response")].toMap();
        responses << response;
    }

    QSet<QString> seenUris;
    Q_FOREACH (const QVariant &rv, responses) {
        QVariantMap rmap = rv.toMap();
        ReplyParser::ContactInformation currInfo;
        currInfo.uri = QUrl::fromPercentEncoding(rmap.value("href").toMap().value("@text").toString().toUtf8());
        currInfo.etag = rmap.value("propstat").toMap().value("prop").toMap().value("getetag").toMap().value("@text").toString();
        QString status = rmap.value("propstat").toMap().value("status").toMap().value("@text").toString();
        if (status.isEmpty()) {
            status = rmap.value("status").toMap().value("@text").toString();
        }

        if (currInfo.uri.endsWith(QChar('/'))) {
            // this is probably a response for the addressbook resource,
            // rather than for a contact resource within the addressbook.
            LOG_DEBUG(Q_FUNC_INFO << "ignoring non-contact (addressbook?) resource:" << currInfo.uri << currInfo.etag << status);
            continue;
        } else if (currInfo.uri.length() > 5
                && (currInfo.uri.at(currInfo.uri.length()-4) == QChar('.')
                       || currInfo.uri.at(currInfo.uri.length()-3) == QChar('.'))
                && !currInfo.uri.endsWith(QStringLiteral(".vcf"), Qt::CaseInsensitive)) {
            // the uri has a file suffix like .ics or .eml rather than .vcf.
            // this is probably not a contact resource, but instead some other file reported erroneously.
            LOG_DEBUG(Q_FUNC_INFO << "ignoring non-contact resource:" << currInfo.uri << currInfo.etag << status);
            continue;
        }

        if (status.contains(QLatin1String("200 OK"))) {
            seenUris.insert(currInfo.uri);
            // only append if it's an addition or an actual modification
            // the etag will have changed since the last time we saw it,
            // if the contact has been modified server-side since last sync.
            if (!contactUriToEtag.contains(currInfo.uri)) {
                LOG_TRACE("Resource" << currInfo.uri << "was added on server with etag" << currInfo.etag << "to addressbook:" << addressbookUrl);
                currInfo.modType = ReplyParser::ContactInformation::Addition;
                info.append(currInfo);
            } else if (contactUriToEtag[currInfo.uri] != currInfo.etag) {
                LOG_TRACE("Resource" << currInfo.uri << "was modified on server in addressbook:" << addressbookUrl);
                LOG_TRACE("Old etag:" << contactUriToEtag[currInfo.uri] << "New etag:" << currInfo.etag);
                currInfo.modType = ReplyParser::ContactInformation::Modification;
                info.append(currInfo);
            } else {
                LOG_TRACE("Resource" << currInfo.uri << "is unchanged since last sync with etag" << currInfo.etag << "in addressbook:" << addressbookUrl);
                currInfo.modType = ReplyParser::ContactInformation::Unmodified;
                info.append(currInfo);
            }
        } else {
            LOG_WARNING(Q_FUNC_INFO << "unknown response:" << currInfo.uri << currInfo.etag << status);
        }
    }

    // we now need to determine deletions.
    for (const QString &uri : contactUriToEtag.keys()) {
        if (!seenUris.contains(uri)) {
            // this uri wasn't listed in the report, so this contact must have been deleted.
            LOG_TRACE("Resource" << uri << "was deleted on server in addressbook:" << addressbookUrl);
            ReplyParser::ContactInformation currInfo;
            currInfo.etag = contactUriToEtag.value(uri);
            currInfo.uri = uri;
            currInfo.modType = ReplyParser::ContactInformation::Deletion;
            info.append(currInfo);
        }
    }

    return info;
}

QHash<QString, QContact> ReplyParser::parseContactData(const QByteArray &contactData, const QString &addressbookUrl) const
{
    /* We expect a response of the form:
        HTTP/1.1 207 Multi-status
        Content-Type: application/xml; charset=utf-8

        <d:multistatus xmlns:d="DAV:" xmlns:card="urn:ietf:params:xml:ns:carddav">
            <d:response>
                <d:href>/addressbooks/johndoe/contacts/abc-def-fez-123454657.vcf</d:href>
                <d:propstat>
                    <d:prop>
                        <d:getetag>"2134-314"</d:getetag>
                        <card:address-data>BEGIN:VCARD
                            VERSION:3.0
                            FN:My Mother
                            UID:abc-def-fez-1234546578
                            END:VCARD
                        </card:address-data>
                    </d:prop>
                    <d:status>HTTP/1.1 200 OK</d:status>
                </d:propstat>
            </d:response>
            <d:response>
                <d:href>/addressbooks/johndoe/contacts/someapplication-12345678.vcf</d:href>
                <d:propstat>
                    <d:prop>
                        <d:getetag>"5467-323"</d:getetag>
                        <card:address-data>BEGIN:VCARD
                            VERSION:3.0
                            FN:Your Mother
                            UID:foo-bar-zim-gir-1234567
                            END:VCARD
                        </card:address-data>
                    </d:prop>
                    <d:status>HTTP/1.1 200 OK</d:status>
                </d:propstat>
            </d:response>
        </d:multistatus>
    */
    debugDumpData(QString::fromUtf8(contactData));
    QXmlStreamReader reader(contactData);
    const QVariantMap vmap = xmlToVMap(reader);
    const QVariantMap multistatusMap = vmap[QLatin1String("multistatus")].toMap();
    const QVariantList responses = (multistatusMap[QLatin1String("response")].type() == QVariant::List)
                                 ? multistatusMap[QLatin1String("response")].toList()
                                 : (QVariantList() << multistatusMap[QLatin1String("response")].toMap());

    QHash<QString, QContact> uriToContactData;
    for (const QVariant &rv : responses) {
        const QVariantMap rmap = rv.toMap();
        const QString uri = QUrl::fromPercentEncoding(rmap.value("href").toMap().value("@text").toString().toUtf8());
        const QString etag = rmap.value("propstat").toMap().value("prop").toMap().value("getetag").toMap().value("@text").toString();
        const QString vcard = rmap.value("propstat").toMap().value("prop").toMap().value("address-data").toMap().value("@text").toString();

        // import the data as a vCard
        bool ok = true;
        QPair<QContact, QStringList> result = m_converter->convertVCardToContact(vcard, &ok);
        if (!ok) {
            continue;
        }

        // fix up the GUID of the contact if required.
        QContact importedContact = result.first;
        QContactGuid guid = importedContact.detail<QContactGuid>();
        const QString uid = guid.guid();
        if (uid.isEmpty()) {
            LOG_WARNING(Q_FUNC_INFO << "contact import from vcard has no UID:\n" << vcard);
            continue;
        }
        if (!uid.startsWith(QStringLiteral("%1:AB:%2:").arg(QString::number(q->m_accountId), addressbookUrl))) {
            // prefix the UID with accountId and addressbook URI to avoid duplicated GUID issue.
            // RFC6352 only requires that the UID be unique within a single collection (addressbook).
            // So, we set the guid to be a compound of the accountId, addressbook URI and the UID.
            guid.setGuid(QStringLiteral("%1:AB:%2:%3").arg(QString::number(q->m_accountId), addressbookUrl, uid));
            importedContact.saveDetail(&guid, QContact::IgnoreAccessConstraints);
        }

        // store the sync target of the contact
        QContactSyncTarget syncTarget = importedContact.detail<QContactSyncTarget>();
        syncTarget.setSyncTarget(uri);
        importedContact.saveDetail(&syncTarget, QContact::IgnoreAccessConstraints);

        // store the etag into the contact
        QContactExtendedDetail etagDetail;
        for (const QContactExtendedDetail &ed : importedContact.details<QContactExtendedDetail>()) {
            if (ed.name() == KEY_ETAG) {
                etagDetail = ed;
                break;
            }
        }
        etagDetail.setName(KEY_ETAG);
        etagDetail.setData(etag);
        importedContact.saveDetail(&etagDetail, QContact::IgnoreAccessConstraints);

        // store unsupported properties into the contact.
        QContactExtendedDetail unsupportedPropertiesDetail;
        for (const QContactExtendedDetail &ed : importedContact.details<QContactExtendedDetail>()) {
            if (ed.name() == KEY_UNSUPPORTEDPROPERTIES) {
                unsupportedPropertiesDetail = ed;
                break;
            }
        }
        unsupportedPropertiesDetail.setName(KEY_UNSUPPORTEDPROPERTIES);
        unsupportedPropertiesDetail.setData(result.second);
        importedContact.saveDetail(&unsupportedPropertiesDetail, QContact::IgnoreAccessConstraints);

        // and insert into the return map.
        uriToContactData.insert(uri, importedContact);
    }

    return uriToContactData;
}

