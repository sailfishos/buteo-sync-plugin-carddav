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

#include "requestgenerator_p.h"
#include "syncer_p.h"

#include "logging.h"

#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QNetworkReply>

#include <QStringList>
#include <QBuffer>
#include <QByteArray>

#include <QtContacts/QContact>

namespace {
    QUrl setRequestUrl(const QString &url, const QString &path, const QString &username, const QString &password)
    {
        QUrl ret(url);
        QString modifiedPath(path);
        if (!path.isEmpty()) {
            // common case: the path may contain %40 instead of @ symbol,
            // if the server returns paths in percent-encoded form.
            // QUrl::setPath() will automatically percent-encode the input,
            // so if we have received percent-encoded path, we need to undo
            // the percent encoding first.  This is suboptimal but works
            // at least for the common case.
            if (path.contains(QStringLiteral("%40"))) {
                modifiedPath = QUrl::fromPercentEncoding(path.toUtf8());
            }

            // override the path from the given url with the path argument.
            // this is because the initial URL may be a user-principals URL
            // but subsequent paths are not relative to that one, but instead
            // are relative to the root path /
            if (path.startsWith('/')) {
                ret.setPath(modifiedPath);
            } else {
                ret.setPath('/' + modifiedPath);
            }
        }
        if (!username.isEmpty() && !password.isEmpty()) {
            ret.setUserName(username);
            ret.setPassword(password);
        }
        return ret;
    }

    QNetworkRequest setRequestData(const QUrl &url,
                                   const QByteArray &requestData,
                                   const QString &depth,
                                   const QString &ifMatch,
                                   const QString &contentType,
                                   const QString &accessToken)
    {
        QNetworkRequest ret(url);
        if (!contentType.isEmpty()) {
            ret.setHeader(QNetworkRequest::ContentTypeHeader,
                          contentType.toUtf8());
        }
        ret.setHeader(QNetworkRequest::ContentLengthHeader,
                      requestData.length());
        if (!depth.isEmpty()) {
            ret.setRawHeader("Depth", depth.toUtf8());
        }
        if (!ifMatch.isEmpty()) {
            ret.setRawHeader("If-Match", ifMatch.toUtf8());
        }
        if (!accessToken.isEmpty()) {
            ret.setRawHeader("Authorization",
                             QString(QLatin1String("Bearer ")
                             + accessToken).toUtf8());
        }
        return ret;
    }
}

RequestGenerator::RequestGenerator(Syncer *parent,
                                   const QString &username,
                                   const QString &password)
    : q(parent)
    , m_username(username)
    , m_password(password)
{
}

RequestGenerator::RequestGenerator(Syncer *parent,
                                   const QString &accessToken)
    : q(parent)
    , m_accessToken(accessToken)
{
}

QNetworkReply *RequestGenerator::generateRequest(const QString &url,
                                                 const QString &path,
                                                 const QString &depth,
                                                 const QString &requestType,
                                                 const QString &request) const
{
    const QByteArray contentType("application/xml; charset=utf-8");
    QByteArray requestData(request.toUtf8());
    QUrl reqUrl(setRequestUrl(url, path, m_username, m_password));
    QNetworkRequest req(setRequestData(reqUrl, requestData, depth, QString(), contentType, m_accessToken));
    QBuffer *requestDataBuffer = new QBuffer(q);
    requestDataBuffer->setData(requestData);
    qCDebug(lcCardDav) << "generateRequest():"
            << m_accessToken << reqUrl << depth << requestType
            << QString::fromUtf8(requestData);
    return q->m_qnam.sendCustomRequest(req, requestType.toLatin1(), requestDataBuffer);
}

