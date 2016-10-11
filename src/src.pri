QT       -= gui
QT       += network dbus

CONFIG += link_pkgconfig console
PKGCONFIG += buteosyncfw5 libsignon-qt5 accounts-qt5 libsailfishkeyprovider
PKGCONFIG += Qt5Versit Qt5Contacts qtcontacts-sqlite-qt5-extensions contactcache-qt5
QT += contacts-private

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
