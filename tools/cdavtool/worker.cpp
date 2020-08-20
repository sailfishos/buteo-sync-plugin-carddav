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

#include "worker.h"

#include <qtcontacts-extensions.h>

#include <QtDebug>

namespace {
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

CDavToolWorker::CDavToolWorker(QObject *parent)
    : QObject(parent)
    , m_carddavSyncer(Q_NULLPTR)
    , m_carddavDiscovery(Q_NULLPTR)
    , m_caldavDiscovery(Q_NULLPTR)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_profileManager(new Buteo::ProfileManager)
    , m_accountManager(new Accounts::Manager(this))
    , m_account(Q_NULLPTR)
    , m_identity(Q_NULLPTR)
    , m_createMode(CDavToolWorker::CreateBoth)
    , m_operationMode(CDavToolWorker::CreateAccount)
    , m_errorOccurred(false)
    , m_verbose(false)
{
}

CDavToolWorker::~CDavToolWorker()
{
    delete m_profileManager;
}

void CDavToolWorker::createAccount(
        const QString &username,
        const QString &password,
        CDavToolWorker::CreateMode mode,
        const QString &hostAddress,
        const QString &calendarPath,
        const QString &addressbookPath)
{
    // cache some URL data we will need later.
    m_username = username;
    m_password = password;
    m_createMode = mode;
    m_hostAddress = hostAddress;
    m_calendarPath = calendarPath;
    m_addressbookPath = addressbookPath;

    // create an account
    m_account = m_accountManager->createAccount(QStringLiteral("onlinesync"));
    if (!m_account) {
        m_errorOccurred = true;
        emit done();
        return;
    }

    // find the CalDAV and CardDAV services
    Accounts::ServiceList services = m_account->services();
    Q_FOREACH (const Accounts::Service &s, services) {
        if (s.serviceType().toLower() == QStringLiteral("caldav")
                && (m_createMode == CDavToolWorker::CreateBoth || m_createMode == CDavToolWorker::CreateCalDAV)) {
            m_caldavService = s;
        } else if (s.serviceType().toLower() == QStringLiteral("carddav")
                   && (m_createMode == CDavToolWorker::CreateBoth || m_createMode == CDavToolWorker::CreateCardDAV)) {
            m_carddavService = s;
        }
    }

    // create a set of credentials for the account
    QMap<QString, QStringList> methodMechanisms;
    methodMechanisms.insert(QStringLiteral("password"), QStringList(QStringLiteral("password")));
    m_credentials = SignOn::IdentityInfo("jolla", username, methodMechanisms);
    m_credentials.setSecret(password, true);
    m_identity = SignOn::Identity::newIdentity(m_credentials);
    if (!m_identity) {
        m_account->remove();
        m_account->syncAndBlock();
        m_errorOccurred = true;
        emit done();
        return;
    }

    // store the credentials into an identity which will later be associated with the account.
    connect(m_identity, SIGNAL(error(SignOn::Error)),
            this, SLOT(handleError(SignOn::Error)), Qt::UniqueConnection);
    connect(m_identity, SIGNAL(credentialsStored(quint32)),
            this, SLOT(handleCredentialsStored(quint32)), Qt::UniqueConnection);
    printf("%s\n", "Storing account credentials...");
    m_identity->storeCredentials(m_credentials);
}


void CDavToolWorker::handleError(const SignOn::Error &err)
{
    printf("Error: %d: %s\n", (int)err.type(), err.message().toLatin1().constData());
    if (m_identity) {
        m_identity->signOut();
    }
    if (m_operationMode == CDavToolWorker::CreateAccount) {
        if (m_identity) {
            m_identity->remove();
        }
        if (m_account) {
            m_account->remove();
            m_account->syncAndBlock();
        }
    }
    m_errorOccurred = true;
    // the Identity operations are asynchronous... give them time to complete.
    QTimer::singleShot(1000, this, SIGNAL(done()));
}

void CDavToolWorker::handleCredentialsStored(quint32 credentialsId)
{
    if (m_identity->id() == 0) {
        handleError(SignOn::Error(SignOn::Error::Unknown, QStringLiteral("Identity has no id, but stored credentials: %1").arg(credentialsId)));
        return;
    } else {
        printf("%s%d\n", "Successfully stored credentials: ", credentialsId);
    }

    // associate the Identity with the Account, and set a variety of other required keys.
    const QString segCredKey = QStringLiteral("jolla/segregated_credentials/Jolla");
    const QString credKey = QStringLiteral("CredentialsId");
    const QString servAddrKey = QStringLiteral("server_address");
    const QString abookPath = QStringLiteral("addressbook_path");
    m_account->selectService(Accounts::Service());
    m_account->setValue(segCredKey, m_identity->id());
    m_account->setValue(credKey, m_identity->id());
    m_account->syncAndBlock();
    if (m_createMode == CDavToolWorker::CreateBoth || m_createMode == CDavToolWorker::CreateCardDAV) {
        m_account->selectService(m_carddavService);
        m_account->setValue(credKey, m_identity->id());
        m_account->setValue(servAddrKey, m_hostAddress);
        if (!m_addressbookPath.isEmpty()) {
            m_account->setValue(abookPath, m_addressbookPath);
        }
        m_account->syncAndBlock();
    }

    if (m_createMode == CDavToolWorker::CreateBoth || m_createMode == CDavToolWorker::CreateCalDAV) {
        m_account->selectService(m_caldavService);
        m_account->setValue(credKey, m_identity->id());
        m_account->setValue(servAddrKey, m_hostAddress);
        m_account->selectService(Accounts::Service());
        m_account->syncAndBlock();
        m_caldavDiscovery = new CalDAVDiscovery(m_caldavService.name(),
                                          m_username,
                                          m_password,
                                          m_account,
                                          m_accountManager,
                                          m_networkManager,
                                          this);
        connect(m_caldavDiscovery, &CalDAVDiscovery::error,
                this, &CDavToolWorker::discoveryError);
        connect(m_caldavDiscovery, &CalDAVDiscovery::success,
                this, &CDavToolWorker::accountDone);
        printf("%s\n", "Performing calendar discovery...");
        m_caldavDiscovery->setVerbose(m_verbose);
        m_caldavDiscovery->start(m_hostAddress, m_calendarPath);
    } else {
        accountDone();
    }
}

void CDavToolWorker::discoveryError()
{
    if (m_calendarPath.isEmpty()) {
        handleError(SignOn::Error(SignOn::Error::Unknown, QStringLiteral("Unable to discover CalDAV calendars!")));
    } else {
        // set the calendar path directly, assume that discovery is not possible.
        m_account->setValue(QStringLiteral("calendars"), QVariant::fromValue<QStringList>(QStringList(m_calendarPath)));
        m_account->setValue(QStringLiteral("enabled_calendars"), QVariant::fromValue<QStringList>(QStringList(m_calendarPath)));
        m_account->setValue(QStringLiteral("calendar_colors"), QVariant::fromValue<QStringList>(QStringList(QStringLiteral("#b90e28"))));
        m_account->syncAndBlock();
        accountDone();
    }
}

void CDavToolWorker::accountDone()
{
    // ok, the account is mostly set up, we just need to generate sync profiles and then enable it.
    printf("%s\n", "Generating sync profiles...");
    if (m_createMode == CDavToolWorker::CreateBoth || m_createMode == CDavToolWorker::CreateCardDAV) {
        if (!createSyncProfiles(m_account, m_carddavService)) {
            // error will be generated by that function.
            return;
        }
        m_account->selectService(Accounts::Service());
        m_account->setEnabled(true);
    }

    if (m_createMode == CDavToolWorker::CreateBoth || m_createMode == CDavToolWorker::CreateCalDAV) {
        if (!createSyncProfiles(m_account, m_caldavService)) {
            // error will be generated by that function.
            return;
        }
        m_account->selectService(Accounts::Service());
        m_account->setEnabled(true);
    }

    m_account->selectService(Accounts::Service());
    m_account->setEnabled(true);
    m_account->setDisplayName(QStringLiteral("cdavtool"));
    m_account->syncAndBlock();

    // success!
    printf("%s\n%d\n", "Successfully created account:", m_account->id());
    QTimer::singleShot(1000, this, SIGNAL(done()));
}

bool CDavToolWorker::createSyncProfiles(Accounts::Account *account,
                                        const Accounts::Service &service)
{
    account->selectService(service);
    QStringList templates = account->value(QStringLiteral("sync_profile_templates")).toStringList();
    Q_FOREACH (const QString &templateProfileName, templates) {
        Buteo::SyncProfile *templateProfile = m_profileManager->syncProfile(templateProfileName);
        if (!templateProfile) {
            handleError(SignOn::Error(SignOn::Error::Unknown, QStringLiteral("Unable to create template profile: %1").arg(templateProfileName)));
            return false;
        }

        Buteo::SyncProfile *perAccountProfile = templateProfile->clone();
        if (!perAccountProfile) {
            handleError(SignOn::Error(SignOn::Error::Unknown, QStringLiteral("Unable to create per-account profile: %1").arg(templateProfileName)));
            return false;
        }

        QString accountIdStr = QString::number(account->id());
        perAccountProfile->setName(templateProfileName + "-" + accountIdStr);
        perAccountProfile->setKey(Buteo::KEY_DISPLAY_NAME, templateProfileName + "-" + account->displayName().toHtmlEscaped());
        perAccountProfile->setKey(Buteo::KEY_ACCOUNT_ID, accountIdStr);
        perAccountProfile->setBoolKey(Buteo::KEY_USE_ACCOUNTS, true);
        perAccountProfile->setEnabled(true);
        QString profileName = m_profileManager->updateProfile(*perAccountProfile);
        if (profileName.isEmpty()) {
            profileName = perAccountProfile->name();
        }
        if (profileName.isEmpty()) {
            handleError(SignOn::Error(SignOn::Error::Unknown, QStringLiteral("Unable to store per-account profile: %1").arg(templateProfileName)));
        }

        account->setValue(QStringLiteral("%1/%2").arg(templateProfileName).arg(Buteo::KEY_PROFILE_ID), profileName);

        delete perAccountProfile;
        delete templateProfile;
    }

    return true;
}

void CDavToolWorker::deleteAccount(int accountId)
{
    Accounts::Account *account = Accounts::Account::fromId(m_accountManager, accountId, this);
    if (!account) {
        m_errorOccurred = true;
    } else {
        // remove associated credentials.
        account->selectService(Accounts::Service());
        QStringList configurationKeys = account->allKeys();
        foreach (const QString &key, configurationKeys) {
            if (key.contains(QStringLiteral("CredentialsId"))) {
                int identityId = account->valueAsInt(key, 0);
                if (identityId) {
                    SignOn::Identity *doomedIdentity = SignOn::Identity::existingIdentity(identityId, this);
                    if (doomedIdentity) {
                        doomedIdentity->signOut();
                        doomedIdentity->remove();
                    }
                }
            }
        }

        account->remove();
        account->syncAndBlock();
    }

    // the Identity operations are asynchronous... give them time to complete.
    QTimer::singleShot(1000, this, SIGNAL(done()));
}

void CDavToolWorker::clearRemoteCalendars(int accountId)
{
    m_operationMode = CDavToolWorker::ClearAllRemoteCalendars;
    m_account = Accounts::Account::fromId(m_accountManager, accountId, this);
    if (!m_account) {
        handleError(SignOn::Error(SignOn::Error::Unknown, QStringLiteral("No such account")));
        return;
    }

    // get username + password...
    m_identity = SignOn::Identity::existingIdentity(m_account->value(QStringLiteral("CredentialsId")).toInt(), this);
    m_session = m_identity->createSession(QStringLiteral("password"));
    connect(m_session, SIGNAL(response(SignOn::SessionData)),
            this, SLOT(gotCredentials(SignOn::SessionData)), Qt::UniqueConnection);
    connect(m_session, SIGNAL(error(SignOn::Error)),
            this, SLOT(handleError(SignOn::Error)), Qt::UniqueConnection);
    m_session->process(SignOn::SessionData(SignOn::SessionData()), QStringLiteral("password"));
}

void CDavToolWorker::clearRemoteAddressbooks(int accountId)
{
    m_operationMode = CDavToolWorker::ClearAllRemoteAddressbooks;
    m_account = Accounts::Account::fromId(m_accountManager, accountId, this);
    if (!m_account) {
        handleError(SignOn::Error(SignOn::Error::Unknown, QStringLiteral("No such account")));
        return;
    }

    // get username + password...
    m_identity = SignOn::Identity::existingIdentity(m_account->value(QStringLiteral("CredentialsId")).toInt(), this);
    m_session = m_identity->createSession(QStringLiteral("password"));
    connect(m_session, SIGNAL(response(SignOn::SessionData)),
            this, SLOT(gotCredentials(SignOn::SessionData)), Qt::UniqueConnection);
    connect(m_session, SIGNAL(error(SignOn::Error)),
            this, SLOT(handleError(SignOn::Error)), Qt::UniqueConnection);
    m_session->process(SignOn::SessionData(SignOn::SessionData()), QStringLiteral("password"));
}

void CDavToolWorker::gotCredentials(const SignOn::SessionData &response)
{
    m_username = response.toMap().value(QStringLiteral("UserName")).toString();
    m_password = response.toMap().value(QStringLiteral("Secret")).toString();

    Accounts::ServiceList services = m_account->services();
    Q_FOREACH (const Accounts::Service &s, services) {
        if (s.serviceType().toLower() == QStringLiteral("caldav")) {
            m_caldavService = s;
        } else if (s.serviceType().toLower() == QStringLiteral("carddav")) {
            m_carddavService = s;
        }
    }

    if (m_operationMode == CDavToolWorker::ClearAllRemoteCalendars) {
        if (m_caldavService.name().isEmpty()) {
            handleError(SignOn::Error(SignOn::Error::Unknown, QStringLiteral("No caldav service found!")));
            return;
        }

        m_account->selectService(m_caldavService);
        m_hostAddress = m_account->value(QStringLiteral("server_address")).toString();
        if (m_hostAddress.isEmpty()) {
            handleError(SignOn::Error(SignOn::Error::Unknown, QStringLiteral("No host address known!")));
            return;
        }

        const QStringList calendarPaths = m_account->value(QStringLiteral("calendars")).toStringList();
        gotCollectionsList(calendarPaths);
    } else if (m_operationMode == CDavToolWorker::ClearAllRemoteAddressbooks) {
        if (m_carddavService.name().isEmpty()) {
            handleError(SignOn::Error(SignOn::Error::Unknown, QStringLiteral("No carddav service found!")));
            return;
        }

        m_account->selectService(m_carddavService);
        m_hostAddress = m_account->value(QStringLiteral("server_address")).toString();
        if (m_hostAddress.isEmpty()) {
            handleError(SignOn::Error(SignOn::Error::Unknown, QStringLiteral("No host address known!")));
            return;
        }

        m_carddavSyncer = new Syncer(this, Q_NULLPTR, m_account->id());
        m_carddavDiscovery = new CardDav(m_carddavSyncer,
                                         m_hostAddress,
                                         m_addressbookPath,
                                         m_username,
                                         m_password);
        connect(m_carddavDiscovery, &CardDav::addressbooksList,
                this, [this] (const QList<ReplyParser::AddressBookInformation> &addressbooks) {
            QStringList paths;
            for (const ReplyParser::AddressBookInformation &ab : addressbooks) {
                paths.append(ab.url);
            }
            this->gotCollectionsList(paths);
        });
        connect(m_carddavDiscovery, &CardDav::error,
                this, &CDavToolWorker::handleCardDAVError);
        m_carddavDiscovery->determineAddressbooksList();
    }
}

void CDavToolWorker::handleCardDAVError(int code)
{
    handleError(SignOn::Error(SignOn::Error::Unknown, QStringLiteral("Error while retrieving addressbook list: %1").arg(code)));
}

void CDavToolWorker::gotCollectionsList(const QStringList &paths)
{
    Q_FOREACH (const QString &cpath, paths) {
        QString requestStr = QStringLiteral(
            "<d:propfind xmlns:d=\"DAV:\">"
              "<d:prop>"
                 "<d:getetag />"
              "</d:prop>"
            "</d:propfind>");

        QNetworkReply *getEtagsReply = generateRequest(m_hostAddress, cpath, QLatin1String("1"), QLatin1String("PROPFIND"), requestStr);
        connect(getEtagsReply, &QNetworkReply::finished, this, &CDavToolWorker::gotEtags);
        m_replies.append(getEtagsReply);
    }

    if (!m_replies.size()) {
        handleError(SignOn::Error(SignOn::Error::Unknown, QStringLiteral("No collections to clear!")));
    }
}

void CDavToolWorker::gotEtags()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (reply->error() != QNetworkReply::NoError) {
        handleError(SignOn::Error(SignOn::Error::Unknown, QStringLiteral("Error occurred when fetching etags: %1: %2").arg(reply->error()).arg(reply->errorString())));
        return;
    }