QNetworkReply *RequestGenerator::generateUpsyncRequest(const QString &url,
                                                       const QString &path,
                                                       const QString &ifMatch,
                                                       const QString &contentType,
                                                       const QString &requestType,
                                                       const QString &request) const
{
    QByteArray requestData(request.toUtf8());
    QUrl reqUrl(setRequestUrl(url, path, m_username, m_password));
    QNetworkRequest req(setRequestData(reqUrl, requestData, QString(), ifMatch, contentType, m_accessToken));

    qCDebug(lcCardDav) << "generateUpsyncRequest():" << m_accessToken << reqUrl << requestType << ":" << requestData.length() << "bytes";
    Q_FOREACH (const QByteArray &headerName, req.rawHeaderList()) {
        qCDebug(lcCardDav) << "   " << headerName << "=" << req.rawHeader(headerName);
    }

    if (!request.isEmpty()) {
        QBuffer *requestDataBuffer = new QBuffer(q);
        requestDataBuffer->setData(requestData);
        return q->m_qnam.sendCustomRequest(req, requestType.toLatin1(), requestDataBuffer);
    }

    return q->m_qnam.sendCustomRequest(req, requestType.toLatin1());
}

QNetworkReply *RequestGenerator::currentUserInformation(const QString &serverUrl)
{
    if (Q_UNLIKELY(serverUrl.isEmpty())) {
        qCWarning(lcCardDav) << Q_FUNC_INFO << "server url empty, aborting";
        return 0;
    }

    QString requestStr = QStringLiteral(
        "<d:propfind xmlns:d=\"DAV:\">"
          "<d:prop>"
             "<d:current-user-principal />"
          "</d:prop>"
        "</d:propfind>");

    return generateRequest(serverUrl, QString(), QLatin1String("0"), QLatin1String("PROPFIND"), requestStr);
}

QNetworkReply *RequestGenerator::addressbookUrls(const QString &serverUrl, const QString &userPath)
{
    if (Q_UNLIKELY(userPath.isEmpty())) {
        qCWarning(lcCardDav) << Q_FUNC_INFO << "user path empty, aborting";
        return 0;
    }

    if (Q_UNLIKELY(serverUrl.isEmpty())) {
        qCWarning(lcCardDav) << Q_FUNC_INFO << "server url empty, aborting";
        return 0;
    }

    QString requestStr = QStringLiteral(
        "<d:propfind xmlns:d=\"DAV:\" xmlns:card=\"urn:ietf:params:xml:ns:carddav\">"
          "<d:prop>"
             "<card:addressbook-home-set />"
          "</d:prop>"
        "</d:propfind>");

    return generateRequest(serverUrl, userPath, QLatin1String("0"), QLatin1String("PROPFIND"), requestStr);
}

QNetworkReply *RequestGenerator::addressbooksInformation(const QString &serverUrl, const QString &userAddressbooksPath)
{
    if (Q_UNLIKELY(userAddressbooksPath.isEmpty())) {
        qCWarning(lcCardDav) << Q_FUNC_INFO << "addressbooks path empty, aborting";
        return 0;
    }

    if (Q_UNLIKELY(serverUrl.isEmpty())) {
        qCWarning(lcCardDav) << Q_FUNC_INFO << "server url empty, aborting";
        return 0;
    }

    QString requestStr = QStringLiteral(
        "<d:propfind xmlns:d=\"DAV:\" xmlns:cs=\"http://calendarserver.org/ns/\">"
          "<d:prop>"
             "<d:resourcetype />"
             "<d:displayname />"
             "<d:current-user-privilege-set />"
             "<d:sync-token />"
             "<cs:getctag />"
          "</d:prop>"
        "</d:propfind>");

    return generateRequest(serverUrl, userAddressbooksPath, QLatin1String("1"), QLatin1String("PROPFIND"), requestStr);
}

QNetworkReply *RequestGenerator::addressbookInformation(const QString &serverUrl, const QString &addressbookPath)
{
    if (Q_UNLIKELY(addressbookPath.isEmpty())) {
        qCWarning(lcCardDav) << Q_FUNC_INFO << "addressbook path empty, aborting";
        return 0;
    }

    if (Q_UNLIKELY(serverUrl.isEmpty())) {
        qCWarning(lcCardDav) << Q_FUNC_INFO << "server url empty, aborting";
        return 0;
    }

    QString requestStr = QStringLiteral(
        "<d:propfind xmlns:d=\"DAV:\" xmlns:cs=\"http://calendarserver.org/ns/\">"
          "<d:prop>"
             "<d:resourcetype />"
             "<d:displayname />"
             "<d:sync-token />"
             "<cs:getctag />"
          "</d:prop>"
        "</d:propfind>");

    return generateRequest(serverUrl, addressbookPath, QLatin1String("0"), QLatin1String("PROPFIND"), requestStr);
}

