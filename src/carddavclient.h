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

#ifndef CARDDAVCLIENT_H
#define CARDDAVCLIENT_H

#include <ClientPlugin.h>
#include <SyncProfile.h>
#include <SyncResults.h>
#include <SyncCommonDefs.h>
#include <SyncPluginLoader.h>

#include <QString>
#include <QObject>

class Syncer;
class Q_DECL_EXPORT CardDavClient : public Buteo::ClientPlugin
{
    Q_OBJECT

public:
    CardDavClient(const QString &aPluginName,
                  const Buteo::SyncProfile &aProfile,
                  Buteo::PluginCbInterface *aCbInterface);
    ~CardDavClient();

    bool init();
    bool uninit();
    bool startSync();
    Buteo::SyncResults getSyncResults() const;
    bool cleanUp();

public Q_SLOTS:
    void connectivityStateChanged(Sync::ConnectivityType aType, bool aState);

private Q_SLOTS:
    void syncSucceeded();
    void syncFailed();

private:
    void abortSync(Buteo::SyncResults::MinorCode code);
    void syncFinished(Buteo::SyncResults::MinorCode minorErrorCode, const QString &message);
    Buteo::SyncProfile::SyncDirection syncDirection();
    Buteo::SyncProfile::ConflictResolutionPolicy conflictResolutionPolicy();

    Sync::SyncStatus            m_syncStatus;
    Buteo::SyncResults          m_results;
    Buteo::SyncProfile::SyncDirection m_syncDirection;
    Buteo::SyncProfile::ConflictResolutionPolicy m_conflictResPolicy;

    Syncer*                     m_syncer;
    int                         m_accountId;
};

class CardDavClientLoader : public Buteo::SyncPluginLoader
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.sailfishos.plugins.sync.CardDavClientLoader")
    Q_INTERFACES(Buteo::SyncPluginLoader)

public:
    /*! \brief Creates CardDav client plugin
     *
     * @param aPluginName Name of this client plugin
     * @param aProfile Profile to use
     * @param aCbInterface Pointer to the callback interface
     * @return Client plugin on success, otherwise NULL
     */
    Buteo::ClientPlugin* createClientPlugin(const QString& pluginName,
                                            const Buteo::SyncProfile& profile,
                                            Buteo::PluginCbInterface* cbInterface) override;
};

#endif // CARDDAVCLIENT_H
