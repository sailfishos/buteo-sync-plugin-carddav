QT       -= gui
QT       += network dbus

CONFIG(qt-contacts-private) {
    QT += contacts-private
}

CONFIG += link_pkgconfig console c++11
PKGCONFIG += buteosyncfw5 libsignon-qt5 accounts-qt5
PKGCONFIG += Qt5Versit Qt5Contacts qtcontacts-sqlite-qt5-extensions

packagesExist(libsailfishkeyprovider) {
    PKGCONFIG += libsailfishkeyprovider
    DEFINES += USE_SAILFISHKEYPROVIDER
}

packagesExist(contactcache-qt5) {
    PKGCONFIG += contactcache-qt5
    DEFINES += USE_LIBCONTACTS
}

INCLUDEPATH += $$PWD

SOURCES += \
    $$PWD/carddavclient.cpp \
    $$PWD/syncer.cpp \
    $$PWD/auth.cpp \
    $$PWD/carddav.cpp \
    $$PWD/requestgenerator.cpp \
    $$PWD/replyparser.cpp

HEADERS += \
    $$PWD/carddavclient.h \
    $$PWD/syncer_p.h \
    $$PWD/auth_p.h \
    $$PWD/carddav_p.h \
    $$PWD/requestgenerator_p.h \
    $$PWD/replyparser_p.h

OTHER_FILES += \
    $$PWD/carddav.xml \
    $$PWD/carddav.Contacts.xml
