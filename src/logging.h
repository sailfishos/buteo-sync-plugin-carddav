/*
 * This file is part of buteo-sync-plugin-carddav package
 *
 * Copyright (C) 2021 Jolla Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef CARDDAV_LOGGING_H
#define CARDDAV_LOGGING_H

#include <QLoggingCategory>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    #include <buteosyncfw6/LogMacros.h>
#else
    #include <buteosyncfw5/LogMacros.h>
#endif

Q_DECLARE_LOGGING_CATEGORY(lcCardDav)
Q_DECLARE_LOGGING_CATEGORY(lcCardDavProtocol)
Q_DECLARE_LOGGING_CATEGORY(lcCardDavTrace)

#endif