    m_replies.removeOne(reply);
    QXmlStreamReader reader(reply->readAll());
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

    Q_FOREACH (const QVariant &rv, responses) {
        QVariantMap rmap = rv.toMap();
        QString href = rmap.value("href").toMap().value("@text").toString();
        QUrl uri = QUrl::fromPercentEncoding(href.toUtf8());
        QString etag = rmap.value("propstat").toMap().value("prop").toMap().value("getetag").toMap().value("@text").toString();
        QString status = rmap.value("propstat").toMap().value("status").toMap().value("@text").toString();
        if (status.isEmpty()) {
            status = rmap.value("status").toMap().value("@text").toString();
        }
        if (!href.endsWith(QStringLiteral(".vcf"), Qt::CaseInsensitive)
                && !href.endsWith(QStringLiteral(".vcs"))
                && !href.endsWith(QStringLiteral(".ics"))) {
            // this is probably a response for a collection resource,
            // rather than for a contact or event resource within the collection.
            qWarning() << "ignoring probable collection resource:" << uri.toString() << etag << status;
            continue;
        } else {
            qWarning() << "DELETING:" << m_hostAddress << href << etag;
            QNetworkReply *deletionRequest = generateUpsyncRequest(m_hostAddress, href, etag, QString(),
                                                                   QStringLiteral("DELETE"), QString());
            connect(deletionRequest, &QNetworkReply::finished, this, &CDavToolWorker::finishedDeletion);
            m_replies.append(deletionRequest);
        }
    }