QNetworkReply *RequestGenerator::syncTokenDelta(const QString &serverUrl, const QString &addressbookUrl, const QString &syncToken)
{
    if (Q_UNLIKELY(syncToken.isEmpty())) {
        qCWarning(lcCardDav) << Q_FUNC_INFO << "sync token empty, aborting";
        return 0;
    }

    if (Q_UNLIKELY(addressbookUrl.isEmpty())) {
        qCWarning(lcCardDav) << Q_FUNC_INFO << "addressbook url empty, aborting";
        return 0;
    }

    if (Q_UNLIKELY(serverUrl.isEmpty())) {
        qCWarning(lcCardDav) << Q_FUNC_INFO << "server url empty, aborting";
        return 0;
    }

    QString requestStr = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\" ?>"
        "<d:sync-collection xmlns:d=\"DAV:\">"
          "<d:sync-token>%1</d:sync-token>"
          "<d:sync-level>1</d:sync-level>"
          "<d:prop>"
            "<d:getetag/>"
          "</d:prop>"
        "</d:sync-collection>").arg(syncToken.toHtmlEscaped());

    return generateRequest(serverUrl, addressbookUrl, QString(), QLatin1String("REPORT"), requestStr);
}

QNetworkReply *RequestGenerator::contactEtags(const QString &serverUrl, const QString &addressbookPath)
{
    if (Q_UNLIKELY(addressbookPath.isEmpty())) {
        qCWarning(lcCardDav) << Q_FUNC_INFO << "addressbook path empty, aborting";
        return 0;
    }

    if (Q_UNLIKELY(serverUrl.isEmpty())) {
        qCWarning(lcCardDav) << Q_FUNC_INFO << "server url empty, aborting";
        return 0;
    }

    QString requestStr = QStringLiteral(
        "<d:propfind xmlns:d=\"DAV:\">"
          "<d:prop>"
             "<d:getetag />"
          "</d:prop>"
        "</d:propfind>");

    return generateRequest(serverUrl, addressbookPath, QLatin1String("1"), QLatin1String("PROPFIND"), requestStr);
}

QNetworkReply *RequestGenerator::contactData(const QString &serverUrl, const QString &addressbookPath, const QStringList &contactEtags)
{
    if (Q_UNLIKELY(contactEtags.isEmpty())) {
        qCWarning(lcCardDav) << Q_FUNC_INFO << "etag list empty, aborting";
        return 0;
    }

    if (Q_UNLIKELY(addressbookPath.isEmpty())) {
        qCWarning(lcCardDav) << Q_FUNC_INFO << "addressbook path empty, aborting";
        return 0;
    }

    if (Q_UNLIKELY(serverUrl.isEmpty())) {
        qCWarning(lcCardDav) << Q_FUNC_INFO << "server url empty, aborting";
        return 0;
    }

    // Note: this may not work with all cardDav servers, since according to the RFC:
    // "The filter component is not optional, but required."  Thus, may need to use the
    // PROPFIND query to get etags, then perform a filter with those etags.
    Q_UNUSED(contactEtags); // TODO
    QString requestStr = QStringLiteral(
        "<card:addressbook-query xmlns:d=\"DAV:\" xmlns:card=\"urn:ietf:params:xml:ns:carddav\">"
            "<d:prop>"
                "<d:getetag />"
                "<card:address-data />"
            "</d:prop>"
        "</card:addressbook-query>");

    return generateRequest(serverUrl, addressbookPath, QLatin1String("1"), QLatin1String("REPORT"), requestStr);
}

