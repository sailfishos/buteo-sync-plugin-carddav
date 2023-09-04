QT       -= gui
QT       += network dbus

CONFIG += link_pkgconfig console c++11
PKGCONFIG += buteosyncfw$${QT_MAJOR_VERSION} libsignon-qt$${QT_MAJOR_VERSION} accounts-qt$${QT_MAJOR_VERSION}
PKGCONFIG += Qt$${QT_MAJOR_VERSION}Versit Qt$${QT_MAJOR_VERSION}Contacts qtcontacts-sqlite-qt$${QT_MAJOR_VERSION}-extensions

packagesExist(libsailfishkeyprovider) {
    PKGCONFIG += libsailfishkeyprovider
    DEFINES += USE_SAILFISHKEYPROVIDER
}

packagesExist(contactcache-qt$${QT_MAJOR_VERSION}) {
    PKGCONFIG += contactcache-qt$${QT_MAJOR_VERSION}
    DEFINES += USE_LIBCONTACTS
}

# We need the moc output for the headers from sqlite-extensions
extensionsIncludePath = $$system(pkg-config --cflags-only-I qtcontacts-sqlite-qt$${QT_MAJOR_VERSION}-extensions)
VPATH += $$replace(extensionsIncludePath, -I, )
HEADERS += qcontactclearchangeflagsrequest.h contactmanagerengine.h

INCLUDEPATH += $$PWD

SOURCES += \
    $$PWD/carddavclient.cpp \
    $$PWD/syncer.cpp \
    $$PWD/auth.cpp \
    $$PWD/carddav.cpp \
    $$PWD/requestgenerator.cpp \
    $$PWD/replyparser.cpp \
    $$PWD/logging.cpp

HEADERS += \
    $$EXTENSION_HEADERS \
    $$PWD/carddavclient.h \
    $$PWD/syncer_p.h \
    $$PWD/auth_p.h \
    $$PWD/carddav_p.h \
    $$PWD/requestgenerator_p.h \
    $$PWD/replyparser_p.h \
    $$PWD/logging.h \

OTHER_FILES += \
    $$PWD/carddav.xml \
    $$PWD/carddav.Contacts.xml
