#include <QtTest>
#include <QObject>
#include <QMap>
#include <QString>

#include "replyparser_p.h"
#include "syncer_p.h"
#include "carddav_p.h"

#include <QContact>
#include <QContactDisplayLabel>
#include <QContactName>
#include <QContactPhoneNumber>
#include <QContactGuid>
#include <QContactSyncTarget>
#include <QContactGender>
#include <QContactBirthday>
#include <QContactTimestamp>
#include <QContactExtendedDetail>
#include <qtcontacts-extensions.h>

QTCONTACTS_USE_NAMESPACE

typedef QSet<QString> QSetString;
typedef QHash<QString, QString> QHashStringString;
typedef QHash<QString, QContact> QHashStringContact;

Q_DECLARE_METATYPE(QHashStringContact);

namespace {

void dumpContactDetail(const QContactDetail &d)
{
    qWarning() << "++ ---------" << d.type();
    QMap<int, QVariant> values = d.values();
    foreach (int key, values.keys()) {
        qWarning() << "    " << key << "=" << values.value(key);
    }
}

void dumpContact(const QContact &c)
{
    qWarning() << "++++ ---- Contact:" << c.id();
    QList<QContactDetail> cdets = c.details();
    foreach (const QContactDetail &det, cdets) {
        dumpContactDetail(det);
    }
}

QContact removeIgnorableFields(const QContact &c)
{
    QContact ret;
    ret.setId(c.id());
    QList<QContactDetail> cdets = c.details();
    foreach (const QContactDetail &det, cdets) {
        QContactDetail d = det;
        d.removeValue(QContactDetail__FieldProvenance);
        d.removeValue(QContactDetail__FieldModifiable);
        d.removeValue(QContactDetail__FieldNonexportable);
        ret.saveDetail(&d);
    }
    return ret;
}

}

class tst_replyparser : public QObject
{
    Q_OBJECT

public:
    tst_replyparser()
        : m_s(Q_NULLPTR, Q_NULLPTR, 7357)
        , m_rp(&m_s, &m_vcc) {}

public slots:
    void initTestCase();
    void cleanupTestCase();

private slots:
    void parseUserPrincipal_data();
    void parseUserPrincipal();

    void parseAddressbookHome_data();
    void parseAddressbookHome();

    void parseAddressbookInformation_data();
    void parseAddressbookInformation();

    void parseSyncTokenDelta_data();
    void parseSyncTokenDelta();

    void parseContactMetadata_data();
    void parseContactMetadata();

    void parseContactData_data();
    void parseContactData();

private:
    CardDavVCardConverter m_vcc;
    Syncer m_s;
    ReplyParser m_rp;
};

void tst_replyparser::initTestCase()
{
}

void tst_replyparser::cleanupTestCase()
{
}

void tst_replyparser::parseUserPrincipal_data()
{
    QTest::addColumn<QString>("xmlFilename");
    QTest::addColumn<QString>("expectedUserPrincipal");
    QTest::addColumn<int>("expectedResponseType");

    QTest::newRow("empty user information response")
        << QStringLiteral("data/replyparser_userprincipal_empty.xml")
        << QString()
        << static_cast<int>(ReplyParser::UserPrincipalResponse);

    QTest::newRow("single user principal in well-formed response")
        << QStringLiteral("data/replyparser_userprincipal_single-well-formed.xml")
        << QStringLiteral("/principals/users/johndoe/")
        << static_cast<int>(ReplyParser::UserPrincipalResponse);
}

void tst_replyparser::parseUserPrincipal()
{
    QFETCH(QString, xmlFilename);
    QFETCH(QString, expectedUserPrincipal);
    QFETCH(int, expectedResponseType);

    QFile f(QStringLiteral("%1/%2").arg(QCoreApplication::applicationDirPath(), xmlFilename));
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
        QFAIL("Data file does not exist or cannot be opened for reading!");
    }

    QByteArray userInformationResponse = f.readAll();
    ReplyParser::ResponseType responseType = ReplyParser::UserPrincipalResponse;
    QString userPrincipal = m_rp.parseUserPrincipal(userInformationResponse, &responseType);

    QCOMPARE(userPrincipal, expectedUserPrincipal);
    QCOMPARE(responseType, static_cast<ReplyParser::ResponseType>(expectedResponseType));
}

