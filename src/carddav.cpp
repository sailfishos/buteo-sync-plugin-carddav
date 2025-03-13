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

#include "carddav_p.h"
#include "syncer_p.h"

#include "logging.h"

#include <QRegularExpression>
#include <QUuid>
#include <QByteArray>
#include <QBuffer>
#include <QTimer>

#include <QContact>
#include <QContactGuid>
#include <QContactAvatar>
#include <QContactDisplayLabel>
#include <QContactName>
#include <QContactNickname>
#include <QContactBirthday>
#include <QContactTimestamp>
#include <QContactGender>
#include <QContactSyncTarget>
#include <QContactExtendedDetail>

#include <QVersitWriter>
#include <QVersitDocument>
#include <QVersitProperty>
#include <QVersitContactExporter>
#include <QVersitReader>
#include <QVersitContactImporter>

#ifdef USE_LIBCONTACTS
#include <seasidepropertyhandler.h>
#include <seasidecache.h>
#endif

#include <qtcontacts-extensions.h>

namespace {
    void debugDumpData(const QString &data)
    {
        if (!lcCardDavProtocol().isDebugEnabled()) {
            return;
        }

        QString dbgout;
        Q_FOREACH (const QChar &c, data) {
            if (c == '\r' || c == '\n') {
                if (!dbgout.isEmpty()) {
                    qCDebug(lcCardDavProtocol) << dbgout;
                    dbgout.clear();
                }
            } else {
                dbgout += c;
            }
        }
        if (!dbgout.isEmpty()) {
            qCDebug(lcCardDavProtocol) << dbgout;
        }
    }

    QContactId matchingContactFromList(const QContact &c, const QList<QContact> &contacts) {
        const QString uri = c.detail<QContactSyncTarget>().syncTarget();
        for (const QContact &other : contacts) {
            if (!uri.isEmpty() && uri == other.detail<QContactSyncTarget>().syncTarget()) {
                return other.id();
            }
        }
        return QContactId();
    }
}

CardDavVCardConverter::CardDavVCardConverter()
{
}

CardDavVCardConverter::~CardDavVCardConverter()
{
}

QStringList CardDavVCardConverter::supportedPropertyNames()
{
    // We only support a small number of (core) vCard properties
    // in this sync adapter.  The rest of the properties will
    // be cached so that we can stitch them back into the vCard
    // we upload on modification.
    QStringList supportedProperties;
    supportedProperties << "VERSION" << "PRODID" << "REV"
                        << "N" << "FN" << "NICKNAME" << "BDAY" << "X-GENDER"
                        << "EMAIL" << "TEL" << "ADR" << "URL" << "PHOTO"
                        << "ORG" << "TITLE" << "ROLE"
                        << "X-SIP" << "X-JABBER"
                        << "NOTE" << "UID";
    return supportedProperties;
}

QPair<QContact, QStringList> CardDavVCardConverter::convertVCardToContact(const QString &vcard, bool *ok)
{
    m_unsupportedProperties.clear();
    QVersitReader reader(vcard.toUtf8());
    reader.startReading();
    reader.waitForFinished();
    QList<QVersitDocument> vdocs = reader.results();
    if (vdocs.size() != 1) {
        qCWarning(lcCardDav) << Q_FUNC_INFO
                   << "invalid results during vcard import, got"
                   << vdocs.size() << "output from input:\n" << vcard;
        *ok = false;
        return QPair<QContact, QStringList>();
    }

    // convert the vCard into a QContact
    QVersitContactImporter importer;
    importer.setPropertyHandler(this);
    importer.importDocuments(vdocs);
    QList<QContact> importedContacts = importer.contacts();
    if (importedContacts.size() != 1) {
        qCWarning(lcCardDav) << Q_FUNC_INFO
                   << "invalid results during vcard conversion, got"
                   << importedContacts.size() << "output from input:\n" << vcard;
        *ok = false;
        return QPair<QContact, QStringList>();
    }

    QContact importedContact = importedContacts.first();
    QStringList unsupportedProperties = m_unsupportedProperties.value(importedContact.detail<QContactGuid>().guid());
    m_unsupportedProperties.clear();

    // If the contact has no structured name data, create a best-guess name for it.
    // This may be the case if the server provides an FN property but no N property.
    // Also, some detail types should be unique, so remove duplicates if present.
    QString displaylabelField, nicknameField;
    QContactName nameDetail;
    QSet<QContactDetail::DetailType> seenUniqueDetailTypes;
    QList<QContactDetail> importedContactDetails = importedContact.details();
    Q_FOREACH (const QContactDetail &d, importedContactDetails) {
        if (d.type() == QContactDetail::TypeName) {
            nameDetail = d;
        } else if (d.type() == QContactDetail::TypeDisplayLabel) {
            displaylabelField = d.value(QContactDisplayLabel::FieldLabel).toString().trimmed();
        } else if (d.type() == QContactDetail::TypeNickname) {
            nicknameField = d.value(QContactNickname::FieldNickname).toString().trimmed();
        } else if (d.type() == QContactDetail::TypeBirthday) {
            if (seenUniqueDetailTypes.contains(QContactDetail::TypeBirthday)) {
                // duplicated BDAY field seen from vCard.
                // remove this duplicate, else save will fail.
                QContactBirthday dupBday(d);
                importedContact.removeDetail(&dupBday);
                qCDebug(lcCardDav) << "Removed duplicate BDAY detail:" << dupBday;
            } else {
                seenUniqueDetailTypes.insert(QContactDetail::TypeBirthday);
            }
        } else if (d.type() == QContactDetail::TypeTimestamp) {
            if (seenUniqueDetailTypes.contains(QContactDetail::TypeTimestamp)) {
                // duplicated REV field seen from vCard.
                // remove this duplicate, else save will fail.
                QContactTimestamp dupRev(d);
                importedContact.removeDetail(&dupRev, QContact::IgnoreAccessConstraints);
                qCDebug(lcCardDav) << "Removed duplicate REV detail:" << dupRev;
                QContactTimestamp firstRev = importedContact.detail<QContactTimestamp>();
                if (dupRev.lastModified().isValid()
                        && (!firstRev.lastModified().isValid()
                            || dupRev.lastModified() > firstRev.lastModified())) {
                    firstRev.setLastModified(dupRev.lastModified());
                    importedContact.saveDetail(&firstRev, QContact::IgnoreAccessConstraints);
                }
            } else {
                seenUniqueDetailTypes.insert(QContactDetail::TypeTimestamp);
            }
        } else if (d.type() == QContactDetail::TypeGuid) {
            if (seenUniqueDetailTypes.contains(QContactDetail::TypeGuid)) {
                // duplicated UID field seen from vCard.
                // remove this duplicate, else save will fail.
                QContactGuid dupUid(d);
                importedContact.removeDetail(&dupUid);
                qCDebug(lcCardDav) << "Removed duplicate UID detail:" << dupUid;
            } else {
                seenUniqueDetailTypes.insert(QContactDetail::TypeGuid);
            }
        } else if (d.type() == QContactDetail::TypeGender) {
            if (seenUniqueDetailTypes.contains(QContactDetail::TypeGender)) {
                // duplicated X-GENDER field seen from vCard.
                // remove this duplicate, else save will fail.
                QContactGender dupGender(d);
                importedContact.removeDetail(&dupGender);
                qCDebug(lcCardDav) << "Removed duplicate X-GENDER detail:" << dupGender;
            } else {
                seenUniqueDetailTypes.insert(QContactDetail::TypeGender);
            }
        }
    }
    if (nameDetail.isEmpty() || (nameDetail.firstName().isEmpty() && nameDetail.lastName().isEmpty())) {
        // we have no valid name data but we may have display label or nickname data which we can decompose.
#ifdef USE_LIBCONTACTS
        if (!displaylabelField.isEmpty()) {
            SeasideCache::decomposeDisplayLabel(displaylabelField, &nameDetail);
            if (nameDetail.isEmpty()) {
                nameDetail.setCustomLabel(displaylabelField);
            }
            importedContact.saveDetail(&nameDetail, QContact::IgnoreAccessConstraints);
            qCDebug(lcCardDav) << "Decomposed vCard display name into structured name:" << nameDetail;
        } else if (!nicknameField.isEmpty()) {
            SeasideCache::decomposeDisplayLabel(nicknameField, &nameDetail);
            importedContact.saveDetail(&nameDetail, QContact::IgnoreAccessConstraints);
            qCDebug(lcCardDav) << "Decomposed vCard nickname into structured name:" << nameDetail;
        } else {
            qCWarning(lcCardDav) << "No structured name data exists in the vCard, contact will be unnamed!";
        }
#else
        qCWarning(lcCardDav) << "No structured name data exists in the vCard, contact will be unnamed!";
#endif
    }

    // mark each detail of the contact as modifiable
    Q_FOREACH (QContactDetail det, importedContact.details()) {
        det.setValue(QContactDetail__FieldModifiable, true);
        importedContact.saveDetail(&det, QContact::IgnoreAccessConstraints);
    }

    *ok = true;
    return qMakePair(importedContact, unsupportedProperties);
}

