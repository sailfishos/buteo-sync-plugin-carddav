TEMPLATE = app
TARGET = tst_replyparser
include($$PWD/../../src/src.pri)
QT += testlib
SOURCES += tst_replyparser.cpp
OTHER_FILES += data/*xml
datafiles.files += data/*xml
datafiles.path = /opt/tests/buteo/plugins/carddav/data/
target.path = /opt/tests/buteo/plugins/carddav/
INSTALLS += target datafiles