    if (m_replies.isEmpty()) {
        // collections are already empty.
        qWarning() << "All collections are empty?";
        emit done();
    }
}

void CDavToolWorker::finishedDeletion()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (reply->error() != QNetworkReply::NoError) {
        handleError(SignOn::Error(SignOn::Error::Unknown, QStringLiteral("Error occurred when deleting event/contact: %1: %2").arg(reply->error()).arg(reply->errorString())));
        return;
    }

    m_replies.removeOne(reply);
    if (m_replies.isEmpty()) {
        // this last deletion is complete!
        emit done();
    }
}


QNetworkReply *CDavToolWorker::generateRequest(const QString &url,
                                               const QString &path,
                                               const QString &depth,
                                               const QString &requestType,
                                               const QString &request) const
{
    QByteArray requestData(request.toUtf8());
    QUrl reqUrl(url);
    if (!path.isEmpty()) {
        // override the path from the given url with the path argument.
        // this is because the initial URL may be a user-principals URL
        // but subsequent paths are not relative to that one, but instead
        // are relative to the root path /
        if (path.startsWith('/')) {
            reqUrl.setPath(path);
        } else {
            reqUrl.setPath('/' + path);
        }
    }
    if (!m_username.isEmpty() && !m_password.isEmpty()) {
        reqUrl.setUserName(m_username);
        reqUrl.setPassword(m_password);
    }

    QNetworkRequest req(reqUrl);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  "application/xml; charset=utf-8");
    req.setHeader(QNetworkRequest::ContentLengthHeader,
                  requestData.length());
    if (!depth.isEmpty()) {
        req.setRawHeader("Depth", depth.toUtf8());
    }

    QBuffer *requestDataBuffer = new QBuffer(parent());
    requestDataBuffer->setData(requestData);
    qWarning() << "generateRequest():"
               << reqUrl << depth << requestType
               << QString::fromUtf8(requestData);
    return m_networkManager->sendCustomRequest(req, requestType.toLatin1(), requestDataBuffer);
}

