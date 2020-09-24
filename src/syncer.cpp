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

#include "syncer_p.h"
#include "carddav_p.h"
#include "auth_p.h"

#include <twowaycontactsyncadaptor_impl.h>
#include <qtcontacts-extensions_manager_impl.h>
#include <contactmanagerengine.h>
#include <qcontactclearchangeflagsrequest.h>
#include <qcontactclearchangeflagsrequest_impl.h>
#include <qcontactstatusflags_impl.h>

#include <QtCore/QDateTime>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtCore/QFile>
#include <QtCore/QByteArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>

#include <QtContacts/QContact>
#include <QtContacts/QContactManager>
#include <QtContacts/QContactGuid>
#include <QtContacts/QContactSyncTarget>
#include <QtContacts/QContactPhoneNumber>
#include <QtContacts/QContactAvatar>
#include <QtContacts/QContactAddress>
#include <QtContacts/QContactUrl>
#include <QtContacts/QContactExtendedDetail>
#include <QtContacts/QContactDetailFilter>
#include <QtContacts/QContactIntersectionFilter>

#include <Accounts/Manager>
#include <Accounts/Account>

#include <SyncProfile.h>
#include <LogMacros.h>

#define CARDDAV_CONTACTS_APPLICATION QLatin1String("carddav")
static const int HTTP_UNAUTHORIZED_ACCESS = 401;

Syncer::Syncer(QObject *parent, Buteo::SyncProfile *syncProfile, int accountId)
    : QObject(parent), QtContactsSqliteExtensions::TwoWayContactSyncAdaptor(
            accountId, CARDDAV_CONTACTS_APPLICATION)
    , m_syncProfile(syncProfile)
    , m_cardDav(0)
    , m_auth(0)
    , m_contactManager(QStringLiteral("org.nemomobile.contacts.sqlite"))
    , m_syncAborted(false)
    , m_syncError(false)
    , m_accountId(accountId)
    , m_ignoreSslErrors(false)
{
    TwoWayContactSyncAdaptor::setManager(m_contactManager);
}

Syncer::~Syncer()
{
    delete m_auth;
    delete m_cardDav;
}

void Syncer::abortSync()
{
    m_syncAborted = true;
}

void Syncer::startSync(int accountId)
{
    Q_ASSERT(accountId != 0);
    m_accountId = accountId;
    m_auth = new Auth(this);
    connect(m_auth, SIGNAL(signInCompleted(QString,QString,QString,QString,QString,bool)),
            this, SLOT(sync(QString,QString,QString,QString,QString,bool)));
    connect(m_auth, SIGNAL(signInError()),
            this, SLOT(signInError()));
    LOG_DEBUG(Q_FUNC_INFO << "starting carddav sync with account" << m_accountId);
    m_auth->signIn(accountId);
}

void Syncer::signInError()
{
    emit syncFailed();
}

void Syncer::sync(const QString &serverUrl, const QString &addressbookPath, const QString &username, const QString &password, const QString &accessToken, bool ignoreSslErrors)
{
    m_serverUrl = serverUrl;
    m_addressbookPath = addressbookPath;
    m_username = username;
    m_password = password;
    m_accessToken = accessToken;
    m_ignoreSslErrors = ignoreSslErrors;

    m_cardDav = m_username.isEmpty()
              ? new CardDav(this, m_serverUrl, m_addressbookPath, m_accessToken)
              : new CardDav(this, m_serverUrl, m_addressbookPath, m_username, m_password);
    connect(m_cardDav, &CardDav::error,
            this, &Syncer::cardDavError);

    LOG_DEBUG("CardDAV Sync adapter initialised for account" << m_accountId << ", starting sync...");

    if (!TwoWayContactSyncAdaptor::startSync(TwoWayContactSyncAdaptor::ContinueAfterError)) {
        LOG_DEBUG("Unable to start CardDAV sync!");
    }
}

bool Syncer::determineRemoteCollections()
{
    m_cardDav->determineAddressbooksList();
    connect(m_cardDav, &CardDav::addressbooksList, this, [this] (const QList<ReplyParser::AddressBookInformation> &infos) {
        QStringList paths;
        QList<QContactCollection> addressbooks;
        for (QList<ReplyParser::AddressBookInformation>::const_iterator it = infos.constBegin(); it != infos.constEnd(); ++it) {
            if (!paths.contains(it->url)) {
                paths.append(it->url);
                QContactCollection addressbook;
                addressbook.setMetaData(QContactCollection::KeyName, it->displayName);
                addressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_AGGREGABLE, true);
                addressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME, CARDDAV_CONTACTS_APPLICATION);
                addressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID, m_accountId);
                addressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH, it->url);
                addressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_READONLY, it->readOnly);
                addressbook.setExtendedMetaData(KEY_CTAG, it->ctag);
                addressbook.setExtendedMetaData(KEY_SYNCTOKEN, it->syncToken);
                addressbooks.append(addressbook);
            }
        }
        remoteCollectionsDetermined(addressbooks);
    }, Qt::UniqueConnection);
    return true;
}