QString CardDavVCardConverter::convertContactToVCard(const QContact &c, const QStringList &unsupportedProperties)
{
    QList<QContact> exportList; exportList << c;
    QVersitContactExporter e;
    e.setDetailHandler(this);
    e.exportContacts(exportList);
    QByteArray output;
    QBuffer vCardBuffer(&output);
    vCardBuffer.open(QBuffer::WriteOnly);
    QVersitWriter writer(&vCardBuffer);
    writer.startWriting(e.documents());
    writer.waitForFinished();
    QString retn = QString::fromUtf8(output);

    // now add back the unsupported properties.
    Q_FOREACH (const QString &propStr, unsupportedProperties) {
        int endIdx = retn.lastIndexOf(QStringLiteral("END:VCARD"));
        if (endIdx > 0) {
            QString ecrlf = propStr + '\r' + '\n';
            retn.insert(endIdx, ecrlf);
        }
    }

    qCDebug(lcCardDav) << "generated vcard:";
    debugDumpData(retn);

    return retn;
}

QString CardDavVCardConverter::convertPropertyToString(const QVersitProperty &p) const
{
    QVersitDocument d(QVersitDocument::VCard30Type);
    d.addProperty(p);
    QByteArray out;
    QBuffer bout(&out);
    bout.open(QBuffer::WriteOnly);
    QVersitWriter w(&bout);
    w.startWriting(d);
    w.waitForFinished();
    QString retn = QString::fromLatin1(out);

    // strip out the BEGIN:VCARD\r\nVERSION:3.0\r\n and END:VCARD\r\n\r\n bits.
    int headerIdx = retn.indexOf(QStringLiteral("VERSION:3.0")) + 11;
    int footerIdx = retn.indexOf(QStringLiteral("END:VCARD"));
    if (headerIdx > 11 && footerIdx > 0 && footerIdx > headerIdx) {
        retn = retn.mid(headerIdx, footerIdx - headerIdx).trimmed();
        return retn;
    }

    qCWarning(lcCardDav) << Q_FUNC_INFO << "no string conversion possible for versit property:" << p.name();
    return QString();
}

void CardDavVCardConverter::propertyProcessed(const QVersitDocument &, const QVersitProperty &property,
                                               const QContact &, bool *alreadyProcessed,
                                               QList<QContactDetail> *updatedDetails)
{
    static QStringList supportedProperties(supportedPropertyNames());
    const QString propertyName(property.name().toUpper());
    if (propertyName == QLatin1String("PHOTO")) {
#ifdef USE_LIBCONTACTS
        // use the standard PHOTO handler from Seaside libcontacts
        QContactAvatar newAvatar = SeasidePropertyHandler::avatarFromPhotoProperty(property);
#else
        QContactAvatar newAvatar;
        QUrl url(property.variantValue().toString());
        if (url.isValid() && !url.isLocalFile()) {
            newAvatar.setImageUrl(url);
        }
#endif
        if (!newAvatar.isEmpty()) {
            updatedDetails->append(newAvatar);
        }
        // don't let the default PHOTO handler import it, even if we failed above.
        *alreadyProcessed = true;
        return;
    } else if (supportedProperties.contains(propertyName)) {
        // do nothing, let the default handler import them.
        *alreadyProcessed = true;
        return;
    }

    // cache the unsupported property string, and remove any detail
    // which was added by the default handler for this property.
    *alreadyProcessed = true;
    QString unsupportedProperty = convertPropertyToString(property);
    m_tempUnsupportedProperties.append(unsupportedProperty);
    updatedDetails->clear();
}

void CardDavVCardConverter::documentProcessed(const QVersitDocument &, QContact *c)
{
    // the UID of the contact will be contained in the QContactGuid detail.
    QString uid = c->detail<QContactGuid>().guid();
    if (uid.isEmpty()) {
        qCWarning(lcCardDav) << Q_FUNC_INFO << "imported contact has no UID, discarding unsupported properties!";
    } else {
        m_unsupportedProperties.insert(uid, m_tempUnsupportedProperties);
    }

    // get ready for the next import.
    m_tempUnsupportedProperties.clear();
}

