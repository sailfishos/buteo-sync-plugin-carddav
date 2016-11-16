/*
 * Copyright (C) 2016 Jolla Ltd.
 * Contact: Chris Adams <chris.adams@jollamobile.com>
 *
 * You may use this file under the terms of the BSD license as follows:
 *
 * "Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Nemo Mobile nor the names of its contributors
 *     may be used to endorse or promote products derived from this
 *     software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
 */

#include "helpers.h"

static const QString ServiceSettingCalendars = QStringLiteral("calendars");
static const QString ServiceSettingEnabledCalendars = QStringLiteral("enabled_calendars");
static const QString ServiceSettingCalendarDisplayNames = QStringLiteral("calendar_display_names");
static const QString ServiceSettingCalendarColors = QStringLiteral("calendar_colors");

static const QByteArray PropFindRequest = "PROPFIND";

static const QString XmlElementResponse = QStringLiteral("response");
static const QString XmlElementHref = QStringLiteral("href");
static const QString XmlElementComp = QStringLiteral("comp");
static const QString XmlElementResourceType = QStringLiteral("resourcetype");
static const QString XmlElementCalendar = QStringLiteral("calendar");
static const QString XmlElementPrincipal = QStringLiteral("principal");
static const QString XmlElementCalendarColor = QStringLiteral("calendar-color");
static const QString XmlElementDisplayName = QStringLiteral("displayname");

static void DebugRequest(const QNetworkRequest &request, const QByteArray &data = QByteArray())
{
    qDebug() << "------------------- Dumping request data:";
    const QList<QByteArray>& rawHeaderList(request.rawHeaderList());
    Q_FOREACH (const QByteArray &rawHeader, rawHeaderList) {
        qDebug() << rawHeader << " : " << request.rawHeader(rawHeader);
    }
    qDebug() << "URL = " << request.url();
    qDebug() << "Request:";
    QString allData = QString::fromUtf8(data);
    Q_FOREACH (const QString &line, allData.split('\n')) {
        qDebug() << line;
    }
    qDebug() << "---------------------------------------------------------------------\n";
}

static void DebugReply(const QNetworkReply &reply, const QByteArray &data = QByteArray())
{
    qDebug() << "------------------- Dumping reply data:";
    qDebug() << "response status code:" << reply.attribute(QNetworkRequest::HttpStatusCodeAttribute);
    QList<QNetworkReply::RawHeaderPair> headers = reply.rawHeaderPairs();
    qDebug() << "response headers:";
    for (int i=0; i<headers.count(); i++) {
        qDebug() << "\t" << headers[i].first << ":" << headers[i].second;
    }
    if (!data.isEmpty()) {
        qDebug() << "Response data:";
        QString allData = QString::fromUtf8(data);
        Q_FOREACH (const QString &line, allData.split('\n')) {
            qDebug() << line;
        }
    }
    qDebug() << "---------------------------------------------------------------------\n";
}

static void dumpXml(const QByteArray &xml)
{
    // this algorithm doesn't handle a lot of stuff (escaped slashes/angle-brackets, slashes/angle-brackets in text, etc) but it works:
    // see < then read until > and that becomes "tag".  Print indent, print tag, print newline.  If tag didn't contain / then indent += "    " else deindent.
    // see anything else, then read until < and that becomes "text".  Print indent, print text, print newline.
    QString indent;
    QString formatted;
    QString allData = QString::fromUtf8(xml);
    for (QString::const_iterator it = allData.constBegin(); it != allData.constEnd(); ) {
        QString text;
        QString tag;
        bool withinString = false, seenSlash = false, needDeindent = false;
        if (*it == QChar('\n') || *it == QChar('\r')) {
            it++;
            continue;
        }
        if (*it == QChar('<')) {
            while (it != allData.constEnd() && *it != QChar('>')) {
                tag += *it;
                if (*it == '\"') {
                    withinString = !withinString;
                }
                if (*it == '/' && !withinString) {
                    seenSlash = true;
                    if (tag == QStringLiteral("</")) {
                        needDeindent = true;
                    }
                }
                it++;
            }
            if (it != allData.constEnd()) {
                tag += *it;
                it++;
            }
            if (needDeindent && indent.size() >= 4) indent.chop(4);
            formatted += indent + tag + '\n';
            if (!seenSlash) indent += "    ";
        } else {
            while (it != allData.constEnd() && *it != QChar('<')) {
                text += *it;
                it++;
            }
            formatted += indent + text + '\n';
        }
    }

    qDebug() << "------------------- Dumping XML data:";
    Q_FOREACH (const QString &line, formatted.split('\n')) {
        qDebug() << line;
    }
    qDebug() << "---------------------------------------------------------------------\n";
}

