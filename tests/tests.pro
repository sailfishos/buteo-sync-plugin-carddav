TEMPLATE=subdirs
SUBDIRS+=replyparser

OTHER_FILES+=tests.xml
tests_xml.path=/opt/tests/buteo/plugins/carddav/
tests_xml.files=tests.xml
INSTALLS+=tests_xml