void tst_replyparser::parseAddressbookHome_data()
{
    QTest::addColumn<QString>("xmlFilename");
    QTest::addColumn<QString>("expectedAddressbooksHomePath");

    QTest::newRow("empty addressbook urls response")
        << QStringLiteral("data/replyparser_addressbookhome_empty.xml")
        << QString();

    QTest::newRow("single well-formed addressbook urls set response")
        << QStringLiteral("data/replyparser_addressbookhome_single-well-formed.xml")
        << QStringLiteral("/addressbooks/johndoe/");
}

void tst_replyparser::parseAddressbookHome()
{
    QFETCH(QString, xmlFilename);
    QFETCH(QString, expectedAddressbooksHomePath);

    QFile f(QStringLiteral("%1/%2").arg(QCoreApplication::applicationDirPath(), xmlFilename));
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
        QFAIL("Data file does not exist or cannot be opened for reading!");
    }

    QByteArray addressbookHomeSetResponse = f.readAll();
    QString addressbooksHomePath = m_rp.parseAddressbookHome(addressbookHomeSetResponse);

    QCOMPARE(addressbooksHomePath, expectedAddressbooksHomePath);
}

void tst_replyparser::parseAddressbookInformation_data()
{
    QTest::addColumn<QString>("xmlFilename");
    QTest::addColumn<QString>("addressbooksHomePath");
    QTest::addColumn<QList<ReplyParser::AddressBookInformation> >("expectedAddressbookInformation");

    QTest::newRow("empty addressbook information response")
        << QStringLiteral("data/replyparser_addressbookinformation_empty.xml")
        << QString()
        << QList<ReplyParser::AddressBookInformation>();

    QList<ReplyParser::AddressBookInformation> infos;
    ReplyParser::AddressBookInformation a;
    a.url = QStringLiteral("/addressbooks/johndoe/contacts/");
    a.displayName = QStringLiteral("My Address Book");
    a.ctag = QStringLiteral("3145");
    a.syncToken = QStringLiteral("http://sabredav.org/ns/sync-token/3145");
    infos << a;
    QTest::newRow("single addressbook information in well-formed response")
        << QStringLiteral("data/replyparser_addressbookinformation_single-well-formed.xml")
        << QStringLiteral("/addressbooks/johndoe/")
        << infos;

    infos.clear();
    ReplyParser::AddressBookInformation a2;
    a2.url = QStringLiteral("/addressbooks/johndoe/contacts/");
    a2.displayName = QStringLiteral("Contacts");
    a2.ctag = QStringLiteral("12345");
    a2.syncToken = QString();
    infos << a2;
    QTest::newRow("addressbook information in response including non-collection resources")
        << QStringLiteral("data/replyparser_addressbookinformation_addressbook-plus-contact.xml")
        << QStringLiteral("/addressbooks/johndoe/")
        << infos;

    infos.clear();
    ReplyParser::AddressBookInformation a3;
    a3.url = QStringLiteral("/dav/johndoe/contacts.vcf/");
    a3.displayName = QStringLiteral("Contacts");
    a3.ctag = QStringLiteral("22222");
    a3.syncToken = QString();
    infos << a3;
    QTest::newRow("addressbook information in response including principal and calendar collection")
        << QStringLiteral("data/replyparser_addressbookinformation_addressbook-calendar-principal.xml")
        << QStringLiteral("/dav/johndoe/")
        << infos;

    infos.clear(); // all of the contents should be ignored, since the addressbook-home-set path matches the addressbook href url.
    QTest::newRow("addressbook information in response including principal and calendar collection, discovery-case")
        << QStringLiteral("data/replyparser_addressbookinformation_addressbook-principal-proxy.xml")
        << QStringLiteral("/carddav")
        << infos;

    infos.clear();
    ReplyParser::AddressBookInformation a5;
    a5.url = QStringLiteral("/carddav");
    a5.displayName = QStringLiteral("Display Name");
    a5.ctag = QString();
    a5.syncToken = QString();
    infos << a5;
    QTest::newRow("addressbook information in response including principal and calendar collection, non-discovery-case")
        << QStringLiteral("data/replyparser_addressbookinformation_addressbook-principal-proxy.xml")
        << QString() // in the non-discovery case, the user provides the addressbook-home-set path directly.
        << infos;    // we then don't pass that into the parseAddressbookInformation() function, to avoid incorrect cycle detection.

    infos.clear();
    ReplyParser::AddressBookInformation a6;
    a6.url = QStringLiteral("/carddav/accountname@server.tld/addressbook/");
    a6.displayName = QStringLiteral("Display Name");
    a6.ctag = QStringLiteral("123456789");
    a6.syncToken = QString();
    infos << a6;
    QTest::newRow("addressbook information in response including home-set collections resource")
        << QStringLiteral("data/replyparser_addressbookinformation_addressbook-plus-collection-resource.xml")
        << QStringLiteral("/carddav/accountname%40server.tld/addressbook/")
        << infos;
}

