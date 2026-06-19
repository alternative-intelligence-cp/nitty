Name:           nitty
Version:        0.14.0
Release:        1%{?dist}
Summary:        A modern, fast, and feature-rich terminal emulator

License:        AGPL-3.0-only
URL:            https://github.com/altintel/nitty
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gtk4-devel >= 4.6
BuildRequires:  libssh2-devel >= 1.10
BuildRequires:  openssl-devel
BuildRequires:  make
# Assumes npkc and npkbld are installed on the build system
# BuildRequires:  npkc

Requires:       gtk4 >= 4.6
Requires:       libssh2 >= 1.10

%description
Nitty is a terminal emulator built from the ground up for power users, written in
the Nitpick programming language. It combines standard local shell capabilities
with advanced features like integrated SSH connection management, a built-in
encrypted credential vault, SFTP browsing, native serial port and telnet support,
and a flexible plugin system.

%prep
%autosetup

%build
npkbld build --release

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/%{_bindir}
mkdir -p $RPM_BUILD_ROOT/%{_datadir}/applications
mkdir -p $RPM_BUILD_ROOT/%{_datadir}/metainfo
mkdir -p $RPM_BUILD_ROOT/%{_mandir}/man1

install -m 0755 .nitpick_make/build/nitty $RPM_BUILD_ROOT/%{_bindir}/
install -m 0644 packaging/nitty.desktop $RPM_BUILD_ROOT/%{_datadir}/applications/
install -m 0644 packaging/nitty.metainfo.xml $RPM_BUILD_ROOT/%{_datadir}/metainfo/
install -m 0644 docs/nitty.1 $RPM_BUILD_ROOT/%{_mandir}/man1/

# Install icons
for size in 16 24 32 48 64 128 256 512; do
  mkdir -p $RPM_BUILD_ROOT/%{_datadir}/icons/hicolor/${size}x${size}/apps
  install -m 0644 packaging/icons/nitty-${size}x${size}.png $RPM_BUILD_ROOT/%{_datadir}/icons/hicolor/${size}x${size}/apps/nitty.png
done

%post
/bin/touch --no-create %{_datadir}/icons/hicolor &>/dev/null || :

%postun
if [ $1 -eq 0 ] ; then
    /bin/touch --no-create %{_datadir}/icons/hicolor &>/dev/null
    /usr/bin/gtk-update-icon-cache -f %{_datadir}/icons/hicolor &>/dev/null || :
fi

%posttrans
/usr/bin/gtk-update-icon-cache -f %{_datadir}/icons/hicolor &>/dev/null || :

%files
%license packaging/deb/copyright
%{_bindir}/nitty
%{_datadir}/applications/nitty.desktop
%{_datadir}/metainfo/nitty.metainfo.xml
%{_datadir}/icons/hicolor/*/apps/nitty.png
%{_mandir}/man1/nitty.1*

%changelog
* Fri Jun 19 2026 Nitty Team <maintainers@nitty.example.com> - 0.14.0-1
- Initial RPM package release