void CardDavVCardConverter::contactProcessed(const QContact &c, QVersitDocument *d)
{
    // FN is a required field in vCard 3.0 and 4.0.  Add it if it does not exist.
    bool foundFN = false;
    Q_FOREACH (const QVersitProperty &p, d->properties()) {
        if (p.name() == QStringLiteral("FN")) {
            foundFN = true;
            break;
        }
    }

    // N is also a required field in vCard 3.0.  Add it if it does not exist.
    bool foundN = false;
    Q_FOREACH (const QVersitProperty &p, d->properties()) {
        if (p.name() == QStringLiteral("N")) {
            foundN = true;
            break;
        }
    }

    if (!foundFN || !foundN) {
#ifdef USE_LIBCONTACTS
        QString displaylabel = SeasideCache::generateDisplayLabel(c);
#else
        QContactName name = c.detail<QContactName>();
        QString displaylabel = QStringList {
            name.firstName(),
            name.middleName(),
            name.lastName(),
        }.join(' ');
#endif
        if (!foundFN) {
            QVersitProperty fnProp;
            fnProp.setName("FN");
            fnProp.setValue(displaylabel);
            d->addProperty(fnProp);
        }
        if (!foundN) {
            QContactName name = c.detail<QContactName>();
#ifdef USE_LIBCONTACTS
            SeasideCache::decomposeDisplayLabel(displaylabel, &name);
#endif
            if (name.firstName().isEmpty()) {
                // If we could not decompose the display label (e.g., only one token)
                // then just assume that the display label is a useful first name.
                name.setFirstName(displaylabel);
            }
            static const QStringList nvalue = { "", "", "", "", "" };
            QVersitProperty nProp;
            nProp.setName("N");
            nProp.setValueType(QVersitProperty::CompoundType);
            nProp.setValue(nvalue);
            d->addProperty(nProp);
        }
    }
}

void CardDavVCardConverter::detailProcessed(const QContact &, const QContactDetail &,
                                            const QVersitDocument &, QSet<int> *,
                                            QList<QVersitProperty> *, QList<QVersitProperty> *toBeAdded)
{
    static QStringList supportedProperties(supportedPropertyNames());
    for (int i = toBeAdded->size() - 1; i >= 0; --i) {
        const QString propName = toBeAdded->at(i).name().toUpper();
        if (!supportedProperties.contains(propName)) {
            // we don't support importing these properties, so we shouldn't
            // attempt to export them.
            toBeAdded->removeAt(i);
        } else if (propName == QStringLiteral("X-GENDER")
                && toBeAdded->at(i).value().toUpper() == QStringLiteral("UNSPECIFIED")) {
            // this is probably added "by default" since qtcontacts-sqlite always stores a gender.
            toBeAdded->removeAt(i);
        }
    }
}

CardDav::CardDav(Syncer *parent,
                 const QString &serverUrl,
                 const QString &addressbookPath,
                 const QString &username,
                 const QString &password)
    : QObject(parent)
    , q(parent)
    , m_converter(new CardDavVCardConverter)
    , m_request(new RequestGenerator(q, username, password))
    , m_parser(new ReplyParser(q, m_converter))
    , m_serverUrl(serverUrl)
    , m_addressbookPath(addressbookPath)
    , m_discoveryStage(CardDav::DiscoveryStarted)
    , m_addressbooksListOnly(false)
    , m_triedAddressbookPathAsHomeSetUrl(false)
{
}

CardDav::CardDav(Syncer *parent,
                 const QString &serverUrl,
                 const QString &addressbookPath,
                 const QString &accessToken)
    : QObject(parent)
    , q(parent)
    , m_converter(new CardDavVCardConverter)
    , m_request(new RequestGenerator(q, accessToken))
    , m_parser(new ReplyParser(q, m_converter))
    , m_serverUrl(serverUrl)
    , m_addressbookPath(addressbookPath)
    , m_discoveryStage(CardDav::DiscoveryStarted)
    , m_addressbooksListOnly(false)
{
}

CardDav::~CardDav()
{
    delete m_converter;
    delete m_parser;
    delete m_request;
}

void CardDav::errorOccurred(int httpError)
{
    emit error(httpError);
}

void CardDav::determineAddressbooksList()
{
    m_addressbooksListOnly = true;
    determineRemoteAMR();
}

void CardDav::determineRemoteAMR()
{
    if (m_addressbookPath.isEmpty()) {
        // The CardDAV sequence for determining the A/M/R delta is:
        // a)  fetch user information from the principal URL
        // b)  fetch addressbooks home url
        // c)  fetch addressbook information
        // d)  for each addressbook, either:
        //     i)  perform immediate delta sync (if webdav-sync enabled) OR
        //     ii) fetch etags, manually calculate delta
        // e) fetch full contacts for delta.

        // We start by fetching user information.
        fetchUserInformation();
    } else {
        // we can skip to step (c) of the discovery.
        fetchAddressbooksInformation(m_addressbookPath);
    }
}

void CardDav::fetchUserInformation()
{
    qCDebug(lcCardDav) << Q_FUNC_INFO << "requesting principal urls for user";

    // we need to specify the .well-known/carddav endpoint if it's the first
    // request (so we have not yet been redirected to the correct endpoint)
    // and if the path is empty/unknown.

    /*
        RFC 6764 section 6.5:

        * The client does a "PROPFIND" [RFC4918] request with the
          request URI set to the initial "context path".  The body of
          the request SHOULD include the DAV:current-user-principal
          [RFC5397] property as one of the properties to return.  Note
          that clients MUST properly handle HTTP redirect responses for
          the request.  The server will use the HTTP authentication
          procedure outlined in [RFC2617] or use some other appropriate
          authentication schemes to authenticate the user.

        * When an initial "context path" has not been determined from a
          TXT record, the initial "context path" is taken to be
          "/.well-known/caldav" (for CalDAV) or "/.well-known/carddav"
          (for CardDAV).

        * If the server returns a 404 ("Not Found") HTTP status response
          to the request on the initial "context path", clients MAY try
          repeating the request on the "root" URI "/" or prompt the user
          for a suitable path.
    */

    QUrl serverUrl(m_serverUrl);
    if (serverUrl.scheme().isEmpty() && (serverUrl.host().isEmpty() || serverUrl.path().isEmpty())) {
        // assume the supplied server url is like: "carddav.server.tld"
        m_serverUrl = QStringLiteral("https://%1/").arg(m_serverUrl);
        serverUrl = QUrl(m_serverUrl);
    }
    const QString wellKnownUrl = serverUrl.port() == -1
                               ? QStringLiteral("%1://%2/.well-known/carddav").arg(serverUrl.scheme()).arg(serverUrl.host())
                               : QStringLiteral("%1://%2:%3/.well-known/carddav").arg(serverUrl.scheme()).arg(serverUrl.host()).arg(serverUrl.port());
    bool firstRequest = m_discoveryStage == CardDav::DiscoveryStarted;
    m_serverUrl = firstRequest && (serverUrl.path().isEmpty() || serverUrl.path() == QStringLiteral("/"))
                ? wellKnownUrl
                : m_serverUrl;
    QNetworkReply *reply = m_request->currentUserInformation(m_serverUrl);
    if (!reply) {
        emit error();
        return;
    }

    connect(reply, SIGNAL(sslErrors(QList<QSslError>)), this, SLOT(sslErrorsOccurred(QList<QSslError>)));
    connect(reply, SIGNAL(finished()), this, SLOT(userInformationResponse()));
}