QNetworkReply *RequestGenerator::contactMultiget(const QString &serverUrl, const QString &addressbookPath, const QStringList &contactUris)
{
    if (Q_UNLIKELY(contactUris.isEmpty())) {
        qCWarning(lcCardDav) << Q_FUNC_INFO << "etag list empty, aborting";
        return 0;
    }

    if (Q_UNLIKELY(addressbookPath.isEmpty())) {
        qCWarning(lcCardDav) << Q_FUNC_INFO << "addressbook path empty, aborting";
        return 0;
    }

    if (Q_UNLIKELY(serverUrl.isEmpty())) {
        qCWarning(lcCardDav) << Q_FUNC_INFO << "server url empty, aborting";
        return 0;
    }

    QString uriHrefs;
    Q_FOREACH (const QString &uri, contactUris) {
        // note: uriHref is of form: <d:href>/addressbooks/johndoe/contacts/acme-12345.vcf</d:href> etc.
        QString href = uri.toHtmlEscaped();
        int lastPathMarker = href.lastIndexOf('/');
        if (lastPathMarker > 0) {
            // percent-encode the filename
            QString vcfName = QUrl::toPercentEncoding(href.mid(lastPathMarker + 1));
            href = href.mid(0, lastPathMarker+1) + vcfName;
        }
        if (uri.endsWith(QStringLiteral(".vcf")) && uri.startsWith(addressbookPath)) {
            uriHrefs.append(QStringLiteral("<d:href>%1</d:href>").arg(href));
        } else if (uri.startsWith(addressbookPath)) {
            // contact resource which doesn't end in .vcf but is otherwise well-formed / fully specified.
            uriHrefs.append(QStringLiteral("<d:href>%1</d:href>").arg(href));
        } else {
            uriHrefs.append(QStringLiteral("<d:href>%1/%2.vcf</d:href>").arg(addressbookPath).arg(href));
        }
    }

    QString requestStr = QStringLiteral(
        "<card:addressbook-multiget xmlns:d=\"DAV:\" xmlns:card=\"urn:ietf:params:xml:ns:carddav\">"
            "<d:prop>"
                "<d:getetag />"
                "<card:address-data />"
            "</d:prop>"
            "%1"
        "</card:addressbook-multiget>").arg(uriHrefs);

    return generateRequest(serverUrl, addressbookPath, QLatin1String("1"), QLatin1String("REPORT"), requestStr);
}

QNetworkReply *RequestGenerator::upsyncAddMod(const QString &serverUrl, const QString &contactPath, const QString &etag, const QString &vcard)
{
    if (Q_UNLIKELY(vcard.isEmpty())) {
        qCWarning(lcCardDav) << Q_FUNC_INFO << "vcard empty, aborting";
        return 0;
    }

    // the etag can be empty if it's an addition

    if (Q_UNLIKELY(contactPath.isEmpty())) {
        qCWarning(lcCardDav) << Q_FUNC_INFO << "contact uri empty, aborting";
        return 0;
    }

    if (Q_UNLIKELY(serverUrl.isEmpty())) {
        qCWarning(lcCardDav) << Q_FUNC_INFO << "server url empty, aborting";
        return 0;
    }

    return generateUpsyncRequest(serverUrl, contactPath, etag,
                                 QStringLiteral("text/vcard; charset=utf-8"),
                                 QStringLiteral("PUT"), vcard);
}

QNetworkReply *RequestGenerator::upsyncDeletion(const QString &serverUrl, const QString &contactPath, const QString &etag)
{
    if (Q_UNLIKELY(etag.isEmpty())) {
        qCWarning(lcCardDav) << Q_FUNC_INFO << "etag empty, aborting";
        return 0;
    }

    if (Q_UNLIKELY(contactPath.isEmpty())) {
        qCWarning(lcCardDav) << Q_FUNC_INFO << "contact uri empty, aborting";
        return 0;
    }

    if (Q_UNLIKELY(serverUrl.isEmpty())) {
        qCWarning(lcCardDav) << Q_FUNC_INFO << "server url empty, aborting";
        return 0;
    }

    return generateUpsyncRequest(serverUrl, contactPath, etag, QString(),
                                 QStringLiteral("DELETE"), QString());
}