bool operator==(const ReplyParser::AddressBookInformation& first, const ReplyParser::AddressBookInformation& second)
{
    return first.url == second.url
        && first.displayName == second.displayName
        && first.ctag == second.ctag
        && first.syncToken == second.syncToken;
}

void tst_replyparser::parseAddressbookInformation()
{
    QFETCH(QString, xmlFilename);
    QFETCH(QString, addressbooksHomePath);
    QFETCH(QList<ReplyParser::AddressBookInformation>, expectedAddressbookInformation);

    QFile f(QStringLiteral("%1/%2").arg(QCoreApplication::applicationDirPath(), xmlFilename));
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
        QFAIL("Data file does not exist or cannot be opened for reading!");
    }

    QByteArray addressbookInformationResponse = f.readAll();
    QList<ReplyParser::AddressBookInformation> addressbookInfo = m_rp.parseAddressbookInformation(addressbookInformationResponse, addressbooksHomePath);

    QCOMPARE(addressbookInfo.size(), expectedAddressbookInformation.size());
    QCOMPARE(addressbookInfo, expectedAddressbookInformation);
}

void tst_replyparser::parseSyncTokenDelta_data()
{
    QTest::addColumn<QString>("xmlFilename");
    QTest::addColumn<QHashStringString>("injectContactUrisEtags");
    QTest::addColumn<QString>("expectedNewSyncToken");
    QTest::addColumn<QList<ReplyParser::ContactInformation> >("expectedContactInformation");

    QList<ReplyParser::ContactInformation> infos;
    QTest::newRow("empty sync token delta response")
        << QStringLiteral("data/replyparser_synctokendelta_empty.xml")
        << QHash<QString, QString>()
        << QString()
        << infos;

    infos.clear();
    ReplyParser::ContactInformation c1;
    c1.modType = ReplyParser::ContactInformation::Addition;
    c1.uri = QStringLiteral("/addressbooks/johndoe/contacts/newcard.vcf");
    c1.etag = QStringLiteral("\"33441-34321\"");
    infos << c1;
    QTest::newRow("single contact addition in well-formed sync token delta response")
        << QStringLiteral("data/replyparser_synctokendelta_single-well-formed-addition.xml")
        << QHash<QString, QString>()
        << QString()
        << infos;

    infos.clear();
    ReplyParser::ContactInformation c2;
    c2.modType = ReplyParser::ContactInformation::Modification;
    c2.uri = QStringLiteral("/addressbooks/johndoe/contacts/updatedcard.vcf");
    c2.etag = QStringLiteral("\"33541-34696\"");
    ReplyParser::ContactInformation c3;
    c3.modType = ReplyParser::ContactInformation::Deletion;
    c3.uri = QStringLiteral("/addressbooks/johndoe/contacts/deletedcard.vcf");
    c3.etag = QString();
    infos << c1 << c2 << c3;
    QHash<QString, QString> mContactUrisEtags;
    mContactUrisEtags.insert(c2.uri, QStringLiteral("\"0001-0001\"")); // some previous etag.
    mContactUrisEtags.insert(c3.uri, c3.etag);
    QTest::newRow("single contact addition + modification + removal in well-formed sync token delta response")
        << QStringLiteral("data/replyparser_synctokendelta_single-well-formed-add-mod-rem.xml")
        << mContactUrisEtags
        << QStringLiteral("http://sabredav.org/ns/sync/5001")
        << infos;
}

bool operator==(const ReplyParser::ContactInformation& first, const ReplyParser::ContactInformation& second)
{
    return first.modType == second.modType
        && first.uri == second.uri
        && first.etag == second.etag;
}

