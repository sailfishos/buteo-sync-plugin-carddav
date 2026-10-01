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

#include "detailpairing_p.h"

#include <qtcontacts-extensions.h>

namespace {
    // The values which identify a detail's content.  The caller supplies the
    // fields the sync adaptor itself ignores when it compares a contact from
    // the database against one imported from a vCard; the rest are set by the
    // database and are simply absent on the imported side.
    QMap<int, QVariant> comparableValues(const QContactDetail &detail,
                                         const QSet<int> &ignorableCommonFields,
                                         const QSet<int> &ignorableFields) {
        QMap<int, QVariant> values = detail.values();
        for (int field : ignorableCommonFields) {
            values.remove(field);
        }
        for (int field : ignorableFields) {
            values.remove(field);
        }
        values.remove(QContactDetail__FieldUnhandledChangeFlags);
        values.remove(QContactDetail__FieldCreated);
        values.remove(QContactDetail__FieldModified);
        values.remove(QContactDetail::FieldDetailUri);
        values.remove(QContactDetail::FieldLinkedDetailUris);
        return values;
    }

    bool valueIsEmpty(const QVariant &value) {
        if (!value.isValid()) {
            return true;
        }
        if (value.canConvert<QList<int> >()) {
            return value.value<QList<int> >().isEmpty();
        }
        return value.userType() == QMetaType::QString && value.toString().isEmpty();
    }

    // Contexts and subtypes are stored as QList<int>, and a QVariant holding one
    // does not compare equal even to an identical copy - qtpim carries the same
    // workaround in QContactDetailPrivate.  An absent field and an empty one
    // mean the same thing, so they are treated alike.
    bool detailValuesMatch(const QContactDetail &a, const QContactDetail &b,
                           const QSet<int> &ignorableCommonFields,
                           const QSet<int> &ignorableFields) {
        const QMap<int, QVariant> avalues = comparableValues(a, ignorableCommonFields, ignorableFields);
        const QMap<int, QVariant> bvalues = comparableValues(b, ignorableCommonFields, ignorableFields);
        QList<int> fields = avalues.keys();
        for (int field : bvalues.keys()) {
            if (!fields.contains(field)) {
                fields.append(field);
            }
        }
        for (int field : fields) {
            const QVariant avalue = avalues.value(field);
            const QVariant bvalue = bvalues.value(field);
            if (valueIsEmpty(avalue) && valueIsEmpty(bvalue)) {
                continue;
            }
            if (avalue.canConvert<QList<int> >() && bvalue.canConvert<QList<int> >()) {
                if (avalue.value<QList<int> >() != bvalue.value<QList<int> >()) {
                    return false;
                }
            } else if (avalue != bvalue) {
                return false;
            }
        }
        return true;
    }

    // Bookkeeping details: never paired up, the sync code owns them.
    bool isBookkeepingDetail(QContactDetail::DetailType type) {
        return type == QContactDetail::TypeGuid
            || type == QContactDetail::TypeSyncTarget
            || type == QContactDetail::TypeTimestamp
            || type == QContactDetail::TypeDisplayLabel
            || type == QContactDetail::TypeExtendedDetail;
    }
}

