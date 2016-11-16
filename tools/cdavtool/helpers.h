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

#ifndef CDAVTOOL_HELPERS_H
#define CDAVTOOL_HELPERS_H

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslError>

#include <QXmlStreamReader>

#include <QIODevice>
#include <QBuffer>

#include <QVariantMap>
#include <QVariantList>
#include <QList>
#include <QHash>

#include <QUrl>
#include <QString>
#include <QObject>

#include <Accounts/Manager>
#include <Accounts/AccountService>
#include <Accounts/Account>

class OnlineCalendar
{
public:
    OnlineCalendar() : enabled(false) {}
    ~OnlineCalendar() {}

    OnlineCalendar(const OnlineCalendar &other) { operator=(other); }
    OnlineCalendar &operator=(const OnlineCalendar &other) {
        serverPath = other.serverPath;
        displayName = other.displayName;
        color = other.color;
        enabled = other.enabled;
        return *this;
    }

    bool operator==(const OnlineCalendar &other) const {
        if (other == *this) {
            return true;
        }
        return other.serverPath == serverPath;
    }

    QString serverPath;
    QString displayName;
    QString color;
    bool enabled;
};

class CalDAVDiscovery : public QObject
{
    Q_OBJECT
    Q_ENUMS(Error)

public:
    enum Status {
        UnknownStatus,
        SigningIn,
        RequestingUserPrincipalUrl,
        RequestingCalendarHomeUrl,
        RequestingCalendarListing,
        Finalizing,
        Finished
    };

    enum Error {
        NoError,
        InternalError,
        InvalidUrlError,
        SignInError,
        NetworkRequestFailedError,
        ContentNotFoundError,
        ServiceUnavailableError,
        InvalidServerResponseError,
        CurrentUserPrincipalNotFoundError,
        CalendarHomeNotFoundError
    };

    CalDAVDiscovery(const QString &serviceName,
                    const QString &username,
                    const QString &password,
                    Accounts::Account *account,
                    Accounts::Manager *accountManager,
                    QNetworkAccessManager *networkManager,
                    QObject *parent = 0);
    ~CalDAVDiscovery();

    void setVerbose(bool verbose) { m_verbose = verbose; }

    void start(const QString &serverAddress,
               const QString &calendarHomePath = QString());

    static void writeCalendars(Accounts::Account *account, const Accounts::Service &srv, const QList<OnlineCalendar> &calendars);

Q_SIGNALS:
    void error();
    void success();

private Q_SLOTS:
    void handleSslErrors(const QList<QSslError> &errors);
    void requestUserPrincipalUrlFinished();
    void requestCalendarHomeUrlFinished();
    void requestCalendarListFinished();

private:
    void startRequests();
    void requestUserPrincipalUrl(const QString &discoveryPath);
    void requestCalendarHomeUrl(const QString &userPrincipalPath);
    void requestCalendarList(const QString &calendarHomePath);
    bool addNextCalendar(QXmlStreamReader *reader, QString *parsedUserPrincipalPath);
    void emitNetworkReplyError(const QNetworkReply &reply);
    void emitError(Error errorCode);
    void setStatus(Status status);
    QNetworkRequest templateRequest(const QString &destUrlString = QString()) const;

    QHash<QNetworkReply *, QIODevice *> m_pendingReplies;
    QList<OnlineCalendar> m_calendars;
    Accounts::Account *m_account;
    Accounts::Manager *m_accountManager;
    QNetworkAccessManager *m_networkAccessManager;
    Status m_status;
    QString m_serviceName;
    QString m_username;
    QString m_password;
    QString m_serverAddress;
    QString m_calendarHomePath;
    QSet<QString> m_userPrincipalPaths;
    bool m_verbose;
};

#endif // CDAVTOOL_HELPERS_H
