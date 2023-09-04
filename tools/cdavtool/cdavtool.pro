TEMPLATE=app
TARGET=cdavtool
QT-=gui
QT+=network

CONFIG += link_pkgconfig console
PKGCONFIG += buteosyncfw$${QT_MAJOR_VERSION}
PKGCONFIG += libsignon-qt$${QT_MAJOR_VERSION} accounts-qt$${QT_MAJOR_VERSION}
PKGCONFIG += Qt$${QT_MAJOR_VERSION}Contacts Qt$${QT_MAJOR_VERSION}Versit contactcache-qt$${QT_MAJOR_VERSION}

QMAKE_CXXFLAGS += -fPIE -fvisibility=hidden -fvisibility-inlines-hidden

HEADERS+=worker.h helpers.h
SOURCES+=worker.cpp helpers.cpp main.cpp

# included from the main carddav plugin
include($$PWD/../../src/src.pri)

target.path = $$INSTALL_ROOT/opt/tests/buteo/plugins/carddav/
INSTALLS+=target