void tst_replyparser::parseSyncTokenDelta()
{
    QFETCH(QString, xmlFilename);
    QFETCH(QHashStringString, injectContactUrisEtags);
    QFETCH(QString, expectedNewSyncToken);
    QFETCH(QList<ReplyParser::ContactInformation>, expectedContactInformation);

    QFile f(QStringLiteral("%1/%2").arg(QCoreApplication::applicationDirPath(), xmlFilename));
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
        QFAIL("Data file does not exist or cannot be opened for reading!");
    }

    const QString addressbookUrl = QStringLiteral("test/addressbook/path");
    m_s.m_localContactUrisEtags.insert(addressbookUrl, injectContactUrisEtags);

    QString newSyncToken;
    QByteArray syncTokenDeltaResponse = f.readAll();
    QList<ReplyParser::ContactInformation> contactInfo = m_rp.parseSyncTokenDelta(syncTokenDeltaResponse, addressbookUrl, &newSyncToken);

    QCOMPARE(newSyncToken, expectedNewSyncToken);
    QCOMPARE(contactInfo.size(), expectedContactInformation.size());
    if (contactInfo != expectedContactInformation) {
        for (int i = 0; i < contactInfo.size(); ++i) {
            if (!(contactInfo[i] == expectedContactInformation[i])) {
                qWarning() << "  actual:"
                           << contactInfo[i].modType
                           << contactInfo[i].uri
                           << contactInfo[i].etag;
                qWarning() << "expected:"
                           << expectedContactInformation[i].modType
                           << expectedContactInformation[i].uri
                           << expectedContactInformation[i].etag;
            }
        }
        QFAIL("contact information different");
    }

    m_s.m_localContactUrisEtags.clear();
}

void tst_replyparser::parseContactMetadata_data()
{
    QTest::addColumn<QString>("xmlFilename");
    QTest::addColumn<QString>("addressbookUrl");
    QTest::addColumn<QHashStringString>("injectContactEtags");
    QTest::addColumn<QList<ReplyParser::ContactInformation> >("expectedContactInformation");

    QList<ReplyParser::ContactInformation> infos;
    QTest::newRow("empty contact metadata response")
        << QStringLiteral("data/replyparser_contactmetadata_empty.xml")
        << QStringLiteral("/addressbooks/johndoe/contacts/")
        << QHash<QString, QString>()
        << infos;

    infos.clear();
    ReplyParser::ContactInformation c1;
    c1.modType = ReplyParser::ContactInformation::Addition;
    c1.uri = QStringLiteral("/addressbooks/johndoe/contacts/newcard.vcf");
    c1.etag = QStringLiteral("\"0001-0001\"");
    ReplyParser::ContactInformation c2;
    c2.modType = ReplyParser::ContactInformation::Modification;
    c2.uri = QStringLiteral("/addressbooks/johndoe/contacts/updatedcard.vcf");
    c2.etag = QStringLiteral("\"0002-0002\"");
    ReplyParser::ContactInformation c3;
    c3.modType = ReplyParser::ContactInformation::Deletion;
    c3.uri = QStringLiteral("/addressbooks/johndoe/contacts/deletedcard.vcf");
    c3.etag = QStringLiteral("\"0003-0001\"");
    ReplyParser::ContactInformation c4;
    c4.modType = ReplyParser::ContactInformation::Unmodified;
    c4.uri = QStringLiteral("/addressbooks/johndoe/contacts/unchangedcard.vcf");
    c4.etag = QStringLiteral("\"0004-0001\"");
    infos << c1 << c2 << c4 << c3;
    QHash<QString, QString> mContactEtags;
    mContactEtags.insert(c2.uri, QStringLiteral("\"0002-0001\"")); // changed to 0002-0002
    mContactEtags.insert(c3.uri, QStringLiteral("\"0003-0001\"")); // unchanged but deleted
    mContactEtags.insert(c4.uri, QStringLiteral("\"0004-0001\"")); // unchanged.
    QTest::newRow("single contact addition + modification + removal + unchanged in well-formed sync token delta response")
        << QStringLiteral("data/replyparser_contactmetadata_single-well-formed-add-mod-rem-unch.xml")
        << QStringLiteral("/addressbooks/johndoe/contacts/")
        << mContactEtags
        << infos;

    infos.clear();
    mContactEtags.clear();
    ReplyParser::ContactInformation c5;
    c5.modType = ReplyParser::ContactInformation::Addition;
    c5.uri = QStringLiteral("/addressbooks/johndoe/contacts/new.vcf");
    c5.etag = QStringLiteral("\"0021-0021\"");
    ReplyParser::ContactInformation c6;
    c6.modType = ReplyParser::ContactInformation::Addition;
    c6.uri = QStringLiteral("/addressbooks/johndoe/contacts/alsonew");
    c6.etag = QStringLiteral("\"0022-0022\"");
    infos << c5 << c6;
        QTest::newRow("two contact additions with vcf and non-vcf extenions in well-formed sync token delta response")
        << QStringLiteral("data/replyparser_contactmetadata_single-vcf-and-non-vcf.xml")
        << QStringLiteral("/addressbooks/johndoe/contacts/")
        << mContactEtags
        << infos;
}

