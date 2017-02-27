TEMPLATE=app
TARGET=cdavtool
QT-=gui
QT+=network

CONFIG += link_pkgconfig console
PKGCONFIG += buteosyncfw5
PKGCONFIG += libsignon-qt5 accounts-qt5
PKGCONFIG += libkcalcoren-qt5 libmkcal-qt5
PKGCONFIG += Qt5Contacts Qt5Versit contactcache-qt5

QMAKE_CXXFLAGS += -fPIE -fvisibility=hidden -fvisibility-inlines-hidden

HEADERS+=worker.h helpers.h
SOURCES+=worker.cpp helpers.cpp main.cpp

# included from the main carddav plugin
include($$PWD/../../src/src.pri)

target.path = $$INSTALL_ROOT/opt/tests/buteo/plugins/carddav/
INSTALLS+=target
