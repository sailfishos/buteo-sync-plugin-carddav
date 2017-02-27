TEMPLATE=subdirs
SUBDIRS=src tests tools
tests.depends=src
tools.depends=src
OTHER_FILES+=rpm/buteo-sync-plugin-carddav.spec