QNetworkReply *CDavToolWorker::generateUpsyncRequest(const QString &url,
                                                     const QString &path,
                                                     const QString &ifMatch,
                                                     const QString &contentType,
                                                     const QString &requestType,
                                                     const QString &request) const
{
    QByteArray requestData(request.toUtf8());
    QUrl reqUrl(url);
    if (!path.isEmpty()) {
        // override the path from the given url with the path argument.
        // this is because the initial URL may be a user-principals URL
        // but subsequent paths are not relative to that one, but instead
        // are relative to the root path /
        reqUrl.setPath(path);
    }
    if (!m_username.isEmpty() && !m_password.isEmpty()) {
        reqUrl.setUserName(m_username);
        reqUrl.setPassword(m_password);
    }

    QNetworkRequest req(reqUrl);
    if (!contentType.isEmpty()) {
        req.setHeader(QNetworkRequest::ContentTypeHeader,
                      contentType);
    }
    if (!request.isEmpty()) {
        req.setHeader(QNetworkRequest::ContentLengthHeader,
                      requestData.length());
    }
    if (!ifMatch.isEmpty()) {
        req.setRawHeader("If-Match", ifMatch.toUtf8());
    }

    qWarning() << "generateUpsyncRequest():" << reqUrl << ":" << requestData.length() << "bytes";
    Q_FOREACH (const QByteArray &headerName, req.rawHeaderList()) {
        qWarning() << "   " << headerName << "=" << req.rawHeader(headerName);
    }

    if (!request.isEmpty()) {
        QBuffer *requestDataBuffer = new QBuffer(parent());
        requestDataBuffer->setData(requestData);
        return m_networkManager->sendCustomRequest(req, requestType.toLatin1(), requestDataBuffer);
    }

    return m_networkManager->sendCustomRequest(req, requestType.toLatin1());
}
