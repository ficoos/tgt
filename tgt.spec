Name: tgt
Version: 0.5.0
Release: 1%{?dist}
Summary: A Tiny Gnome Terminal
License: GPLv3

URL: https://github.com/ficoos/tgt
VCS: {{{ git_dir_vcs }}}

Source: {{{ git_dir_pack }}}

BuildRequires: meson
BuildRequires: gcc
BuildRequires: pkgconfig(gtk+-3.0)
BuildRequires: pkgconfig(vte-2.91)
BuildRequires: pkgconfig(glib-2.0)

Requires: gtk3
Requires: vte291

%description

%prep
%autosetup

%build
%meson
%meson_build

%install
%meson_install

%check
%meson_test

%files
%{_bindir}/tgt


%changelog
* Mon Jun 08 2020 meson <meson@example.com> - 
- Initial Release