void CardDav::sslErrorsOccurred(const QList<QSslError> &errors)
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (q->m_ignoreSslErrors) {
        qCDebug(lcCardDav) << Q_FUNC_INFO << "ignoring SSL errors due to account policy:" << errors;
        reply->ignoreSslErrors(errors);
    } else {
        qCWarning(lcCardDav) << Q_FUNC_INFO << "SSL errors occurred, aborting:" << errors;
        errorOccurred(401);
    }
}

void CardDav::userInformationResponse()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    const QByteArray data = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        int httpError = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qCWarning(lcCardDav) << Q_FUNC_INFO << "error:" << reply->error() << "(" << httpError << ") to request" << m_serverUrl;
        debugDumpData(QString::fromUtf8(data));
        QUrl oldServerUrl(m_serverUrl);
        if (m_discoveryStage == CardDav::DiscoveryStarted && (httpError == 404 || httpError == 405)) {
            if (!oldServerUrl.path().endsWith(QStringLiteral(".well-known/carddav"))) {
                // From RFC 6764: If the initial "context path" derived from a TXT record
                // generates HTTP errors when targeted by requests, the client
                // SHOULD repeat its "bootstrapping" procedure using the
                // appropriate ".well-known" URI instead.
                qCDebug(lcCardDav) << Q_FUNC_INFO << "got HTTP response" << httpError << "to initial discovery request; trying well-known URI";
                m_serverUrl = oldServerUrl.port() == -1
                            ? QStringLiteral("%1://%2/.well-known/carddav").arg(oldServerUrl.scheme()).arg(oldServerUrl.host())
                            : QStringLiteral("%1://%2:%3/.well-known/carddav").arg(oldServerUrl.scheme()).arg(oldServerUrl.host()).arg(oldServerUrl.port());
                fetchUserInformation(); // set initial context path to well-known URI.
            } else {
                // From RFC 6764: if the server returns a 404 HTTP status response to the
                // request on the initial context path, clients may try repeating the request
                // on the root URI.
                // We also do this on HTTP 405 in case some implementation is non-spec-conformant.
                qCDebug(lcCardDav) << Q_FUNC_INFO << "got HTTP response" << httpError << "to well-known request; trying root URI";
                m_discoveryStage = CardDav::DiscoveryTryRoot;
                m_serverUrl = oldServerUrl.port() == -1
                            ? QStringLiteral("%1://%2/").arg(oldServerUrl.scheme()).arg(oldServerUrl.host())
                            : QStringLiteral("%1://%2:%3/").arg(oldServerUrl.scheme()).arg(oldServerUrl.host()).arg(oldServerUrl.port());
                fetchUserInformation();
            }
            return;
        }
        errorOccurred(httpError);
        return;
    }

    // if the request was to the /.well-known/carddav path, then we need to redirect
    QUrl redir = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    if (!redir.isEmpty()) {
        QUrl orig = reply->url();
        // In case of a relative redirect, resolve it, so the code below does not have to take relative redirects into account
        redir = orig.resolved(redir);
        qCDebug(lcCardDav) << Q_FUNC_INFO << "server requested redirect from:" << orig.toString() << "to:" << redir.toString();
        const bool hostChanged = orig.host() != redir.host();
        const bool pathChanged = orig.path() != redir.path();
        const bool schemeChanged = orig.scheme() != redir.scheme();
        const bool portChanged = orig.port() != redir.port();
        const bool validPathRedirect = orig.path().endsWith(QStringLiteral(".well-known/carddav"))
                                    || orig.path() == redir.path(); // e.g. scheme change.
        if (!hostChanged && !pathChanged && !schemeChanged && !portChanged) {
            // circular redirect, avoid the endless loop by aborting sync.
            qCWarning(lcCardDav) << Q_FUNC_INFO << "redirect specified is circular:" << redir.toString();
            errorOccurred(301);
        } else if (hostChanged || !validPathRedirect) {
            // possibly unsafe redirect.  for security, assume it's malicious and abort sync.
            qCWarning(lcCardDav) << Q_FUNC_INFO << "unexpected redirect from:" << orig.toString() << "to:" << redir.toString();
            errorOccurred(301);
        } else {
            // redirect as required, and change our server URL to point to the redirect URL.
            qCDebug(lcCardDav) << Q_FUNC_INFO << "redirecting from:" << orig.toString() << "to:" << redir.toString();
            m_serverUrl = redir.url();
            m_discoveryStage = CardDav::DiscoveryRedirected;
            fetchUserInformation();
        }
        return;
    }

    ReplyParser::ResponseType responseType = ReplyParser::UserPrincipalResponse;
    const QString userPath = m_parser->parseUserPrincipal(data, &responseType);
    if (responseType == ReplyParser::UserPrincipalResponse) {
        // the server responded with the expected user principal information.
        if (userPath.isEmpty()) {
            qCWarning(lcCardDav) << Q_FUNC_INFO << "unable to parse user principal from response";
            emit error();
            return;
        }
        fetchAddressbookUrls(userPath);
    } else if (responseType == ReplyParser::AddressbookInformationResponse) {
        // the server responded with addressbook information instead
        // of user principal information.  Skip the next discovery step.
        QList<ReplyParser::AddressBookInformation> infos = m_parser->parseAddressbookInformation(data, QString());
        if (infos.isEmpty()) {
            qCWarning(lcCardDav) << Q_FUNC_INFO << "unable to parse addressbook info from user principal response";
            emit error();
            return;
        }
        emit addressbooksList(infos);
    } else {
        qCWarning(lcCardDav) << Q_FUNC_INFO << "unknown response from user principal request";
        emit error();
    }
}

void CardDav::fetchAddressbookUrls(const QString &userPath)
{
    qCDebug(lcCardDav) << Q_FUNC_INFO << "requesting addressbook urls for user";
    QNetworkReply *reply = m_request->addressbookUrls(m_serverUrl, userPath);
    if (!reply) {
        emit error();
        return;
    }

    connect(reply, SIGNAL(sslErrors(QList<QSslError>)), this, SLOT(sslErrorsOccurred(QList<QSslError>)));
    connect(reply, SIGNAL(finished()), this, SLOT(addressbookUrlsResponse()));
}

