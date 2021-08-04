/*
 * This file is part of buteo-sync-plugin-carddav package
 *
 * Copyright (C) 2014 - 2021 Jolla Ltd. and/or its subsidiary(-ies).
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

#include "carddavclient.h"
#include "syncer_p.h"

#include <PluginCbInterface.h>
#include "logging.h"
#include <ProfileEngineDefs.h>
#include <ProfileManager.h>

Buteo::ClientPlugin* CardDavClientLoader::createClientPlugin(
        const QString& pluginName,
        const Buteo::SyncProfile& profile,
        Buteo::PluginCbInterface* cbInterface)
{
    return new CardDavClient(pluginName, profile, cbInterface);
}


CardDavClient::CardDavClient(const QString& aPluginName,
                            const Buteo::SyncProfile& aProfile,
                            Buteo::PluginCbInterface *aCbInterface)
    : ClientPlugin(aPluginName, aProfile, aCbInterface)
    , m_syncer(0)
    , m_accountId(0)
{
    FUNCTION_CALL_TRACE(lcCardDavTrace);
}

CardDavClient::~CardDavClient()
{
    FUNCTION_CALL_TRACE(lcCardDavTrace);
}

void CardDavClient::connectivityStateChanged(Sync::ConnectivityType aType, bool aState)
{
    FUNCTION_CALL_TRACE(lcCardDavTrace);
    qCDebug(lcCardDav) << "Received connectivity change event:" << aType << " changed to " << aState;
    if (aType == Sync::CONNECTIVITY_INTERNET && !aState) {
        // we lost connectivity during sync.
        abortSync(Buteo::SyncResults::CONNECTION_ERROR);
    }
}

bool CardDavClient::init()
{
    FUNCTION_CALL_TRACE(lcCardDavTrace);

    QString accountIdString = iProfile.key(Buteo::KEY_ACCOUNT_ID);
    m_accountId = accountIdString.toInt();
    if (m_accountId == 0) {
        qCCritical(lcCardDav) << "profile does not specify" << Buteo::KEY_ACCOUNT_ID;
        return false;
    }

    m_syncDirection = iProfile.syncDirection();
    m_conflictResPolicy = iProfile.conflictResolutionPolicy();
    if (!m_syncer) {
        m_syncer = new Syncer(this, &iProfile, m_accountId);
        connect(m_syncer, SIGNAL(syncSucceeded()),
                this, SLOT(syncSucceeded()));
        connect(m_syncer, SIGNAL(syncFailed()),
                this, SLOT(syncFailed()));
    }

    return true;
}

bool CardDavClient::uninit()
{
    FUNCTION_CALL_TRACE(lcCardDavTrace);
    delete m_syncer;
    m_syncer = 0;
    return true;
}

bool CardDavClient::startSync()
{
    FUNCTION_CALL_TRACE(lcCardDavTrace);
    if (m_accountId == 0) return false;
    m_syncer->startSync(m_accountId);
    return true;
}

void CardDavClient::syncSucceeded()
{
    syncFinished(Buteo::SyncResults::NO_ERROR, QString());
}

void CardDavClient::syncFailed()
{
    syncFinished(Buteo::SyncResults::INTERNAL_ERROR, QString());
}

void CardDavClient::abortSync(Buteo::SyncResults::MinorCode code)
{
    FUNCTION_CALL_TRACE(lcCardDavTrace);
    m_syncer->abortSync();
    syncFinished(code, QStringLiteral("Sync aborted"));
}

void CardDavClient::syncFinished(Buteo::SyncResults::MinorCode minorErrorCode, const QString &message)
{
    FUNCTION_CALL_TRACE(lcCardDavTrace);

    if (minorErrorCode == Buteo::SyncResults::NO_ERROR) {
        qCDebug(lcCardDav) << "CardDAV sync succeeded!" << message;
        m_results = Buteo::SyncResults(QDateTime::currentDateTimeUtc(),
                                       Buteo::SyncResults::SYNC_RESULT_SUCCESS,
                                       Buteo::SyncResults::NO_ERROR);
        emit success(getProfileName(), message);
    } else {
        qCCritical(lcCardDav) << "CardDAV sync failed:" << minorErrorCode << message;
        m_results = Buteo::SyncResults(iProfile.lastSuccessfulSyncTime(), // don't change the last sync time
                                       Buteo::SyncResults::SYNC_RESULT_FAILED,
                                       minorErrorCode);
        emit error(getProfileName(), message, minorErrorCode);
    }
}

Buteo::SyncResults CardDavClient::getSyncResults() const
{
    FUNCTION_CALL_TRACE(lcCardDavTrace);
    return m_results;
}

bool CardDavClient::cleanUp()
{
    FUNCTION_CALL_TRACE(lcCardDavTrace);

    // This function is called after the account has been deleted.
    QString accountIdString = iProfile.key(Buteo::KEY_ACCOUNT_ID);
    m_accountId = accountIdString.toInt();
    if (m_accountId == 0) {
        qCCritical(lcCardDav) << "profile does not specify" << Buteo::KEY_ACCOUNT_ID;
        return false;
    }

    if (!m_syncer) m_syncer = new Syncer(this, &iProfile, m_accountId);
    m_syncer->purgeAccount(m_accountId);
    delete m_syncer;
    m_syncer = 0;

    return true;
}
