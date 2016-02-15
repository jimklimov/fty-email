#
#    agent-smtp - XXXX
#
#    Copyright (C) 2014 - 2015 Eaton                                        
#                                                                           
#    This program is free software; you can redistribute it and/or modify   
#    it under the terms of the GNU General Public License as published by   
#    the Free Software Foundation; either version 2 of the License, or      
#    (at your option) any later version.                                    
#                                                                           
#    This program is distributed in the hope that it will be useful,        
#    but WITHOUT ANY WARRANTY; without even the implied warranty of         
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          
#    GNU General Public License for more details.                           
#                                                                           
#    You should have received a copy of the GNU General Public License along
#    with this program; if not, write to the Free Software Foundation, Inc.,
#    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.            
#

Name:           agent-smtp
Version:        0.1.0
Release:        1
Summary:        xxxx
License:        GPL-2.0+
URL:            https://eaton.com/
Source0:        %{name}-%{version}.tar.gz
Group:          System/Libraries
BuildRequires:  automake
BuildRequires:  autoconf
BuildRequires:  libtool
BuildRequires:  pkg-config
BuildRequires:  systemd-devel
BuildRequires:  gcc-c++
BuildRequires:  libsodium-devel
BuildRequires:  zeromq-devel
BuildRequires:  uuid-devel
BuildRequires:  czmq-devel
BuildRequires:  malamute-devel
BuildRequires:  biosproto-devel
BuildRequires:  cxxtools-devel
BuildRoot:      %{_tmppath}/%{name}-%{version}-build

%description
agent-smtp xxxx.

%package -n libagent_smtp0
Group:          System/Libraries
Summary:        xxxx

%description -n libagent_smtp0
agent-smtp xxxx.
This package contains shared library.

%post -n libagent_smtp0 -p /sbin/ldconfig
%postun -n libagent_smtp0 -p /sbin/ldconfig

%files -n libagent_smtp0
%defattr(-,root,root)
%{_libdir}/libagent_smtp.so.*

%package devel
Summary:        xxxx
Group:          System/Libraries
Requires:       libagent_smtp0 = %{version}
Requires:       libsodium-devel
Requires:       zeromq-devel
Requires:       uuid-devel
Requires:       czmq-devel
Requires:       malamute-devel
Requires:       biosproto-devel
Requires:       cxxtools-devel

%description devel
agent-smtp xxxx.
This package contains development files.

%files devel
%defattr(-,root,root)
%{_includedir}/*
%{_libdir}/libagent_smtp.so
%{_libdir}/pkgconfig/libagent_smtp.pc

%prep
%setup -q

%build
sh autogen.sh
%{configure} --with-systemd
make %{_smp_mflags}

%install
make install DESTDIR=%{buildroot} %{?_smp_mflags}

# remove static libraries
find %{buildroot} -name '*.a' | xargs rm -f
find %{buildroot} -name '*.la' | xargs rm -f

%files
%defattr(-,root,root)
%{_bindir}/bios-agent-smtp
%{_prefix}/lib/systemd/system/bios-agent-smtp*.service
%{_prefix}/lib/tmpfiles.d/*.conf


%changelog