void CardDav::addressbookUrlsResponse()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    const QByteArray data = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        int httpError = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qCWarning(lcCardDav) << Q_FUNC_INFO << "error:" << reply->error()
                   << "(" << httpError << ")";
        debugDumpData(QString::fromUtf8(data));
        errorOccurred(httpError);
        return;
    }

    const QString addressbooksHomePath = m_parser->parseAddressbookHome(data);
    if (addressbooksHomePath.isEmpty()) {
        qCWarning(lcCardDav) << Q_FUNC_INFO << "unable to parse addressbook home from response";
        emit error();
        return;
    }

    fetchAddressbooksInformation(addressbooksHomePath);
}

void CardDav::fetchAddressbooksInformation(const QString &addressbooksHomePath)
{
    qCDebug(lcCardDav) << Q_FUNC_INFO << "requesting addressbook sync information from" << addressbooksHomePath;
    QNetworkReply *reply = m_request->addressbooksInformation(m_serverUrl, addressbooksHomePath);
    reply->setProperty("addressbooksHomePath", addressbooksHomePath);
    if (!reply) {
        emit error();
        return;
    }

    connect(reply, SIGNAL(sslErrors(QList<QSslError>)), this, SLOT(sslErrorsOccurred(QList<QSslError>)));
    connect(reply, SIGNAL(finished()), this, SLOT(addressbooksInformationResponse()));
}

void CardDav::addressbooksInformationResponse()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    QString addressbooksHomePath = reply->property("addressbooksHomePath").toString();
    const QByteArray data = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        int httpError = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qCWarning(lcCardDav) << Q_FUNC_INFO << "error:" << reply->error()
                   << "(" << httpError << ")";
        debugDumpData(QString::fromUtf8(data));
        errorOccurred(httpError);
        return;
    }

    // if we didn't parse the addressbooks home path via discovery, but instead were provided it by the user,
    // then don't pass the path to the parser, as it uses it for cycle detection.
    if (m_addressbookPath == addressbooksHomePath) {
        addressbooksHomePath = QString();
    }

    QList<ReplyParser::AddressBookInformation> infos = m_parser->parseAddressbookInformation(data, addressbooksHomePath);
    if (infos.isEmpty()) {
        if (!m_addressbookPath.isEmpty() && !m_triedAddressbookPathAsHomeSetUrl) {
            // the user provided an addressbook path during account creation, which didn't work.
            // it may not be an addressbook path but instead the home set url; try that.
            qCDebug(lcCardDav) << Q_FUNC_INFO << "Given path is not addressbook path; trying as home set url";
            m_triedAddressbookPathAsHomeSetUrl = true;
            fetchAddressbookUrls(m_addressbookPath);
        } else {
            qCWarning(lcCardDav) << Q_FUNC_INFO << "unable to parse addressbook info from response";
            emit error();
        }
    } else {
        emit addressbooksList(infos);
    }
}

bool CardDav::downsyncAddressbookContent(
        const QString &addressbookUrl,
        const QString &newSyncToken,
        const QString &newCtag,
        const QString &oldSyncToken,
        const QString &oldCtag)
{
    if (newSyncToken.isEmpty() && newCtag.isEmpty()) {
        // we cannot use either sync-token or ctag for this addressbook.
        // we need to manually calculate the complete delta.
        qCDebug(lcCardDav) << "No sync-token or ctag given for addressbook:" << addressbookUrl << ", manual delta detection required";
        return fetchContactMetadata(addressbookUrl);
    } else if (newSyncToken.isEmpty()) {
        // we cannot use sync-token for this addressbook, but instead ctag.
        if (oldCtag.isEmpty()) {
            // first time sync
            // do etag request, the delta will be all remote additions
            return fetchContactMetadata(addressbookUrl);
        } else if (oldCtag != newCtag) {
            // changes have occurred since last sync
            // perform etag request and then manually calculate deltas.
            return fetchContactMetadata(addressbookUrl);
        } else {
            // no changes have occurred in this addressbook since last sync
            qCDebug(lcCardDav) << Q_FUNC_INFO << "no changes since last sync for"
                     << addressbookUrl << "from account" << q->m_accountId;
            QTimer::singleShot(0, this, [this, addressbookUrl] () {
                calculateContactChanges(addressbookUrl, QList<QContact>(), QList<QContact>());
            });
            return true;
        }
    } else {
        // the server supports webdav-sync for this addressbook.
        // attempt to perform synctoken sync
        if (oldSyncToken.isEmpty()) {
            // first time sync
            // perform slow sync / full report
            return fetchContactMetadata(addressbookUrl);
        } else if (oldSyncToken != newSyncToken) {
            // changes have occurred since last sync.
            // perform immediate delta sync, by passing the old sync token to the server.
            return fetchImmediateDelta(addressbookUrl, oldSyncToken);
        } else {
            // no changes have occurred in this addressbook since last sync
            qCDebug(lcCardDav) << Q_FUNC_INFO << "no changes since last sync for"
                     << addressbookUrl << "from account" << q->m_accountId;
            QTimer::singleShot(0, this, [this, addressbookUrl] () {
                calculateContactChanges(addressbookUrl, QList<QContact>(), QList<QContact>());
            });
            return true;
        }
    }
}

bool CardDav::fetchImmediateDelta(const QString &addressbookUrl, const QString &syncToken)
{
    qCDebug(lcCardDav) << Q_FUNC_INFO
             << "requesting immediate delta for addressbook" << addressbookUrl
             << "with sync token" << syncToken;

    QNetworkReply *reply = m_request->syncTokenDelta(m_serverUrl, addressbookUrl, syncToken);
    if (!reply) {
        return false;
    }

    reply->setProperty("addressbookUrl", addressbookUrl);
    connect(reply, SIGNAL(sslErrors(QList<QSslError>)), this, SLOT(sslErrorsOccurred(QList<QSslError>)));
    connect(reply, SIGNAL(finished()), this, SLOT(immediateDeltaResponse()));
    return true;
}

void CardDav::immediateDeltaResponse()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    const QString addressbookUrl = reply->property("addressbookUrl").toString();
    const QByteArray data = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        qCWarning(lcCardDav) << Q_FUNC_INFO << "error:" << reply->error()
                   << "(" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() << ")";
        debugDumpData(QString::fromUtf8(data));
        // The server is allowed to forget the syncToken by the
        // carddav protocol.  Try a full report sync just in case.
        fetchContactMetadata(addressbookUrl);
        return;
    }

    QString newSyncToken;
    QList<ReplyParser::ContactInformation> infos = m_parser->parseSyncTokenDelta(data, addressbookUrl, &newSyncToken);

    QContactCollection addressbook = q->m_currentCollections[addressbookUrl];
    addressbook.setExtendedMetaData(KEY_SYNCTOKEN, newSyncToken);
    q->m_currentCollections.insert(addressbookUrl, addressbook);

    fetchContacts(addressbookUrl, infos);
}

