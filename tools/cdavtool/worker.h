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

#ifndef CDAVTOOL_WORKER_H
#define CDAVTOOL_WORKER_H

#include "helpers.h"

#include "syncer_p.h"
#include "carddav_p.h"

#include <QNetworkAccessManager>
#include <QTimer>
#include <QObject>
#include <QString>

// accounts&sso
#include <Accounts/Manager>
#include <Accounts/Account>
#include <Accounts/Service>
#include <SignOn/AuthSession>
#include <SignOn/SessionData>
#include <SignOn/Identity>
#include <SignOn/IdentityInfo>
#include <SignOn/Error>

// buteo-syncfw
#include <ProfileEngineDefs.h>
#include <SyncProfile.h>
#include <SyncCommonDefs.h>
#include <ProfileManager.h>
#include <buteosyncfw5/SyncClientInterface.h>

class CDavToolWorker : public QObject
{
    Q_OBJECT

public:
    enum CreateMode {
        CreateBoth = 0,
        CreateCardDAV,
        CreateCalDAV
    };

    enum OperationMode {
        CreateAccount = 0,
        DeleteAccount,
        ClearAllRemoteCalendars,
        ClearAllRemoteAddressbooks
    };

    CDavToolWorker(QObject *parent = Q_NULLPTR);
    ~CDavToolWorker();

    void setVerbose(bool verbose) { m_verbose = verbose; }

    void createAccount(const QString &username, const QString &password,
                       CreateMode mode, const QString &hostAddress,
                       const QString &calendarPath = QString(),
                       const QString &addressbookPath = QString());
    void deleteAccount(int accountId);
    void clearRemoteCalendars(int accountId);
    void clearRemoteAddressbooks(int accountId);

    bool errorOccurred() const { return m_errorOccurred; }

Q_SIGNALS:
    void done();

private:
    bool createSyncProfiles(Accounts::Account *account,
                            const Accounts::Service &service);
    QNetworkReply *generateRequest(const QString &url,
                                   const QString &path,
                                   const QString &depth,
                                   const QString &requestType,
                                   const QString &request) const;
    QNetworkReply *generateUpsyncRequest(const QString &url,
                                         const QString &path,
                                         const QString &ifMatch,
                                         const QString &contentType,
                                         const QString &requestType,
                                         const QString &request) const;

private Q_SLOTS:
    void handleCredentialsStored(quint32);
    void accountDone();
    void discoveryError();
    void handleError(const SignOn::Error &err);
    void handleCardDAVError(int code);

    void gotCredentials(const SignOn::SessionData &response);
    void gotCollectionsList(const QStringList &paths);
    void gotEtags();
    void finishedDeletion();

private:
    Syncer *m_carddavSyncer;
    CardDav *m_carddavDiscovery;
    CalDAVDiscovery *m_caldavDiscovery;
    QNetworkAccessManager *m_networkManager;
    Buteo::ProfileManager *m_profileManager;
    Accounts::Manager *m_accountManager;
    Accounts::Account *m_account;
    SignOn::AuthSession *m_session;
    SignOn::Identity *m_identity;
    SignOn::IdentityInfo m_credentials;
    Accounts::Service m_caldavService;
    Accounts::Service m_carddavService;
    QString m_username;
    QString m_password;
    QString m_hostAddress;
    QString m_calendarPath;
    QString m_addressbookPath;
    CreateMode m_createMode;
    OperationMode m_operationMode;
    bool m_errorOccurred;
    bool m_verbose;
    QList<QNetworkReply *> m_replies;
};

#endif // CDAVTOOL_WORKER_H
