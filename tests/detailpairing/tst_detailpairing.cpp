#include <QtTest>

#include "detailpairing_p.h"

#include <QContact>
#include <QContactEmailAddress>
#include <QContactPhoneNumber>
#include <qtcontacts-extensions.h>

QTCONTACTS_USE_NAMESPACE

namespace {
    const QSet<int> commonFields {
        QContactDetail::FieldProvenance,
        QContactDetail__FieldModifiable,
        QContactDetail__FieldNonexportable,
        QContactDetail__FieldChangeFlags,
        QContactDetail__FieldDatabaseId
    };

    struct Local {
        QString value;
        quint32 dbId;
        int flags;
        QList<int> contexts;
    };

    QContact localContact(const QList<Local> &emails)
    {
        QContact c;
        for (const Local &l : emails) {
            QContactEmailAddress e;
            e.setEmailAddress(l.value);
            if (!l.contexts.isEmpty()) {
                e.setContexts(l.contexts);
            }
            e.setValue(QContactDetail__FieldDatabaseId, l.dbId);
            e.setValue(QContactDetail__FieldChangeFlags, l.flags);
            c.saveDetail(&e, QContact::IgnoreAccessConstraints);
        }
        return c;
    }

    QContact remoteContact(const QStringList &emails, const QList<int> &contexts = QList<int>())
    {
        QContact c;
        for (const QString &value : emails) {
            QContactEmailAddress e;
            e.setEmailAddress(value);
            if (!contexts.isEmpty()) {
                e.setContexts(contexts);
            }
            c.saveDetail(&e, QContact::IgnoreAccessConstraints);
        }
        return c;
    }

    // Database ids attached to the remote details, by value.
    QMultiMap<QString, quint32> attached(const QContact &local, QContact remote)
    {
        attachLocalDetailIds(&remote, local, QSet<QContactDetail::DetailType>(),
                             QHash<QContactDetail::DetailType, QSet<int> >(), commonFields);
        QMultiMap<QString, quint32> ids;
        for (const QContactEmailAddress &e : remote.details<QContactEmailAddress>()) {
            ids.insert(e.emailAddress(), e.value(QContactDetail__FieldDatabaseId).toUInt());
        }
        return ids;
    }

    QContactPhoneNumber phone(const QString &number, const QList<int> &subTypes,
                              quint32 dbId = 0, int flags = 0)
    {
        QContactPhoneNumber p;
        p.setNumber(number);
        p.setSubTypes(subTypes);
        if (dbId) {
            p.setValue(QContactDetail__FieldDatabaseId, dbId);
            p.setValue(QContactDetail__FieldChangeFlags, flags);
        }
        return p;
    }

    const int Unchanged = 0;
    const int Modified = QContactDetail__ChangeFlag_IsModified;
    const int Deleted = QContactDetail__ChangeFlag_IsDeleted;
    const int Added = QContactDetail__ChangeFlag_IsAdded;
}

class tst_detailpairing : public QObject
{
    Q_OBJECT

private slots:
    void noLocalChange();
    void deletionServerUntouched();
    void deletionValueMovedOnServer();
    void deletionOtherChangedOnServer();
    void deletionNextToLocalModification();
    void deletionNextToLocalAddition();
    void modificationSoleCandidate();
    void duplicateRemoteValue();
    void soleCandidateNextToLocalDeletion();
    void soleCandidateNextToRemoteChange();
    void contextsCompareByValue();
    void ignorableFieldsIgnored();
};

void tst_detailpairing::noLocalChange()
{
    const QMultiMap<QString, quint32> ids = attached(
            localContact({ { "a@x", 1, Unchanged } }),
            remoteContact({ "a@x" }));
    QCOMPARE(ids.value("a@x"), 0u);
}

void tst_detailpairing::deletionServerUntouched()
{
    const QMultiMap<QString, quint32> ids = attached(
            localContact({ { "a@x", 1, Deleted }, { "b@x", 2, Unchanged } }),
            remoteContact({ "a@x", "b@x" }));
    QCOMPARE(ids.value("a@x"), 1u);
    QCOMPARE(ids.value("b@x"), 2u);
}

// Deleted here and on the server, which also changed b to a: the remote a is
// the former b and must not inherit the deletion.
void tst_detailpairing::deletionValueMovedOnServer()
{
    const QMultiMap<QString, quint32> ids = attached(
            localContact({ { "a@x", 1, Deleted }, { "b@x", 2, Unchanged } }),
            remoteContact({ "a@x" }));
    QVERIFY(ids.value("a@x") != 1u);
}

