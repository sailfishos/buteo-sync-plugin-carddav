Name:       buteo-sync-plugin-carddav
Summary:    Syncs contact data from CardDAV services
Version:    0.0.24
Release:    1
Group:      System/Libraries
License:    LGPLv2.1
URL:        https://github.com/nemomobile/buteo-sync-plugin-carddav
Source0:    %{name}-%{version}.tar.bz2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Gui)
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  pkgconfig(Qt5Sql)
BuildRequires:  pkgconfig(Qt5Network)
BuildRequires:  pkgconfig(Qt5Contacts)
BuildRequires:  pkgconfig(Qt5Versit)
BuildRequires:  pkgconfig(mlite5)
BuildRequires:  pkgconfig(buteosyncfw5)
BuildRequires:  pkgconfig(accounts-qt5) >= 1.13
BuildRequires:  pkgconfig(libsignon-qt5)
BuildRequires:  pkgconfig(libsailfishkeyprovider)
BuildRequires:  pkgconfig(qtcontacts-sqlite-qt5-extensions) >= 0.2.18
BuildRequires:  pkgconfig(contactcache-qt5) >= 0.1.5
Requires: buteo-syncfw-qt5-msyncd

%description
A Buteo plugin which syncs contact data from CardDAV services

%package tests
Summary:    Unit tests for buteo-sync-plugin-carddav
Group:      System/Libraries
BuildRequires:  pkgconfig(Qt5Test)
Requires:   blts-tools
Requires:   %{name} = %{version}-%{release}

%description tests
This package contains unit tests for the CardDAV Buteo sync plugin.

%files
%defattr(-,root,root,-)
#out-of-process-plugin
/usr/lib/buteo-plugins-qt5/oopp/carddav-client
#in-process-plugin
#/usr/lib/buteo-plugins-qt5/libcarddav-client.so
%config %{_sysconfdir}/buteo/profiles/client/carddav.xml
%config %{_sysconfdir}/buteo/profiles/sync/carddav.Contacts.xml

%files tests
%defattr(-,root,root,-)
/opt/tests/buteo/plugins/carddav/tests.xml
/opt/tests/buteo/plugins/carddav/tst_replyparser
/opt/tests/buteo/plugins/carddav/data/replyparser_userprincipal_empty.xml
/opt/tests/buteo/plugins/carddav/data/replyparser_userprincipal_single-well-formed.xml
/opt/tests/buteo/plugins/carddav/data/replyparser_addressbookhome_empty.xml
/opt/tests/buteo/plugins/carddav/data/replyparser_addressbookhome_single-well-formed.xml
/opt/tests/buteo/plugins/carddav/data/replyparser_addressbookinformation_empty.xml
/opt/tests/buteo/plugins/carddav/data/replyparser_addressbookinformation_single-well-formed.xml
/opt/tests/buteo/plugins/carddav/data/replyparser_synctokendelta_empty.xml
/opt/tests/buteo/plugins/carddav/data/replyparser_synctokendelta_single-well-formed-add-mod-rem.xml
/opt/tests/buteo/plugins/carddav/data/replyparser_synctokendelta_single-well-formed-addition.xml
/opt/tests/buteo/plugins/carddav/data/replyparser_contactmetadata_empty.xml
/opt/tests/buteo/plugins/carddav/data/replyparser_contactmetadata_single-well-formed-add-mod-rem-unch.xml
/opt/tests/buteo/plugins/carddav/data/replyparser_contactdata_empty.xml
/opt/tests/buteo/plugins/carddav/data/replyparser_contactdata_single-well-formed.xml

%prep
%setup -q -n %{name}-%{version}

%build
%qmake5 "DEFINES+=BUTEO_OUT_OF_PROCESS_SUPPORT"
make %{?jobs:-j%jobs}

%pre
rm -f /home/nemo/.cache/msyncd/sync/client/carddav.xml
rm -f /home/nemo/.cache/msyncd/sync/carddav.Contacts.xml

%install
rm -rf %{buildroot}
%qmake5_install

%post
systemctl-user try-restart msyncd.service || :