void tst_replyparser::parseContactMetadata()
{
    QFETCH(QString, xmlFilename);
    QFETCH(QString, addressbookUrl);
    QFETCH(QHashStringString, injectContactEtags);
    QFETCH(QList<ReplyParser::ContactInformation>, expectedContactInformation);

    QFile f(QStringLiteral("%1/%2").arg(QCoreApplication::applicationDirPath(), xmlFilename));
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
        QFAIL("Data file does not exist or cannot be opened for reading!");
    }

    m_s.m_localContactUrisEtags.insert(addressbookUrl, injectContactEtags);

    QByteArray contactMetadataResponse = f.readAll();
    QList<ReplyParser::ContactInformation> contactInfo = m_rp.parseContactMetadata(contactMetadataResponse, addressbookUrl, injectContactEtags);

    QCOMPARE(contactInfo, expectedContactInformation);

    m_s.m_localContactUrisEtags.clear();
}

void tst_replyparser::parseContactData_data()
{
    QTest::addColumn<QString>("xmlFilename");
    QTest::addColumn<QString>("addressbookUrl");
    QTest::addColumn<QHashStringContact>("expectedContactInformation");

    QHash<QString, QContact> infos;
    QTest::newRow("empty contact data response")
        << QStringLiteral("data/replyparser_contactdata_empty.xml")
        << QStringLiteral("/addressbooks/johndoe/contacts/")
        << infos;

    infos.clear();
    QContact contact;
    QContactDisplayLabel cd;
    cd.setLabel(QStringLiteral("Testy Testperson"));
    QContactName cn;
    cn.setFirstName(QStringLiteral("Testy"));
    cn.setLastName(QStringLiteral("Testperson"));
    QContactPhoneNumber cp;
    cp.setNumber(QStringLiteral("555333111"));
    cp.setContexts(QList<int>() << QContactDetail::ContextHome);
    cp.setSubTypes(QList<int>() << QContactPhoneNumber::SubTypeMobile);
    QContactGuid cg;
    cg.setGuid(QStringLiteral("%1:AB:%2:%3").arg(QString::number(7357), QStringLiteral("/addressbooks/johndoe/contacts/"), QStringLiteral("testy-testperson-uid")));
    QContactSyncTarget cs;
    cs.setSyncTarget(QStringLiteral("/addressbooks/johndoe/contacts/testytestperson.vcf"));
    QContactExtendedDetail ce;
    ce.setName(KEY_ETAG);
    ce.setData(QStringLiteral("\"0001-0001\""));
    QContactExtendedDetail cu;
    cu.setName(KEY_UNSUPPORTEDPROPERTIES);
    cu.setData(QStringList() << QStringLiteral("X-UNSUPPORTED-TEST-PROPERTY:7357"));
    contact.saveDetail(&cd);
    contact.saveDetail(&cn);
    contact.saveDetail(&cp);
    contact.saveDetail(&cg);
    contact.saveDetail(&cs);
    contact.saveDetail(&ce);
    contact.saveDetail(&cu);
    infos.insert(QStringLiteral("/addressbooks/johndoe/contacts/testytestperson.vcf"), contact);
    QTest::newRow("single contact in well-formed contact data response")
        << QStringLiteral("data/replyparser_contactdata_single-well-formed.xml")
        << QStringLiteral("/addressbooks/johndoe/contacts/")
        << infos;

    cu.setData(QStringList());
    contact.saveDetail(&cu);

    QContactBirthday cb;
    cb.setDateTime(QDateTime(QDate(1990, 12, 31), QTime(2, 0, 0), Qt::UTC));
    cg.setGuid(QStringLiteral("%1:AB:%2:%3").arg(QString::number(7357),
                                                 QStringLiteral("/addressbooks/johndoe/contacts/"),
                                                 QStringLiteral("testy-testperson-uid-2")));
    cs.setSyncTarget(QStringLiteral("/addressbooks/johndoe/contacts/testytestperson2.vcf"));
    contact.saveDetail(&cs);
    contact.saveDetail(&cg);
    contact.saveDetail(&cb);
    infos.clear();
    infos.insert(QStringLiteral("/addressbooks/johndoe/contacts/testytestperson2.vcf"), contact);
    QTest::newRow("single contact with fully-specified, hyphen-separated UTC ISO8601 BDAY")
        << QStringLiteral("data/replyparser_contactdata_single-hs-utc-iso8601-bday.xml")
        << QStringLiteral("/addressbooks/johndoe/contacts/")
        << infos;

    cg.setGuid(QStringLiteral("%1:AB:%2:%3").arg(QString::number(7357),
                                                 QStringLiteral("/addressbooks/johndoe/contacts/"),
                                                 QStringLiteral("testy-testperson-uid-3")));
    cs.setSyncTarget(QStringLiteral("/addressbooks/johndoe/contacts/testytestperson3.vcf"));
    contact.saveDetail(&cs);
    contact.saveDetail(&cg);
    infos.clear();
    infos.insert(QStringLiteral("/addressbooks/johndoe/contacts/testytestperson3.vcf"), contact);
    QTest::newRow("single contact with fully-specified, non-separated UTC ISO8601 BDAY")
        << QStringLiteral("data/replyparser_contactdata_single-ns-utc-iso8601-bday.xml")
        << QStringLiteral("/addressbooks/johndoe/contacts/")
        << infos;

    cb.setDateTime(QDateTime(QDate(1990, 12, 31), QTime(2, 0, 0), Qt::LocalTime));
    cg.setGuid(QStringLiteral("%1:AB:%2:%3").arg(QString::number(7357),
                                                 QStringLiteral("/addressbooks/johndoe/contacts/"),
                                                 QStringLiteral("testy-testperson-uid-4")));
    cs.setSyncTarget(QStringLiteral("/addressbooks/johndoe/contacts/testytestperson4.vcf"));
    contact.saveDetail(&cs);
    contact.saveDetail(&cg);
    contact.saveDetail(&cb);
    infos.clear();
    infos.insert(QStringLiteral("/addressbooks/johndoe/contacts/testytestperson4.vcf"), contact);
    QTest::newRow("single contact with fully-specified, hyphen-separated no-tz ISO8601 BDAY")
        << QStringLiteral("data/replyparser_contactdata_single-hs-notz-iso8601-bday.xml")
        << QStringLiteral("/addressbooks/johndoe/contacts/")
        << infos;

    cg.setGuid(QStringLiteral("%1:AB:%2:%3").arg(QString::number(7357),
                                                 QStringLiteral("/addressbooks/johndoe/contacts/"),
                                                 QStringLiteral("testy-testperson-uid-5")));
    cs.setSyncTarget(QStringLiteral("/addressbooks/johndoe/contacts/testytestperson5.vcf"));
    contact.saveDetail(&cs);
    contact.saveDetail(&cg);
    infos.clear();
    infos.insert(QStringLiteral("/addressbooks/johndoe/contacts/testytestperson5.vcf"), contact);
    QTest::newRow("single contact with fully-specified, non-separated no-tz ISO8601 BDAY")
        << QStringLiteral("data/replyparser_contactdata_single-ns-notz-iso8601-bday.xml")
        << QStringLiteral("/addressbooks/johndoe/contacts/")
        << infos;

    cb.setDate(QDate(1990, 12, 31));
    cg.setGuid(QStringLiteral("%1:AB:%2:%3").arg(QString::number(7357),
                                                 QStringLiteral("/addressbooks/johndoe/contacts/"),
                                                 QStringLiteral("testy-testperson-uid-6")));
    cs.setSyncTarget(QStringLiteral("/addressbooks/johndoe/contacts/testytestperson6.vcf"));
    contact.saveDetail(&cs);
    contact.saveDetail(&cg);
    contact.saveDetail(&cb);
    infos.clear();
    infos.insert(QStringLiteral("/addressbooks/johndoe/contacts/testytestperson6.vcf"), contact);
    QTest::newRow("single contact with non-separated, date-only ISO8601 BDAY")
        << QStringLiteral("data/replyparser_contactdata_single-ns-do-iso8601-bday.xml")
        << QStringLiteral("/addressbooks/johndoe/contacts/")
        << infos;

    cg.setGuid(QStringLiteral("%1:AB:%2:%3").arg(QString::number(7357),
                                                 QStringLiteral("/addressbooks/johndoe/contacts/"),
                                                 QStringLiteral("testy-testperson-uid-7")));
    cs.setSyncTarget(QStringLiteral("/addressbooks/johndoe/contacts/testytestperson7.vcf"));
    contact.saveDetail(&cs);
    contact.saveDetail(&cg);
    infos.clear();
    infos.insert(QStringLiteral("/addressbooks/johndoe/contacts/testytestperson7.vcf"), contact);
    QTest::newRow("single contact with multiple non-separated, date-only ISO8601 BDAY fields")
        << QStringLiteral("data/replyparser_contactdata_single-ns-do-iso8601-bday-multiple.xml")
        << QStringLiteral("/addressbooks/johndoe/contacts/")
        << infos;

    cg.setGuid(QStringLiteral("%1:AB:%2:%3").arg(QString::number(7357),
                                                 QStringLiteral("/addressbooks/johndoe/contacts/"),
                                                 QStringLiteral("testy-testperson-uid-8")));
    cs.setSyncTarget(QStringLiteral("/addressbooks/johndoe/contacts/testytestperson8.vcf"));
    contact.saveDetail(&cs);
    contact.saveDetail(&cg);
    infos.clear();
    infos.insert(QStringLiteral("/addressbooks/johndoe/contacts/testytestperson8.vcf"), contact);
    QTest::newRow("single contact with multiple FN fields")
        << QStringLiteral("data/replyparser_contactdata_single-contact-multiple-formattedname.xml")
        << QStringLiteral("/addressbooks/johndoe/contacts/")
        << infos;

    cg.setGuid(QStringLiteral("%1:AB:%2:%3").arg(QString::number(7357),
                                                 QStringLiteral("/addressbooks/johndoe/contacts/"),
                                                 QStringLiteral("testy-testperson-uid-9")));
    cs.setSyncTarget(QStringLiteral("/addressbooks/johndoe/contacts/testytestperson9.vcf"));
    contact.saveDetail(&cs);
    contact.saveDetail(&cg);
    infos.clear();
    infos.insert(QStringLiteral("/addressbooks/johndoe/contacts/testytestperson9.vcf"), contact);
    QTest::newRow("single contact with multiple N fields")
        << QStringLiteral("data/replyparser_contactdata_single-contact-multiple-name.xml")
        << QStringLiteral("/addressbooks/johndoe/contacts/")
        << infos;

    QContactGender cgender;
    cgender.setGender(QContactGender::GenderFemale);
    cg.setGuid(QStringLiteral("%1:AB:%2:%3").arg(QString::number(7357),
                                                 QStringLiteral("/addressbooks/johndoe/contacts/"),
                                                 QStringLiteral("testy-testperson-uid-12")));
    cs.setSyncTarget(QStringLiteral("/addressbooks/johndoe/contacts/testytestperson12.vcf"));
    contact.saveDetail(&cs);
    contact.saveDetail(&cg);
    contact.saveDetail(&cgender);
    infos.clear();
    infos.insert(QStringLiteral("/addressbooks/johndoe/contacts/testytestperson12.vcf"), contact);
    QTest::newRow("single contact with multiple X-GENDER fields")
        << QStringLiteral("data/replyparser_contactdata_single-contact-multiple-xgender.xml")
        << QStringLiteral("/addressbooks/johndoe/contacts/")
        << infos;

    QContactTimestamp ct;
    ct.setLastModified(QDateTime::fromString(QStringLiteral("1996-10-31T22:27:10Z"), Qt::ISODate));
    cg.setGuid(QStringLiteral("%1:AB:%2:%3").arg(QString::number(7357),
                                                 QStringLiteral("/addressbooks/johndoe/contacts/"),
                                                 QStringLiteral("testy-testperson-uid-11")));
    cs.setSyncTarget(QStringLiteral("/addressbooks/johndoe/contacts/testytestperson11.vcf"));
    contact.saveDetail(&cs);
    contact.saveDetail(&cg);
    contact.saveDetail(&ct);
    infos.clear();
    infos.insert(QStringLiteral("/addressbooks/johndoe/contacts/testytestperson11.vcf"), contact);
    QTest::newRow("single contact with multiple REV fields")
        << QStringLiteral("data/replyparser_contactdata_single-contact-multiple-rev.xml")
        << QStringLiteral("/addressbooks/johndoe/contacts/")
        << infos;

    ct.setLastModified(QDateTime::fromString(QStringLiteral("1995-10-31T22:27:10Z"), Qt::ISODate));
    cg.setGuid(QStringLiteral("%1:AB:%2:%3").arg(QString::number(7357),
                                                 QStringLiteral("/addressbooks/johndoe/contacts/"),
                                                 QStringLiteral("testy-testperson-uid-10")));
    cs.setSyncTarget(QStringLiteral("/addressbooks/johndoe/contacts/testytestperson10.vcf"));
    contact.saveDetail(&cs);
    contact.saveDetail(&cg);
    contact.saveDetail(&ct);
    infos.clear();
    infos.insert(QStringLiteral("/addressbooks/johndoe/contacts/testytestperson10.vcf"), contact);
    QTest::newRow("single contact with multiple UID fields")
        << QStringLiteral("data/replyparser_contactdata_single-contact-multiple-uid.xml")
        << QStringLiteral("/addressbooks/johndoe/contacts/")
        << infos;
}

