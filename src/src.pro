TARGET    = carddav-client

include(src.pri)

QMAKE_CXXFLAGS = -Wall \
    -g \
    -Wno-cast-align \
    -O2 -finline-functions

TEMPLATE = lib
CONFIG += plugin
target.path = $$[QT_INSTALL_LIBS]/buteo-plugins-qt$${QT_MAJOR_VERSION}/oopp

sync.path = /etc/buteo/profiles/sync
sync.files = carddav.Contacts.xml

client.path = /etc/buteo/profiles/client
client.files = carddav.xml

INSTALLS += target sync client