bool Syncer::determineRemoteCollectionChanges(
        const QList<QContactCollection> &locallyAddedCollections,
        const QList<QContactCollection> &locallyModifiedCollections,
        const QList<QContactCollection> &locallyRemovedCollections,
        const QList<QContactCollection> &locallyUnmodifiedCollections,
        QContactManager::Error *)
{
    m_cardDav->determineAddressbooksList();
    connect(m_cardDav, &CardDav::addressbooksList, this,
            [this, locallyAddedCollections, locallyModifiedCollections,
             locallyRemovedCollections, locallyUnmodifiedCollections]
            (const QList<ReplyParser::AddressBookInformation> &infos) {
        // create a list of collections from the addressbooks information
        QHash<QString, QContactCollection> remoteCollections;
        for (QList<ReplyParser::AddressBookInformation>::const_iterator it = infos.constBegin(); it != infos.constEnd(); ++it) {
            const QString path = it->url;
            if (!remoteCollections.contains(path)) {
                QContactCollection addressbook;
                addressbook.setMetaData(QContactCollection::KeyName, it->displayName);
                addressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_AGGREGABLE, true);
                addressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME, CARDDAV_CONTACTS_APPLICATION);
                addressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID, m_accountId);
                addressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH, path);
                addressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_READONLY, it->readOnly);
                addressbook.setExtendedMetaData(KEY_CTAG, it->ctag);
                addressbook.setExtendedMetaData(KEY_SYNCTOKEN, it->syncToken);
                remoteCollections.insert(path, addressbook);
            }
        }

        // determine which locally-present collections are not present remotely,
        // these will be remote deletions.
        // determine which locally-present collections are present remotely,
        // these will be either remote modifications, or unmodified.
        QList<QContactCollection> remotelyAddedCollections;
        QList<QContactCollection> remotelyModifiedCollections;
        QList<QContactCollection> remotelyRemovedCollections;
        QList<QContactCollection> remotelyUnmodifiedCollections;
        auto comparisonMethod = [this,
                                 &remoteCollections,
                                 &remotelyAddedCollections,
                                 &remotelyModifiedCollections,
                                 &remotelyRemovedCollections,
                                 &remotelyUnmodifiedCollections]
                                 (const QList<QContactCollection> &localCollections) {
            for (const QContactCollection &local : localCollections) {
                const QString path = local.extendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH).toString();
                if (!path.isEmpty()) {
                    if (!remoteCollections.contains(path)) {
                        // remote deletion
                        remotelyRemovedCollections.append(local);
                    } else {
                        // cache the previously stored ctag and synctoken value.
                        // this will be needed during the sync contacts step.
                        const QString prevCtag = local.extendedMetaData(KEY_CTAG).toString();
                        const QString prevSyncToken = local.extendedMetaData(KEY_SYNCTOKEN).toString();
                        m_previousCtagSyncToken.insert(path, qMakePair(prevCtag, prevSyncToken));
                        const QString remoteCtag = remoteCollections.value(path).extendedMetaData(KEY_CTAG).toString();
                        const QString remoteSyncToken = remoteCollections.value(path).extendedMetaData(KEY_SYNCTOKEN).toString();
                        if (prevCtag != remoteCtag || prevSyncToken != remoteSyncToken) {
                            // we assume that the only remote modification is the ctag/synctoken values.
                            // in future: sync more information (color etc) and detect changes.
                            remoteCollections.remove(path);
                            QContactCollection remoteMod = local;
                            remoteMod.setExtendedMetaData(KEY_CTAG, remoteCtag);
                            remoteMod.setExtendedMetaData(KEY_SYNCTOKEN, remoteSyncToken);
                            remotelyModifiedCollections.append(remoteMod);
                        } else {
                            // we assume that the remote collection is unmodified.
                            remotelyUnmodifiedCollections.append(remoteCollections.take(path));
                        }
                    }
                }
            }
        };
        comparisonMethod(locallyAddedCollections); // partial upsync artifact detection?  XXXXXX TODO: shouldn't hit this in the "normal" case...
        comparisonMethod(locallyModifiedCollections);
        comparisonMethod(locallyUnmodifiedCollections);

        // TODO: look at the collections which have been marked as locally added,
        // and try to match those to remote collections according to the display name.
        // these may be artifacts a previous failed sync cycle.

        // any collections left in the remoteCollections hash must be new/added remotely.
        remotelyAddedCollections.append(remoteCollections.values());

        // finished determining remote collection changes.
        remoteCollectionChangesDetermined(remotelyAddedCollections, remotelyModifiedCollections,
                                          remotelyRemovedCollections, remotelyUnmodifiedCollections);
    }, Qt::UniqueConnection);

    return true;
}

bool Syncer::determineRemoteContacts(const QContactCollection &collection)
{
    // don't attempt any delta detection, so pass in null ctag/syncToken values.
    const QString remotePath = collection.extendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH).toString();
    m_currentCollections.insert(remotePath, collection);
    // will call remoteContactsDetermined() when complete.
    return m_cardDav->downsyncAddressbookContent(remotePath, QString(), QString(), QString(), QString());
}

