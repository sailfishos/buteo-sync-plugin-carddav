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

#ifndef SYNCER_P_H
#define SYNCER_P_H

#include "replyparser_p.h"

#include <twowaycontactsyncadaptor.h>

#include <QObject>
#include <QDateTime>
#include <QString>
#include <QList>
#include <QPair>
#include <QNetworkAccessManager>

#include <QContactManager>
#include <QContact>
#include <QContactCollection>

QTCONTACTS_USE_NAMESPACE

class tst_replyparser;

class Auth;
class CardDav;
class RequestGenerator;
namespace Buteo { class SyncProfile; }

class Syncer : public QObject, public QtContactsSqliteExtensions::TwoWayContactSyncAdaptor
{
    Q_OBJECT

public:
    Syncer(QObject *parent, Buteo::SyncProfile *profile, int accountId);
   ~Syncer();

    void startSync(int accountId);
    void purgeAccount(int accountId);
    void abortSync();

Q_SIGNALS:
    void syncSucceeded();
    void syncFailed();

protected:
    // implementing the TWCSA interface
    bool determineRemoteCollections();
    bool determineRemoteCollectionChanges(
        const QList<QContactCollection> &locallyAddedCollections,
        const QList<QContactCollection> &locallyModifiedCollections,
        const QList<QContactCollection> &locallyRemovedCollections,
        const QList<QContactCollection> &locallyUnmodifiedCollections,
        QContactManager::Error *error);
    bool determineRemoteContacts(const QContactCollection &collection);
    bool determineRemoteContactChanges(
            const QContactCollection &collection,
            const QList<QContact> &localAddedContacts,
            const QList<QContact> &localModifiedContacts,
            const QList<QContact> &localDeletedContacts,
            const QList<QContact> &localUnmodifiedContacts,
            QContactManager::Error *error);
    bool deleteRemoteCollection(const QContactCollection &collection);
    bool storeLocalChangesRemotely(
            const QContactCollection &collection,
            const QList<QContact> &addedContacts,
            const QList<QContact> &modifiedContacts,
            const QList<QContact> &deletedContacts);

private Q_SLOTS:
    void sync(const QString &serverUrl, const QString &addressbookPath, const QString &username, const QString &password, const QString &accessToken, bool ignoreSslErrors);
    void signInError();
    void cardDavError(int errorCode = 0);
    void syncFinishedSuccessfully();
    void syncFinishedWithError();

private:
    friend class CardDav;
    friend class RequestGenerator;
    friend class ReplyParser;
    friend class tst_replyparser;
    Buteo::SyncProfile *m_syncProfile;
    CardDav *m_cardDav;
    Auth *m_auth;
    QContactManager m_contactManager;
    QNetworkAccessManager m_qnam;
    bool m_syncAborted;
    bool m_syncError;

    // auth related
    int m_accountId;
    QString m_serverUrl;
    QString m_addressbookPath;
    QString m_username;
    QString m_password;
    QString m_accessToken;
    bool m_ignoreSslErrors;

    // the ctag and sync token for each particular addressbook, as stored during the previous sync cycle.
    QHash<QString, QPair<QString, QString> > m_previousCtagSyncToken; // uri to ctag+synctoken.
    QHash<QString, QContactCollection> m_currentCollections;
    QHash<QString, QHash<QString, QString> > m_localContactUrisEtags;

    // colletion uri to contact uri (sync target) to contact info
    QHash<QString, QHash<QString, ReplyParser::ContactInformation> > m_remoteAdditions;
    QHash<QString, QHash<QString, ReplyParser::ContactInformation> > m_remoteModifications;
    QHash<QString, QHash<QString, ReplyParser::ContactInformation> > m_remoteRemovals;
    QHash<QString, QHash<QString, ReplyParser::ContactInformation> > m_remoteUnmodified;

    // for change detection
    struct AMRU {
        QList<QContact> added;
        QList<QContact> modified;
        QList<QContact> removed;
        QList<QContact> unmodified;
    };
    QHash<QString, AMRU> m_collectionAMRU; // collection uri to AMRU
};

#endif // SYNCER_P_H