bool CardDav::fetchContactMetadata(const QString &addressbookUrl)
{
    qCDebug(lcCardDav) << Q_FUNC_INFO << "requesting contact metadata for addressbook" << addressbookUrl;
    QNetworkReply *reply = m_request->contactEtags(m_serverUrl, addressbookUrl);
    if (!reply) {
        return false;
    }

    reply->setProperty("addressbookUrl", addressbookUrl);
    connect(reply, SIGNAL(sslErrors(QList<QSslError>)), this, SLOT(sslErrorsOccurred(QList<QSslError>)));
    connect(reply, SIGNAL(finished()), this, SLOT(contactMetadataResponse()));
    return true;
}

void CardDav::contactMetadataResponse()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    const QString addressbookUrl = reply->property("addressbookUrl").toString();
    const QByteArray data = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        int httpError = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qCWarning(lcCardDav) << Q_FUNC_INFO << "error:" << reply->error()
                   << "(" << httpError << ")";
        debugDumpData(QString::fromUtf8(data));
        errorOccurred(httpError);
        return;
    }

    // if we are determining contact changes (i.e. delta) then we will
    // have local contact AMRU information cached for this addressbook.
    // build a cache list of the old etags of the still-existent contacts.
    QHash<QString, QString> uriToEtag;
    if (q->m_collectionAMRU.contains(addressbookUrl)) {
        auto createHash = [&uriToEtag] (const QList<QContact> &contacts) {
            for (const QContact &c : contacts) {
                const QString uri = c.detail<QContactSyncTarget>().syncTarget();
                if (uri.isEmpty()) {
                    qCWarning(lcCardDav) << Q_FUNC_INFO << ": carddav contact has empty sync target (uri): " << QString::fromLatin1(c.id().localId());
                } else {
                    const QList<QContactExtendedDetail> dets = c.details<QContactExtendedDetail>();
                    for (const QContactExtendedDetail &d : dets) {
                        if (d.name() == KEY_ETAG) {
                            uriToEtag.insert(uri, d.data().toString());
                            break;
                        }
                    }
                }
            }
        };
        createHash(q->m_collectionAMRU[addressbookUrl].modified);
        createHash(q->m_collectionAMRU[addressbookUrl].unmodified);
    }

    QList<ReplyParser::ContactInformation> infos = m_parser->parseContactMetadata(data, addressbookUrl, uriToEtag);
    fetchContacts(addressbookUrl, infos);
}

void CardDav::fetchContacts(const QString &addressbookUrl, const QList<ReplyParser::ContactInformation> &amrInfo)
{
    qCDebug(lcCardDav) << Q_FUNC_INFO << "requesting full contact information from addressbook" << addressbookUrl;

    // split into A/M/R/U request sets
    QStringList contactUris;
    Q_FOREACH (const ReplyParser::ContactInformation &info, amrInfo) {
        if (info.modType == ReplyParser::ContactInformation::Addition) {
            q->m_remoteAdditions[addressbookUrl].insert(info.uri, info);
            contactUris.append(info.uri);
        } else if (info.modType == ReplyParser::ContactInformation::Modification) {
            q->m_remoteModifications[addressbookUrl].insert(info.uri, info);
            contactUris.append(info.uri);
        } else if (info.modType == ReplyParser::ContactInformation::Deletion) {
            q->m_remoteRemovals[addressbookUrl].insert(info.uri, info);
        } else if (info.modType == ReplyParser::ContactInformation::Unmodified) {
            q->m_remoteUnmodified[addressbookUrl].insert(info.uri, info);
        } else {
            qCWarning(lcCardDav) << Q_FUNC_INFO << "no modification type in info for:" << info.uri;
        }
    }

    qCDebug(lcCardDav) << Q_FUNC_INFO << "Have calculated A/M/R/U:"
             << q->m_remoteAdditions[addressbookUrl].size() << "/"
             << q->m_remoteModifications[addressbookUrl].size() << "/"
             << q->m_remoteRemovals[addressbookUrl].size() << "/"
             << q->m_remoteUnmodified[addressbookUrl].size()
             << "for addressbook:" << addressbookUrl;

    if (contactUris.isEmpty()) {
        // no additions or modifications to fetch.
        qCDebug(lcCardDav) << Q_FUNC_INFO << "no further data to fetch";
        calculateContactChanges(addressbookUrl, QList<QContact>(), QList<QContact>());
    } else {
        // fetch the full contact data for additions/modifications.
        qCDebug(lcCardDav) << Q_FUNC_INFO << "fetching vcard data for" << contactUris.size() << "contacts";
        QNetworkReply *reply = m_request->contactMultiget(m_serverUrl, addressbookUrl, contactUris);
        if (!reply) {
            emit error();
            return;
        }

        reply->setProperty("addressbookUrl", addressbookUrl);
        connect(reply, SIGNAL(sslErrors(QList<QSslError>)), this, SLOT(sslErrorsOccurred(QList<QSslError>)));
        connect(reply, SIGNAL(finished()), this, SLOT(contactsResponse()));
    }
}

void CardDav::contactsResponse()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    const QString addressbookUrl = reply->property("addressbookUrl").toString();
    const QByteArray data = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        int httpError = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qCWarning(lcCardDav) << Q_FUNC_INFO << "error:" << reply->error()
                   << "(" << httpError << ")";
        debugDumpData(QString::fromUtf8(data));
        errorOccurred(httpError);
        return;
    }

    QList<QContact> added;
    QList<QContact> modified;

    const QHash<QString, QContact> addMods = m_parser->parseContactData(data, addressbookUrl);
    QHash<QString, QContact>::const_iterator it = addMods.constBegin(), end = addMods.constEnd();
    for ( ; it != end; ++it) {
        const QString contactUri = it.key();
        if (q->m_remoteAdditions[addressbookUrl].contains(contactUri)) {
            added.append(it.value());
        } else if (q->m_remoteModifications[addressbookUrl].contains(contactUri)) {
            modified.append(it.value());
        } else {
            qCWarning(lcCardDav) << Q_FUNC_INFO << "ignoring unknown addition/modification:" << contactUri;
        }
    }

    calculateContactChanges(addressbookUrl, added, modified);
}