CalDAVDiscovery::CalDAVDiscovery(const QString &serviceName,
                                 const QString &username,
                                 const QString &password,
                                 Accounts::Account *account,
                                 Accounts::Manager *accountManager,
                                 QNetworkAccessManager *networkManager,
                                 QObject *parent)
    : QObject(parent)
    , m_account(account)
    , m_accountManager(accountManager)
    , m_networkAccessManager(networkManager)
    , m_status(UnknownStatus)
    , m_serviceName(serviceName)
    , m_username(username)
    , m_password(password)
    , m_verbose(false)
{
}

CalDAVDiscovery::~CalDAVDiscovery()
{
}

void CalDAVDiscovery::start(const QString &serverAddress, const QString &calendarHomePath)
{
    if (m_status != UnknownStatus) {
        qWarning() << "Already started!";
        emitError(InternalError);
        return;
    }
    if (!m_account || m_serviceName.isEmpty()) {
        qWarning() << "account or service not provided!";
        emitError(InternalError);
        return;
    }
    m_serverAddress = serverAddress.endsWith('/')
                       ? serverAddress.mid(0, serverAddress.length()-1)
                       : serverAddress;
    // path must start and end with '/'
    m_calendarHomePath = calendarHomePath;
    if (!m_calendarHomePath.isEmpty()) {
        if (!m_calendarHomePath.startsWith('/'))
            m_calendarHomePath = '/' + m_calendarHomePath;
        if (!m_calendarHomePath.endsWith('/'))
            m_calendarHomePath += '/';
    }
    QUrl testUrl(m_serverAddress);
    testUrl.setPath(m_calendarHomePath);
    if (!testUrl.isValid()) {
        qWarning() << "Supplied server address + path produced bad URL. serverAddress ="
                   << serverAddress << "serverPath = " << calendarHomePath;
        emitError(InvalidUrlError);
        return;
    }

    startRequests();
}

void CalDAVDiscovery::writeCalendars(Accounts::Account *account, const Accounts::Service &srv, const QList<OnlineCalendar> &calendars)
{
    if (!account || !srv.isValid()) {
        qWarning() << "account is null or service is invalid";
        return;
    }
    QStringList serverPaths;
    QStringList enabled;
    QStringList displayNames;
    QStringList colors;
    for (int i=0; i<calendars.count(); i++) {
        const OnlineCalendar &calendar = calendars[i];
        serverPaths << calendar.serverPath;
        if (calendar.enabled) {
            enabled << calendar.serverPath;
        }
        displayNames << calendar.displayName;
        colors << calendar.color;
    }
    account->selectService(srv);
    account->setEnabled(true);
    account->setValue(ServiceSettingCalendars, serverPaths);
    account->setValue(ServiceSettingEnabledCalendars, enabled);
    account->setValue(ServiceSettingCalendarDisplayNames, displayNames);
    account->setValue(ServiceSettingCalendarColors, colors);
    account->selectService(Accounts::Service());
}