bool Syncer::determineRemoteContactChanges(
        const QContactCollection &collection,
        const QList<QContact> &localAddedContacts,
        const QList<QContact> &localModifiedContacts,
        const QList<QContact> &localDeletedContacts,
        const QList<QContact> &localUnmodifiedContacts,
        QContactManager::Error *error)
{
    const QString remotePath = collection.extendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH).toString();
    const QString newSyncToken = collection.extendedMetaData(KEY_SYNCTOKEN).toString();
    const QString newCtag = collection.extendedMetaData(KEY_CTAG).toString();
    const QString oldSyncToken = m_previousCtagSyncToken.value(remotePath).second;
    const QString oldCtag = m_previousCtagSyncToken.value(remotePath).first;

    // build a set of known contact uris/etags for use by the parser to determine delta.
    QHash<QString, QString> contactUrisEtags;
    auto builder = [&contactUrisEtags] (const QList<QContact> &contacts) {
        for (const QContact &c : contacts) {
            const QString uri = c.detail<QContactSyncTarget>().syncTarget();
            if (!uri.isEmpty()) {
                const QList<QContactExtendedDetail> dets = c.details<QContactExtendedDetail>();
                for (const QContactExtendedDetail &d : dets) {
                    if (d.name() == KEY_ETAG) {
                        contactUrisEtags.insert(uri, d.data().toString());
                        break;
                    }
                }
            }
        }
    };
    builder(localModifiedContacts);
    builder(localDeletedContacts);
    builder(localUnmodifiedContacts);

    m_localContactUrisEtags.insert(remotePath, contactUrisEtags);
    m_currentCollections.insert(remotePath, collection);

    // will call remoteContactChangesDetermined() when complete.
    bool ret = m_cardDav->downsyncAddressbookContent(
            remotePath,
            newSyncToken,
            newCtag,
            oldSyncToken,
            oldCtag);

    if (ret) {
        m_collectionAMRU.insert(remotePath, {
            localAddedContacts,
            localModifiedContacts,
            localDeletedContacts,
            localUnmodifiedContacts
        });
        *error = QContactManager::NoError;
    } else {
        *error = QContactManager::UnspecifiedError;
    }

    return ret;
}

bool Syncer::deleteRemoteCollection(const QContactCollection &)
{
    // TODO: implement this.
    LOG_WARNING(Q_FUNC_INFO << "delete remote collection operation not supported for carddav!");
    return true;
}

bool Syncer::storeLocalChangesRemotely(
        const QContactCollection &collection,
        const QList<QContact> &addedContacts,
        const QList<QContact> &modifiedContacts,
        const QList<QContact> &deletedContacts)
{
    const QString remotePath = collection.extendedMetaData(
            COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH).toString();

    // will call localChangesStoredRemotely() when complete.
    return m_cardDav->upsyncUpdates(remotePath,
                                    addedContacts,
                                    modifiedContacts,
                                    deletedContacts);
}

void Syncer::syncFinishedSuccessfully()
{
    LOG_DEBUG(Q_FUNC_INFO << "CardDAV sync with account" << m_accountId << "finished successfully!");
    emit syncSucceeded();
}

void Syncer::syncFinishedWithError()
{
    emit syncFailed();
}

void Syncer::cardDavError(int errorCode)
{
    LOG_WARNING("CardDAV sync for account: " << m_accountId << " finished with error:" << errorCode);
    m_syncError = true;
    if (errorCode == HTTP_UNAUTHORIZED_ACCESS) {
        m_auth->setCredentialsNeedUpdate(m_accountId);
    }
    QMetaObject::invokeMethod(this, "syncFinishedWithError", Qt::QueuedConnection);
}

void Syncer::purgeAccount(int accountId)
{
    QContactManager::Error err = QContactManager::NoError;
    QtContactsSqliteExtensions::ContactManagerEngine *cme = QtContactsSqliteExtensions::contactManagerEngine(m_contactManager);
    QList<QContactCollection> added, modified, deleted, unmodified;
    if (!cme->fetchCollectionChanges(accountId, QString(), &added, &modified, &deleted, &unmodified, &err)) {
        LOG_WARNING("Unable to retrieve CardDAV collections for purged account: " << m_accountId);
        return;
    }

    const QList<QContactCollection> all = added + modified + deleted + unmodified;
    QList<QContactCollectionId> purge;
    for (const QContactCollection &col : all) {
        purge.append(col.id());
    }

    if (purge.size() && !cme->storeChanges(nullptr, nullptr, purge,
            QtContactsSqliteExtensions::ContactManagerEngine::PreserveLocalChanges,
            true, &err)) {
        LOG_WARNING("Unable to delete CardDAV collections for purged account: " << m_accountId);
        return;
    }

    LOG_DEBUG(Q_FUNC_INFO << "Purged contacts for account: " << accountId);
}