bool operator==(const ReplyParser::FullContactInformation& first, const ReplyParser::FullContactInformation& second)
{
    return first.unsupportedProperties == second.unsupportedProperties
        && first.etag == second.etag
        && first.contact == second.contact;
}

QContactExtendedDetail unsupportedPropertiesDetail(const QContact &contact)
{
    for (const QContactExtendedDetail &d : contact.details<QContactExtendedDetail>()) {
        if (d.name() == KEY_UNSUPPORTEDPROPERTIES) {
            return d;
        }
    }
    return QContactExtendedDetail();
}

QContactExtendedDetail etagDetail(const QContact &contact)
{
    for (const QContactExtendedDetail &d : contact.details<QContactExtendedDetail>()) {
        if (d.name() == KEY_ETAG) {
            return d;
        }
    }
    return QContactExtendedDetail();
}

void tst_replyparser::parseContactData()
{
    QFETCH(QString, xmlFilename);
    QFETCH(QString, addressbookUrl);
    QFETCH(QHashStringContact, expectedContactInformation);

    QFile f(QStringLiteral("%1/%2").arg(QCoreApplication::applicationDirPath(), xmlFilename));
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
        QFAIL("Data file does not exist or cannot be opened for reading!");
    }

    QByteArray contactDataResponse = f.readAll();
    QHash<QString, QContact> contactInfo = m_rp.parseContactData(contactDataResponse, addressbookUrl);

    QCOMPARE(contactInfo.size(), expectedContactInformation.size());
    QCOMPARE(contactInfo.keys(), expectedContactInformation.keys());
    Q_FOREACH (const QString &contactUri, contactInfo.keys()) {
        QCOMPARE(unsupportedPropertiesDetail(contactInfo[contactUri]).data(), unsupportedPropertiesDetail(expectedContactInformation[contactUri]).data());
        QCOMPARE(etagDetail(contactInfo[contactUri]).data(), etagDetail(expectedContactInformation[contactUri]).data());
        bool identical = false;
        QContact actualContact = removeIgnorableFields(contactInfo[contactUri]);
        QContact expectedContact = removeIgnorableFields(expectedContactInformation[contactUri]);
        QContact resolved = m_s.resolveConflictingChanges(actualContact, expectedContact, &identical);
        if (!identical) {
            qWarning() << "  actual:";
            dumpContact(actualContact);
            qWarning() << " resolved:";
            dumpContact(resolved);
            qWarning() << " expected:";
            dumpContact(expectedContact);
        }
        QVERIFY(identical);

        // explicitly test for multiples of unique details
        QVERIFY(contactInfo[contactUri].details<QContactTimestamp>().size() <= 1);
        QVERIFY(contactInfo[contactUri].details<QContactGender>().size() <= 1);
        QVERIFY(contactInfo[contactUri].details<QContactBirthday>().size() <= 1);
        QVERIFY(contactInfo[contactUri].details<QContactDisplayLabel>().size() <= 1);
        QVERIFY(contactInfo[contactUri].details<QContactName>().size() <= 1);
        QVERIFY(contactInfo[contactUri].details<QContactGuid>().size() <= 1);
    }
}

#include "tst_replyparser.moc"
QTEST_MAIN(tst_replyparser)