void CardDav::calculateContactChanges(const QString &addressbookUrl, const QList<QContact> &added, const QList<QContact> &modified)
{
    // at this point, we have already retrieved the added+modified contacts from the server.
    // we need to populate the removed contacts list, by inspecting the local data.
    if (!q->m_collectionAMRU.contains(addressbookUrl)) {
        Q_ASSERT(modified.isEmpty());
        q->remoteContactsDetermined(q->m_currentCollections[addressbookUrl], added);
    } else {
        QList<QContact> removed;
        const Syncer::AMRU amru = q->m_collectionAMRU.take(addressbookUrl);
        auto appendMatches = [] (const QList<QContact> &contacts,
                                 const QHash<QString, QHash<QString, ReplyParser::ContactInformation> > &hash,
                                 QList<QContact> *list) {
            for (const QContact &c : contacts) {
                const QString uri = c.detail<QContactSyncTarget>().syncTarget();
                if (!uri.isEmpty() && hash.contains(uri)) {
                    list->append(c);
                }
            }
        };
        appendMatches(amru.added, q->m_remoteRemovals, &removed);
        appendMatches(amru.modified, q->m_remoteRemovals, &removed);
        appendMatches(amru.removed, q->m_remoteRemovals, &removed);
        appendMatches(amru.unmodified, q->m_remoteRemovals, &removed);

        // we also need to find the local ids associated with the modified contacts.
        QList<QContact> modifiedWithIds = modified;
        for (int i = 0; i < modifiedWithIds.size(); ++i) {
            QContact &c(modifiedWithIds[i]);
            QContactId matchingId = matchingContactFromList(c, amru.added);
            if (matchingId.isNull()) matchingId = matchingContactFromList(c, amru.modified);
            if (matchingId.isNull()) matchingId = matchingContactFromList(c, amru.removed);
            if (matchingId.isNull()) matchingId = matchingContactFromList(c, amru.unmodified);
            if (!matchingId.isNull()) {
                c.setId(matchingId);
            }
        }

        // TODO: also match remotely added to locally added, to find partial upsync artifacts.
        q->remoteContactChangesDetermined(q->m_currentCollections[addressbookUrl], added, modifiedWithIds, removed);
    }
}

static void setContactGuid(QContact *c, const QString &uid)
{
    QContactGuid newGuid = c->detail<QContactGuid>();
    newGuid.setGuid(uid);
    c->saveDetail(&newGuid, QContact::IgnoreAccessConstraints);
}

bool CardDav::upsyncUpdates(const QString &addressbookUrl, const QList<QContact> &added, const QList<QContact> &modified, const QList<QContact> &removed)
{
    qCDebug(lcCardDav) << Q_FUNC_INFO
             << "upsyncing updates to addressbook:" << addressbookUrl
             << ":" << added.count() << modified.count() << removed.count();

    bool hadNonSpuriousChanges = false;
    int spuriousModifications = 0;

    m_upsyncRequests.insert(addressbookUrl, 0);
    if (added.size() || modified.size()) {
        m_upsyncedChanges.insert(addressbookUrl, UpsyncedContacts());
    }

    // put local additions
    for (int i = 0; i < added.size(); ++i) {
        QContact c = added.at(i);

        // generate a server-side uid.  this does NOT contain addressbook prefix etc.
        const QString uid = QUuid::createUuid().toString().replace(QRegularExpression(QStringLiteral("[\\-{}]")), QString());
        // set the uid so that the VCF UID is generated.
        setContactGuid(&c, uid);

        // generate a valid uri
        const QString uri = addressbookUrl + (addressbookUrl.endsWith('/') ? QString() : QStringLiteral("/")) + uid + QStringLiteral(".vcf");
        QContactSyncTarget st = c.detail<QContactSyncTarget>();
        st.setSyncTarget(uri);
        c.saveDetail(&st, QContact::IgnoreAccessConstraints);

        // ensure that we haven't already upsynced this one previously, i.e. partial upsync artifact
        if (q->m_remoteAdditions[addressbookUrl].contains(uri)
                || q->m_remoteModifications[addressbookUrl].contains(uri)
                || q->m_remoteRemovals[addressbookUrl].contains(uri)
                || q->m_remoteUnmodified[addressbookUrl].contains(uri)) {
            // this contact was previously upsynced already.
            continue;
        }

        // generate a vcard
        const QString vcard = m_converter->convertContactToVCard(c, QStringList());
        // upload
        QNetworkReply *reply = m_request->upsyncAddMod(m_serverUrl, uri, QString(), vcard);
        if (!reply) {
            return false;
        }

        // set the addressbook-prefixed guid into the contact.
        const QString guid = QStringLiteral("%1:AB:%2:%3").arg(QString::number(q->m_accountId), addressbookUrl, uid);
        setContactGuid(&c, guid);

        // cached the updated contact, as it will eventually be written back to the local database with updated guid + etag.
        m_upsyncedChanges[addressbookUrl].additions.append(c);
        m_upsyncRequests[addressbookUrl] += 1;
        hadNonSpuriousChanges = true;
        reply->setProperty("addressbookUrl", addressbookUrl);
        reply->setProperty("contactGuid", guid);
        connect(reply, SIGNAL(sslErrors(QList<QSslError>)), this, SLOT(sslErrorsOccurred(QList<QSslError>)));
        connect(reply, SIGNAL(finished()), this, SLOT(upsyncResponse()));
    }

    // put local modifications
    for (int i = 0; i < modified.size(); ++i) {
        QContact c = modified.at(i);

        // reinstate the server-side UID into the guid detail for upsync
        const QString guidstr = c.detail<QContactGuid>().guid();
        const QString uidPrefix = QStringLiteral("%1:AB:%2:").arg(QString::number(q->m_accountId), addressbookUrl);
        if (guidstr.isEmpty()) {
            qCWarning(lcCardDav) << Q_FUNC_INFO << "modified contact has no guid:" << c.id().toString();
            continue; // TODO: this is actually an error.
        } else if (!guidstr.startsWith(uidPrefix)) {
            qCWarning(lcCardDav) << Q_FUNC_INFO << "modified contact: " << QString::fromLatin1(c.id().localId())
                                    << "has guid with invalid form: " << guidstr;
            continue; // TODO: this is actually an error.
        } else {
            const QString uidstr = guidstr.mid(uidPrefix.size());
            setContactGuid(&c, uidstr);
        }

        QString etag;
        for (const QContactExtendedDetail &ed : c.details<QContactExtendedDetail>()) {
            if (ed.name() == KEY_ETAG) {
                etag = ed.data().toString();
                break;
            }
        }

        QStringList unsupportedProperties;
        for (const QContactExtendedDetail &ed : c.details<QContactExtendedDetail>()) {
            if (ed.name() == KEY_UNSUPPORTEDPROPERTIES) {
                unsupportedProperties = ed.data().toStringList();
                break;
            }
        }

        // convert to vcard and upsync to remote server.
        const QString uri = c.detail<QContactSyncTarget>().syncTarget();
        const QString vcard = m_converter->convertContactToVCard(c, unsupportedProperties);

        // upload
        QNetworkReply *reply = m_request->upsyncAddMod(m_serverUrl, uri, etag, vcard);
        if (!reply) {
            return false;
        }

        // cached the updated contact, as it will eventually be written back to the local database with updated guid + etag.
        setContactGuid(&c, guidstr);
        m_upsyncedChanges[addressbookUrl].modifications.append(c);
        m_upsyncRequests[addressbookUrl] += 1;
        hadNonSpuriousChanges = true;
        reply->setProperty("addressbookUrl", addressbookUrl);
        reply->setProperty("contactGuid", guidstr);
        connect(reply, SIGNAL(sslErrors(QList<QSslError>)), this, SLOT(sslErrorsOccurred(QList<QSslError>)));
        connect(reply, SIGNAL(finished()), this, SLOT(upsyncResponse()));
    }

    // delete local removals
    for (int i = 0; i < removed.size(); ++i) {
        QContact c = removed[i];
        const QString guidstr = c.detail<QContactGuid>().guid();
        const QString uri = c.detail<QContactSyncTarget>().syncTarget();
        if (uri.isEmpty()) {
            qCWarning(lcCardDav) << Q_FUNC_INFO << "deleted contact server uri unknown:" << QString::fromLatin1(c.id().localId()) << " - " << guidstr;
            continue; // TODO: this is actually an error.
        }
        QString etag;
        for (const QContactExtendedDetail &ed : c.details<QContactExtendedDetail>()) {
            if (ed.name() == KEY_ETAG) {
                etag = ed.data().toString();
                break;
            }
        }
        QNetworkReply *reply = m_request->upsyncDeletion(m_serverUrl, uri, etag);
        if (!reply) {
            return false;
        }

        m_upsyncRequests[addressbookUrl] += 1;
        hadNonSpuriousChanges = true;
        reply->setProperty("addressbookUrl", addressbookUrl);
        connect(reply, SIGNAL(sslErrors(QList<QSslError>)), this, SLOT(sslErrorsOccurred(QList<QSslError>)));
        connect(reply, SIGNAL(finished()), this, SLOT(upsyncResponse()));
    }

    if (!hadNonSpuriousChanges || (added.size() == 0 && modified.size() == 0 && removed.size() == 0)) {
        // nothing to upsync.  Use a singleshot to avoid synchronously
        // decrementing the m_upsyncRequests count to zero if there
        // happens to be nothing to upsync to the first addressbook.
        m_upsyncRequests[addressbookUrl] += 1;
        QMetaObject::invokeMethod(this, "upsyncComplete", Qt::QueuedConnection, Q_ARG(QString, addressbookUrl));
    }

    // clear our caches of info for this addressbook, no longer required.
    q->m_remoteAdditions.remove(addressbookUrl);
    q->m_remoteModifications.remove(addressbookUrl);
    q->m_remoteRemovals.remove(addressbookUrl);
    q->m_remoteUnmodified.remove(addressbookUrl);

    qCDebug(lcCardDav) << Q_FUNC_INFO << "ignored" << spuriousModifications << "spurious updates to addressbook:" << addressbookUrl;
    return true;
}

