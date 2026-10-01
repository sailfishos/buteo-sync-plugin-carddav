/*
 * This file is part of buteo-sync-plugin-carddav package
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

#ifndef DETAILPAIRING_P_H
#define DETAILPAIRING_P_H

#include <QContact>
#include <QHash>
#include <QSet>

QTCONTACTS_USE_NAMESPACE

void attachLocalDetailIds(QContact *remote, const QContact &local,
                          const QSet<QContactDetail::DetailType> &ignorableTypes,
                          const QHash<QContactDetail::DetailType, QSet<int> > &ignorableFields,
                          const QSet<int> &ignorableCommonFields);

#endif // DETAILPAIRING_P_H