QNetworkRequest CalDAVDiscovery::templateRequest(const QString &destUrlString) const
{
    QUrl url;
    if (destUrlString.isEmpty()) {
        url.setUrl(m_serverAddress);
    } else {
        if (destUrlString.startsWith('/')) {
            // this is a path, so use the default server address
            url.setUrl(m_serverAddress);
            url.setPath(destUrlString);
            if (!url.isValid()) {
                qWarning() << "Cannot read URL:" << url << "with address:" << m_serverAddress
                           << "and path:" << destUrlString;
                return QNetworkRequest();
            }
        } else {
            url.setUrl(destUrlString);
            if (!url.isValid()) {
                qWarning() << "Cannot read URL:" << destUrlString;
                return QNetworkRequest();
            }
        }
    }
    QNetworkRequest req;
    url.setUserName(m_username);
    url.setPassword(m_password);
    req.setUrl(url);
    req.setRawHeader("Prefer", "return-minimal");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/xml; charset=utf-8");
    return req;
}

void CalDAVDiscovery::startRequests()
{
    if (m_calendarHomePath.isEmpty()) {
        qDebug() << "calendar home path is empty, requesting user principal url";
        requestUserPrincipalUrl(QString());
    } else {
        qDebug() << "calendar home path given, requesting calendar list from:" << m_calendarHomePath;
        requestCalendarList(m_calendarHomePath);
    }
}

void CalDAVDiscovery::requestUserPrincipalUrl(const QString &discoveryPath)
{
    QNetworkRequest request = templateRequest(discoveryPath);
    request.setRawHeader("Depth", "0");

    QBuffer *buffer = new QBuffer(this);
    buffer->setData("<d:propfind xmlns:d=\"DAV:\">" \
                        "<d:prop>" \
                            "<d:current-user-principal />" \
                        "</d:prop>" \
                    "</d:propfind>");
    if (m_verbose) {
        DebugRequest(request, buffer->data());
    }
    QNetworkReply *reply = m_networkAccessManager->sendCustomRequest(request, PropFindRequest, buffer);
    reply->setProperty("discoveryPath", discoveryPath);
    connect(reply, SIGNAL(finished()),
            this, SLOT(requestUserPrincipalUrlFinished()));
    connect(reply, SIGNAL(sslErrors(QList<QSslError>)),
            this, SLOT(handleSslErrors(QList<QSslError>)));
    m_pendingReplies.insert(reply, buffer);

    setStatus(RequestingUserPrincipalUrl);
}

void CalDAVDiscovery::requestUserPrincipalUrlFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    QByteArray replyData = reply->readAll();
    QString discoveryPath = reply->property("discoveryPath").toString();

    if (reply->error() != QNetworkReply::NoError) {
        // perform discovery as per RFC 6764
        if (discoveryPath.isEmpty()) {
            // try the well-known path instead.
            requestUserPrincipalUrl(QStringLiteral("/.well-known/caldav"));
        } else if (discoveryPath == QStringLiteral("/.well-known/caldav")) {
            // try the root URI instead.
            requestUserPrincipalUrl(QStringLiteral("/"));
        } else {
            // abort.
            if (m_verbose) {
                DebugReply(*reply, replyData);
            }
            emitNetworkReplyError(*reply);
        }
    } else {
        // handle redirects if required as per RFC 6764
        QUrl redirectUrl(reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
        if (!redirectUrl.isEmpty()) {
            QUrl originalUrl(m_serverAddress);
            if (!discoveryPath.isEmpty()) {
                originalUrl.setPath(discoveryPath);
            }

            if (originalUrl.path().endsWith(QStringLiteral(".well-known/caldav"))) {
                qDebug() << "being redirected from" << originalUrl << "to" << redirectUrl;
                requestUserPrincipalUrl(redirectUrl.toString());
            } else {
                qWarning() << "ignoring possibly malicious redirect from" << originalUrl << "to" << redirectUrl;
                emitError(CurrentUserPrincipalNotFoundError);
            }
        } else {
            QXmlStreamReader reader(replyData);
            reader.setNamespaceProcessing(true);
            QString userPrincipalPath;
            while (!reader.atEnd() && reader.name() != "current-user-principal") {
                reader.readNext();
            }
            while (reader.readNextStartElement()) {
                if (reader.name() == XmlElementHref) {
                    userPrincipalPath = reader.readElementText();
                    break;
                }
            }
            if (reader.hasError()) {
                qWarning() << QString("XML parse error: %1: %2").arg(reader.error()).arg(reader.errorString());
                if (m_verbose) {
                    DebugReply(*reply, replyData);
                }
                emitError(InvalidServerResponseError);
            } else if (userPrincipalPath.isEmpty()) {
                qWarning() << "Request for user calendar path failed, response is missing current-user-principal href";
                if (m_verbose) {
                    dumpXml(replyData);
                }
                emitError(CurrentUserPrincipalNotFoundError);
            } else {
                if (m_verbose) {
                    dumpXml(replyData);
                }
                m_userPrincipalPaths.insert(userPrincipalPath);
                requestCalendarHomeUrl(userPrincipalPath);
            }
        }
    }
    QIODevice *device = m_pendingReplies.take(reply);
    if (device) {
        device->deleteLater();
    }
    reply->deleteLater();
}

