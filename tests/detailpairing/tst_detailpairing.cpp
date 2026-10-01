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
    };

    QContact localContact(const QList<Local> &emails)
    {
        QContact c;
        for (const Local &l : emails) {
            QContactEmailAddress e;
            e.setEmailAddress(l.value);
            e.setValue(QContactDetail__FieldDatabaseId, l.dbId);
            e.setValue(QContactDetail__FieldChangeFlags, l.flags);
            c.saveDetail(&e, QContact::IgnoreAccessConstraints);
        }
        return c;
    }

    QContact remoteContact(const QStringList &emails)
    {
        QContact c;
        for (const QString &value : emails) {
            QContactEmailAddress e;
            e.setEmailAddress(value);
            c.saveDetail(&e, QContact::IgnoreAccessConstraints);
        }
        return c;
    }

    // Database ids attached to the remote details, by value.
    QMap<QString, quint32> attached(const QContact &local, QContact remote)
    {
        attachLocalDetailIds(&remote, local, QSet<QContactDetail::DetailType>(),
                             QHash<QContactDetail::DetailType, QSet<int> >(), commonFields);
        QMap<QString, quint32> ids;
        for (const QContactEmailAddress &e : remote.details<QContactEmailAddress>()) {
            ids.insertMulti(e.emailAddress(), e.value(QContactDetail__FieldDatabaseId).toUInt());
        }
        return ids;
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
};

void tst_detailpairing::noLocalChange()
{
    const QMap<QString, quint32> ids = attached(
            localContact({ { "a@x", 1, Unchanged } }),
            remoteContact({ "a@x" }));
    QCOMPARE(ids.value("a@x"), 0u);
}

void tst_detailpairing::deletionServerUntouched()
{
    const QMap<QString, quint32> ids = attached(
            localContact({ { "a@x", 1, Deleted }, { "b@x", 2, Unchanged } }),
            remoteContact({ "a@x", "b@x" }));
    QCOMPARE(ids.value("a@x"), 1u);
    QCOMPARE(ids.value("b@x"), 2u);
}

// Deleted here and on the server, which also changed b to a: the remote a is
// the former b and must not inherit the deletion.
void tst_detailpairing::deletionValueMovedOnServer()
{
    const QMap<QString, quint32> ids = attached(
            localContact({ { "a@x", 1, Deleted }, { "b@x", 2, Unchanged } }),
            remoteContact({ "a@x" }));
    QCOMPARE(ids.value("a@x"), 2u);
}

void tst_detailpairing::deletionOtherChangedOnServer()
{
    const QMap<QString, quint32> ids = attached(
            localContact({ { "a@x", 1, Deleted }, { "b@x", 2, Unchanged } }),
            remoteContact({ "a@x", "c@x" }));
    QCOMPARE(ids.value("a@x"), 0u);
    QCOMPARE(ids.value("c@x"), 0u);
}

// A local modification hides the value the server last saw.
void tst_detailpairing::deletionNextToLocalModification()
{
    const QMap<QString, quint32> ids = attached(
            localContact({ { "a@x", 1, Deleted }, { "b2@x", 2, Modified } }),
            remoteContact({ "a@x", "b@x" }));
    QCOMPARE(ids.value("a@x"), 0u);
}

// A local addition is not part of what the server last saw.
void tst_detailpairing::deletionNextToLocalAddition()
{
    const QMap<QString, quint32> ids = attached(
            localContact({ { "a@x", 1, Deleted }, { "c@x", 3, Added } }),
            remoteContact({ "a@x" }));
    QCOMPARE(ids.value("a@x"), 1u);
}

void tst_detailpairing::modificationSoleCandidate()
{
    const QMap<QString, quint32> ids = attached(
            localContact({ { "a2@x", 1, Modified } }),
            remoteContact({ "a3@x" }));
    QCOMPARE(ids.value("a3@x"), 1u);
}

void tst_detailpairing::duplicateRemoteValue()
{
    const QMap<QString, quint32> ids = attached(
            localContact({ { "a@x", 1, Deleted } }),
            remoteContact({ "a@x", "a@x" }));
    QCOMPARE(ids.values("a@x"), QList<quint32>({ 0u, 0u }));
}

QTEST_GUILESS_MAIN(tst_detailpairing)
#include "tst_detailpairing.moc"
