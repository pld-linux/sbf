Summary:	Simple boot flag support utility
Name:		sbf
Version:	0.3
Release:	1
License:	GPL
Group:		Applications/System
Source0:	http://www.codemonkey.org.uk/cruft/sbf.c
# Source0-md5:	f7d07ddf99e203857bed52b71d9a01be
BuildRoot:	%{tmpdir}/%{name}-%{version}-root-%(id -u -n)

%description
The SBF specification is an x86 BIOS extension that allows improved
system boot speeds. It does this by marking a CMOS field to say
"I booted okay, skip extensive POST next reboot".

http://www.microsoft.com/hwdev/resources/specs/simp_bios.asp

%prep
%setup -q -c -T

%build
%{__cc} %{rpmcflags} %{rpmldflags} %{SOURCE0} -o sbf

%install
rm -rf $RPM_BUILD_ROOT
install -d $RPM_BUILD_ROOT%{_sbindir}

install sbf $RPM_BUILD_ROOT%{_sbindir}

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(644,root,root,755)
%attr(755,root,root) %{_sbindir}/*