void CalDAVDiscovery::requestCalendarHomeUrl(const QString &userPrincipalPath)
{
    QNetworkRequest request = templateRequest(userPrincipalPath);
    request.setRawHeader("Depth", "0");

    QBuffer *buffer = new QBuffer(this);
    buffer->setData("<d:propfind xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">" \
                        "<d:prop>" \
                            "<c:calendar-home-set />" \
                        "</d:prop>" \
                    "</d:propfind>");
    if (m_verbose) {
        DebugRequest(request, buffer->data());
    }
    QNetworkReply *reply = m_networkAccessManager->sendCustomRequest(request, PropFindRequest, buffer);
    connect(reply, SIGNAL(finished()),
            this, SLOT(requestCalendarHomeUrlFinished()));
    connect(reply, SIGNAL(sslErrors(QList<QSslError>)),
            this, SLOT(handleSslErrors(QList<QSslError>)));
    m_pendingReplies.insert(reply, buffer);

    setStatus(RequestingCalendarHomeUrl);
}

void CalDAVDiscovery::requestCalendarHomeUrlFinished()
{
   QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
   QByteArray replyData = reply->readAll();

   if (reply->error() != QNetworkReply::NoError) {
       if (m_verbose) {
           DebugReply(*reply, replyData);
       }
       emitNetworkReplyError(*reply);
   } else {
       QXmlStreamReader reader(replyData);
       reader.setNamespaceProcessing(true);
       QString calendarHome;
       while (!reader.atEnd() && reader.name() != "calendar-home-set") {
           reader.readNext();
       }
       while (reader.readNextStartElement()) {
           if (reader.name() == XmlElementHref) {
               calendarHome = reader.readElementText();
               break;
           }
       }
       if (reader.hasError()) {
           qWarning() << QString("XML parse error: %1: %2").arg(reader.error()).arg(reader.errorString());
           if (m_verbose) {
               DebugReply(*reply, replyData);
           }
           emitError(InvalidServerResponseError);
       } else if (calendarHome.isEmpty()) {
           qWarning() << "Request for user calendar home failed, response is missing calendar-home-set href";
           if (m_verbose) {
               dumpXml(replyData);
           }
           emitError(CalendarHomeNotFoundError);
       } else {
           if (m_verbose) {
               dumpXml(replyData);
           }
           requestCalendarList(calendarHome);
       }
   }
   QIODevice *device = m_pendingReplies.take(reply);
   if (device) {
       device->deleteLater();
   }
   reply->deleteLater();
}

void CalDAVDiscovery::requestCalendarList(const QString &calendarHomePath)
{
    QNetworkRequest request = templateRequest(calendarHomePath);
    request.setRawHeader("Depth", "1");

    QBuffer *buffer = new QBuffer(this);
    buffer->setData("<d:propfind xmlns:d=\"DAV:\" xmlns:cs=\"http://calendarserver.org/ns/\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\" xmlns:ca=\"http://apple.com/ns/ical/\">" \
                        "<d:prop>" \
                            "<d:resourcetype />" \
                            "<d:current-user-principal />" \
                            "<d:displayname />" \
                            "<cs:getctag />" \
                            "<ca:calendar-color />" \
                        "</d:prop>" \
                    "</d:propfind>");
    if (m_verbose) {
        DebugRequest(request, buffer->data());
    }
    QNetworkReply *reply = m_networkAccessManager->sendCustomRequest(request, PropFindRequest, buffer);
    connect(reply, SIGNAL(finished()),
            this, SLOT(requestCalendarListFinished()));
    connect(reply, SIGNAL(sslErrors(QList<QSslError>)),
            this, SLOT(handleSslErrors(QList<QSslError>)));
    m_pendingReplies.insert(reply, buffer);

    setStatus(RequestingCalendarListing);
}

void CalDAVDiscovery::requestCalendarListFinished()
{
   QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
   QByteArray replyData = reply->readAll();

   if (reply->error() != QNetworkReply::NoError) {
       if (m_verbose) {
           DebugReply(*reply, replyData);
       }
       emitNetworkReplyError(*reply);
   } else {
       // if the server returns the user principal path instead of the calendar set
       // we may need to then request the calendar home set url from there.
       QString userPrincipalPath;
       bool foundCalendars = false;
       QXmlStreamReader reader(replyData);
       reader.setNamespaceProcessing(true);
       while (!reader.atEnd()) {
           reader.readNext();
           if (reader.name() == XmlElementResponse && reader.isStartElement()) {
               bool responseContainsCalendar = addNextCalendar(&reader, &userPrincipalPath);
               foundCalendars |= responseContainsCalendar;
           }
       }
       if (reader.hasError()) {
           qWarning() << QString("XML parse error: %1: %2").arg(reader.error()).arg(reader.errorString());
           if (m_verbose) {
               DebugReply(*reply, replyData);
           }
           m_userPrincipalPaths.clear(); // reset state
           emitError(InvalidServerResponseError);
       } else if (!foundCalendars && !userPrincipalPath.isEmpty()) {
           if (!m_userPrincipalPaths.contains(userPrincipalPath)) {
               qDebug() << "calendar list response returned (different) user principal; performing calendar home url request.";
               if (m_verbose) {
                   dumpXml(replyData);
               }
               m_userPrincipalPaths.insert(userPrincipalPath);
               requestCalendarHomeUrl(userPrincipalPath);
           } else {
               qDebug() << "calendar list response is returning (identical) user principal; aborting";
               if (m_verbose) {
                   dumpXml(replyData);
               }
               m_userPrincipalPaths.clear();
               emitError(InvalidServerResponseError);
           }
       } else {
           // write the calendars to the service settings and sync the changes
           if (m_verbose) {
               dumpXml(replyData);
           }
           m_userPrincipalPaths.clear(); // reset state
           Accounts::Service srv = m_accountManager->service(m_serviceName);
           writeCalendars(m_account, srv, m_calendars);
           m_account->syncAndBlock();
           setStatus(Finished);
       }
   }
   QIODevice *device = m_pendingReplies.take(reply);
   if (device) {
       device->deleteLater();
   }
   reply->deleteLater();
}

bool CalDAVDiscovery::addNextCalendar(QXmlStreamReader *reader, QString *parsedUserPrincipalPath)
{
    QString calendarPath;
    bool isCalendar = false, isPrincipal = false;
    QString calendarDisplayName;
    QString colorCode;

    if (!(reader->name() == XmlElementResponse && reader->isStartElement())) {
        qWarning() << "Parse error: expected to be reading a <response>";
        return false;
    }
    while (!reader->atEnd() && !(reader->name() == XmlElementResponse && reader->isEndElement())) {
        reader->readNext();
        if (reader->name() == XmlElementHref && calendarPath.isEmpty()) {
            calendarPath = reader->readElementText();
        } else if (reader->name() == XmlElementResourceType) {
            reader->readNext();
            while (!reader->atEnd() && reader->name() != XmlElementResourceType) {
                reader->readNext();
                if (reader->name() == XmlElementCalendar) {
                    isCalendar = true;
                } else if (reader->name() == XmlElementPrincipal) {
                    *parsedUserPrincipalPath = calendarPath;
                    isPrincipal = true;
                }
            }
        } else if (reader->name() == XmlElementDisplayName) {
            calendarDisplayName = reader->readElementText();
        } else if (reader->name() == XmlElementCalendarColor) {
            QString colorName = reader->readElementText();
            if (colorName.length() == 9 && colorName.startsWith(QStringLiteral("#"))) {
                // color is in "#RRGGBBAA" format
                colorName = colorName.mid(0, 7);
            }
            colorCode = colorName;
        }
    }
    if (isCalendar) {
        OnlineCalendar calendar;
        QString removeCalendarPathSuffix;
        if (m_serverAddress.contains(QStringLiteral("memotoo.com"))) {
            removeCalendarPathSuffix = QStringLiteral("category0/");
        }
        QString fixedCalendarPath = calendarPath;
        if (!removeCalendarPathSuffix.isEmpty() && calendarPath.endsWith(removeCalendarPathSuffix)) {
            // some providers (e.g. Memotoo) need special treatment...
            fixedCalendarPath.chop(removeCalendarPathSuffix.size());
        }
        calendar.serverPath = fixedCalendarPath;
        calendar.enabled = true;
        calendar.displayName = calendarDisplayName;
        calendar.color = colorCode.isEmpty() ? "#800000" : colorCode;
        m_calendars << calendar;
        qDebug() << "found calendar information in response:" << calendarPath << calendarDisplayName << colorCode;
        return true;
    }

    if (isPrincipal) {
        qDebug() << "found user principal path in response:" << *parsedUserPrincipalPath;
    } else {
        qDebug() << "Unable to parse calendar from response, have details:" << calendarPath << calendarDisplayName << colorCode;
    }
    return false;
}

void CalDAVDiscovery::handleSslErrors(const QList<QSslError> &errors)
{
    // TODO add configuration for SSL handling
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (reply) {
        reply->ignoreSslErrors(errors);
    }
}

void CalDAVDiscovery::emitNetworkReplyError(const QNetworkReply &reply)
{
    qWarning() << QString("QNetworkReply error: %1: %2").arg(reply.error()).arg(reply.errorString());

    switch (reply.error()) {
    case QNetworkReply::AuthenticationRequiredError:
        emitError(SignInError);
        break;
    case QNetworkReply::ContentNotFoundError:
        emitError(ContentNotFoundError);
        break;
    default:
        emitError(NetworkRequestFailedError);
        break;
    }
}

void CalDAVDiscovery::emitError(Error errorCode)
{
    switch (errorCode) {
    case NoError:
        return;
    case InvalidUrlError:
        qWarning() << "The server address or path is incorrect.";
        break;
    case SignInError:
        qWarning() << "The username or password is incorrect.";
        break;
    case NetworkRequestFailedError:
        qWarning() << "The network request was unsuccessful.";
        break;
    case ContentNotFoundError:
        // We may get this error if an incorrect username means that we made a request with an invalid server URL
        qWarning() << "The server request was unsuccessful. Make sure the username is correct.";
        break;
    case ServiceUnavailableError:
        // Some servers respond with this if the server path is wrong
        qWarning() << "The server request was unsuccessful. Make sure the server path is correct.";
        break;
    case InvalidServerResponseError:
        qWarning() << "The server response could not be processed.";
        break;
    case CurrentUserPrincipalNotFoundError:
        qWarning() << "The server response did not provide the user details for the specified username.";
        break;
    case CalendarHomeNotFoundError:
        qWarning() << "The server response did not provide the calendar home location for the specified username.";
        break;
    default:
        qWarning() << "An error has occurred.";
        break;
    }
    emit error();
}

void CalDAVDiscovery::setStatus(Status status)
{
    if (status != m_status) {
        m_status = status;
        if (status == CalDAVDiscovery::Finished) {
            emit success();
        }
    }
}