void CardDav::upsyncResponse()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    const QString addressbookUrl = reply->property("addressbookUrl").toString();
    const QString guid = reply->property("contactGuid").toString();
    const QByteArray data = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        int httpError = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qCWarning(lcCardDav) << Q_FUNC_INFO << "error:" << reply->error()
                   << "(" << httpError << ")";
        debugDumpData(QString::fromUtf8(data));
        if (httpError == 405) {
            // MethodNotAllowed error.  Most likely the server has restricted
            // new writes to the collection (e.g., read-only or update-only).
            // We should not abort the sync if we receive this error.
            qCWarning(lcCardDav) << Q_FUNC_INFO << "405 MethodNotAllowed - is the collection read-only?";
            qCWarning(lcCardDav) << Q_FUNC_INFO << "continuing sync despite this error - upsync will have failed!";
        } else {
            errorOccurred(httpError);
            return;
        }
    }

    if (!guid.isEmpty()) {
        // this is an addition or modification.
        // get the new etag value reported by the server.
        QString etag;
        Q_FOREACH(const QByteArray &header, reply->rawHeaderList()) {
            if (QString::fromUtf8(header).contains(QLatin1String("etag"), Qt::CaseInsensitive)) {
                etag = reply->rawHeader(header);
                break;
            }
        }

        if (!etag.isEmpty()) {
            qCDebug(lcCardDav) << "Got updated etag for" << guid << ":" << etag;
            // store the updated etag into the upsynced contact
            auto updateEtag = [this, &guid, etag] (QList<QContact> &upsynced) {
                for (int i = upsynced.size() - 1; i >= 0; --i) {
                    if (upsynced[i].detail<QContactGuid>().guid() == guid) {
                        QContactExtendedDetail etagDetail;
                        for (const QContactExtendedDetail &ed : upsynced[i].details<QContactExtendedDetail>()) {
                            if (ed.name() == KEY_ETAG) {
                                etagDetail = ed;
                                break;
                            }
                        }
                        etagDetail.setName(KEY_ETAG);
                        etagDetail.setData(etag);
                        upsynced[i].saveDetail(&etagDetail, QContact::IgnoreAccessConstraints);
                        break;
                    }
                }
            };
            updateEtag(m_upsyncedChanges[addressbookUrl].additions);
            updateEtag(m_upsyncedChanges[addressbookUrl].modifications);
        } else {
            // If we don't perform an additional request, the etag server-side will be different to the etag
            // we have locally, and thus on next sync we would spuriously detect a server-side modification.
            // That's ok, we'll just detect that it's spurious via data inspection during the next sync.
            qCWarning(lcCardDav) << "No updated etag provided for" << guid << ": will be reported as spurious remote modification next sync";
        }
    }

    upsyncComplete(addressbookUrl);
}

void CardDav::upsyncComplete(const QString &addressbookUrl)
{
    m_upsyncRequests[addressbookUrl] -= 1;
    if (m_upsyncRequests[addressbookUrl] == 0) {
        // finished upsyncing all data for the addressbook.
        qCDebug(lcCardDav) << Q_FUNC_INFO << "upsync complete for addressbook: " << addressbookUrl;
        // TODO: perform another request to get the ctag/synctoken after updates have been upsynced?
        q->localChangesStoredRemotely(
                q->m_currentCollections[addressbookUrl],
                m_upsyncedChanges[addressbookUrl].additions,
                m_upsyncedChanges[addressbookUrl].modifications);
        m_upsyncedChanges.remove(addressbookUrl);
        q->m_previousCtagSyncToken.remove(addressbookUrl);
        q->m_currentCollections.remove(addressbookUrl);
        q->m_localContactUrisEtags.remove(addressbookUrl);
    }
}
