TEMPLATE=subdirs
SUBDIRS=src tests

CONFIG(build-tools) {
    SUBDIRS += tools
    tools.depends=src
}

tests.depends=src
OTHER_FILES+=rpm/buteo-sync-plugin-carddav.spec