// TwoWayContactSyncAdaptor::resolveConflictingChanges() can only carry a
// local detail modification or deletion over to the remote contact when
// both details share a persistent database id.  A contact parsed from a
// vCard has none, so a local change to an existing detail is dropped from
// the merge without a word.  Attach the local ids for the details which can
// be paired up without guessing.
void attachLocalDetailIds(QContact *remote, const QContact &local,
                          const QSet<QContactDetail::DetailType> &ignorableTypes,
                          const QHash<QContactDetail::DetailType, QSet<int> > &ignorableFields,
                          const QSet<int> &ignorableCommonFields) {
    const QList<QContactDetail> localDetails = local.details();

    // Without a flagged local detail an id could only do harm.
    bool haveLocalChange = false;
    for (const QContactDetail &detail : localDetails) {
        if (detail.value(QContactDetail__FieldChangeFlags).toInt() != 0) {
            haveLocalChange = true;
            break;
        }
    }
    if (!haveLocalChange) {
        return;
    }

    const int remoteCount = remote->details().size();
    QSet<int> pairedLocal;
    QSet<int> pairedRemote;

    auto changeFlags = [&localDetails] (int l) {
        return localDetails.at(l).value(QContactDetail__FieldChangeFlags).toInt();
    };

    auto skipType = [&ignorableTypes] (QContactDetail::DetailType type) {
        return isBookkeepingDetail(type) || ignorableTypes.contains(type);
    };

    auto valuesMatch = [&] (const QContactDetail &a, const QContactDetail &b) {
        return a.type() == b.type()
            && detailValuesMatch(a, b, ignorableCommonFields, ignorableFields.value(a.type()));
    };

    auto pairUp = [&] (int r, int l) {
        const quint32 dbId = localDetails.at(l).value(QContactDetail__FieldDatabaseId).toUInt();
        if (dbId == 0
                || (changeFlags(l) & QContactDetail__ChangeFlag_IsAdded) > 0) {
            // Additions are applied directly by resolveConflictingChanges()
            // and never looked up by id; pairing one would only risk handing
            // the same row id to two details of the same contact.
            return false;
        }
        QContactDetail detail = remote->details().at(r);
        detail.setValue(QContactDetail__FieldDatabaseId, dbId);
        remote->saveDetail(&detail, QContact::IgnoreAccessConstraints);
        pairedLocal.insert(l);
        pairedRemote.insert(r);
        return true;
    };

    // Equal values prove identity only if the server left the type alone: it
    // may have removed the deleted value and changed another detail to it.
    // Untouched means the remote details of the type are exactly what the
    // local side had at the last sync - the unflagged ones plus the deleted
    // ones.  A local modification hides its old value, so it rules that out.
    auto untouchedRemotely = [&] (QContactDetail::DetailType type) {
        QList<QContactDetail> previous;
        for (int l = 0; l < localDetails.size(); ++l) {
            if (localDetails.at(l).type() != type) {
                continue;
            }
            const int flags = changeFlags(l);
            if ((flags & QContactDetail__ChangeFlag_IsModified) > 0) {
                return false;
            }
            if ((flags & QContactDetail__ChangeFlag_IsAdded) == 0) {
                previous.append(localDetails.at(l));
            }
        }
        for (const QContactDetail &rdetail : remote->details()) {
            if (rdetail.type() != type) {
                continue;
            }
            int i = 0;
            while (i < previous.size() && !valuesMatch(previous.at(i), rdetail)) {
                ++i;
            }
            if (i == previous.size()) {
                return false;
            }
            previous.removeAt(i);
        }
        return previous.isEmpty();
    };

    // Live local details get first refusal, so that a deleted one cannot claim
    // the detail its unchanged twin should keep - with duplicates on the local
    // side the pairing would otherwise depend on their order.
    for (int deletedRound = 0; deletedRound < 2; ++deletedRound) {
        for (int r = 0; r < remoteCount; ++r) {
            if (pairedRemote.contains(r)) {
                continue;
            }
            const QContactDetail rdetail = remote->details().at(r);
            if (skipType(rdetail.type())) {
                continue;
            }
            // A value the remote side carries more than once cannot be told
            // apart, and stamping an id on one of them would keep
            // resolveConflictingChanges() from collapsing the duplicate.
            int equalRemote = 0;
            for (int o = 0; o < remoteCount; ++o) {
                if (valuesMatch(remote->details().at(o), rdetail)) {
                    ++equalRemote;
                }
            }
            if (equalRemote != 1
                    || (deletedRound == 1 && !untouchedRemotely(rdetail.type()))) {
                continue;
            }
            for (int l = 0; l < localDetails.size(); ++l) {
                if (pairedLocal.contains(l) || !valuesMatch(localDetails.at(l), rdetail)) {
                    continue;
                }
                const bool deleted = (changeFlags(l) & QContactDetail__ChangeFlag_IsDeleted) > 0;
                if (deleted != (deletedRound == 1)) {
                    continue;
                }
                if (pairUp(r, l)) {
                    break;
                }
            }
        }
    }

    // Then the sole remaining candidate of its type on either side.  The
    // values differ - that is the conflict.  Deletions are left out: equal
    // count is not identity.  This guesses: a remote replacement, or a remote
    // change next to a local deletion, looks the same as a change, and then
    // the local modification wins, as in the adaptor.
    for (int r = 0; r < remoteCount; ++r) {
        if (pairedRemote.contains(r)) {
            continue;
        }
        const QContactDetail::DetailType type = remote->details().at(r).type();
        if (skipType(type)) {
            continue;
        }
        int unpairedRemote = 0;
        for (int o = 0; o < remoteCount; ++o) {
            if (!pairedRemote.contains(o) && remote->details().at(o).type() == type) {
                ++unpairedRemote;
            }
        }
        int candidates = 0;
        int candidate = -1;
        for (int l = 0; l < localDetails.size(); ++l) {
            if (pairedLocal.contains(l) || localDetails.at(l).type() != type) {
                continue;
            }
            const int flags = changeFlags(l);
            if ((flags & QContactDetail__ChangeFlag_IsDeleted) > 0
                    || (flags & QContactDetail__ChangeFlag_IsAdded) > 0) {
                continue;
            }
            ++candidates;
            candidate = l;
        }
        if (unpairedRemote == 1 && candidates == 1) {
            pairUp(r, candidate);
        }
    }
}
