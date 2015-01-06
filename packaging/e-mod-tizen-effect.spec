%bcond_with x

Name: e-mod-tizen-effect
Version: 0.0.1
Release: 1
Summary: The effect module for the enlightenment
URL: http://www.enlightenment.org
Group: Graphics & UI Framework/Other
Source0: %{name}-%{version}.tar.gz
License: BSD-2-Clause
BuildRequires: pkgconfig(enlightenment)
BuildRequires: pkgconfig(eina)
BuildRequires: pkgconfig(ecore)
BuildRequires: pkgconfig(edje)
BuildRequires:  gettext
BuildRequires:  edje-tools

%if !%{with x}
ExclusiveArch:
%endif

%description
This package provides various window effect(animation)
as one module of enlightenment.

%prep
%setup -q

%build

export GC_SECTIONS_FLAGS="-fdata-sections -ffunction-sections -Wl,--gc-sections"
export CFLAGS+=" -Wall -g -fPIC -rdynamic ${GC_SECTIONS_FLAGS}"
export LDFLAGS+=" -Wl,--hash-style=both -Wl,--as-needed -Wl,--rpath=/usr/lib"

%autogen
%configure --prefix=/usr
make

%install
rm -rf %{buildroot}

# for license notification
#mkdir -p %{buildroot}/usr/share/license
#cp -a %{_builddir}/%{buildsubdir}/COPYING %{buildroot}/usr/share/license/%{name}

# install
make install DESTDIR=%{buildroot}

# clear useless textual files
find  %{buildroot}%{_libdir}/enlightenment/modules/%{name} -name *.la | xargs rm


%files
%defattr(-,root,root,-)
%{_libdir}/enlightenment/modules/e-mod-tizen-effect
%{_datadir}/enlightenment/data/themes
#/usr/share/license/%{name}