void tst_detailpairing::deletionOtherChangedOnServer()
{
    const QMultiMap<QString, quint32> ids = attached(
            localContact({ { "a@x", 1, Deleted }, { "b@x", 2, Unchanged } }),
            remoteContact({ "a@x", "c@x" }));
    QCOMPARE(ids.value("a@x"), 0u);
    QCOMPARE(ids.value("c@x"), 0u);
}

// A local modification hides the value the server last saw.
void tst_detailpairing::deletionNextToLocalModification()
{
    const QMultiMap<QString, quint32> ids = attached(
            localContact({ { "a@x", 1, Deleted }, { "b2@x", 2, Modified } }),
            remoteContact({ "a@x", "b@x" }));
    QCOMPARE(ids.value("a@x"), 0u);
}

// A local addition is not part of what the server last saw.
void tst_detailpairing::deletionNextToLocalAddition()
{
    const QMultiMap<QString, quint32> ids = attached(
            localContact({ { "a@x", 1, Deleted }, { "c@x", 3, Added } }),
            remoteContact({ "a@x" }));
    QCOMPARE(ids.value("a@x"), 1u);
}

void tst_detailpairing::modificationSoleCandidate()
{
    const QMultiMap<QString, quint32> ids = attached(
            localContact({ { "a2@x", 1, Modified } }),
            remoteContact({ "a3@x" }));
    QCOMPARE(ids.value("a3@x"), 1u);
}

void tst_detailpairing::duplicateRemoteValue()
{
    const QMultiMap<QString, quint32> ids = attached(
            localContact({ { "a@x", 1, Deleted } }),
            remoteContact({ "a@x", "a@x" }));
    QCOMPARE(ids.values("a@x"), QList<quint32>({ 0u, 0u }));
}

// Here a2, deleted b; there b deleted too.  Looks like the case below: unpaired.
void tst_detailpairing::soleCandidateNextToLocalDeletion()
{
    const QMultiMap<QString, quint32> ids = attached(
            localContact({ { "a2@x", 1, Modified }, { "b@x", 2, Deleted } }),
            remoteContact({ "a@x" }));
    QCOMPARE(ids.value("a@x"), 0u);
}

// Here a2, deleted b; there a removed, b changed to b2: no guess, the remote version stands.
void tst_detailpairing::soleCandidateNextToRemoteChange()
{
    const QMultiMap<QString, quint32> ids = attached(
            localContact({ { "a2@x", 1, Modified }, { "b@x", 2, Deleted } }),
            remoteContact({ "b2@x" }));
    QCOMPARE(ids.value("b2@x"), 0u);
}

void tst_detailpairing::contextsCompareByValue()
{
    const QList<int> home { QContactDetail::ContextHome };
    const QMultiMap<QString, quint32> ids = attached(
            localContact({ { "a@x", 1, Deleted, home }, { "b@x", 2, Unchanged, home } }),
            remoteContact({ "a@x", "b@x" }, home));
    QCOMPARE(ids.value("a@x"), 1u);
    QCOMPARE(ids.value("b@x"), 2u);
}

// Subtypes differ between the database and the vCard; the adaptor ignores them.
void tst_detailpairing::ignorableFieldsIgnored()
{
    const QList<int> mobile { QContactPhoneNumber::SubTypeMobile };
    QContact local;
    QContactPhoneNumber p1 = phone("123", mobile, 1, Deleted);
    QContactPhoneNumber p2 = phone("456", mobile, 2, Unchanged);
    local.saveDetail(&p1, QContact::IgnoreAccessConstraints);
    local.saveDetail(&p2, QContact::IgnoreAccessConstraints);
    QContact remote;
    QContactPhoneNumber r1 = phone("123", QList<int>());
    QContactPhoneNumber r2 = phone("456", QList<int>());
    remote.saveDetail(&r1, QContact::IgnoreAccessConstraints);
    remote.saveDetail(&r2, QContact::IgnoreAccessConstraints);

    QHash<QContactDetail::DetailType, QSet<int> > fields;
    fields[QContactDetail::TypePhoneNumber] << QContactPhoneNumber::FieldSubTypes;
    attachLocalDetailIds(&remote, local, QSet<QContactDetail::DetailType>(), fields, commonFields);

    QMap<QString, quint32> ids;
    for (const QContactPhoneNumber &p : remote.details<QContactPhoneNumber>()) {
        ids.insert(p.number(), p.value(QContactDetail__FieldDatabaseId).toUInt());
    }
    QCOMPARE(ids.value("123"), 1u);
    QCOMPARE(ids.value("456"), 2u);
}

QTEST_GUILESS_MAIN(tst_detailpairing)
#include "tst_detailpairing.moc"
