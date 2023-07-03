Name: display-kernel-headers
Version: 1.0
Release: r0
Summary: display-driver
BuildArch: noarch
License: GPL-2.0-only WITH Linux-syscall-note
URL: http://support.cdmatech.com
Source0: %{name}-%{version}.tar.gz

%{!?kversion: %define kversion %(uname -r)}

BuildRequires: automake autoconf
BuildRequires: kernel-automotive-devel-uname-r = %{kversion}

%description
Provide display drivers

%global debug_package %{nil}

%prep
%setup -qn %{name}

KERNEL_SRC="/usr/src/kernels"
CURDIR=${PWD}
cd ${KERNEL_SRC}/%{kversion}/
scripts/headers_install.sh ${CURDIR}/include/uapi/display/drm/msm_drm_pp.h ${CURDIR}/include/uapi/msm_drm_pp.h
scripts/headers_install.sh ${CURDIR}/include/uapi/display/drm/sde_drm.h ${CURDIR}/include/uapi/sde_drm.h
scripts/headers_install.sh ${CURDIR}/include/uapi/display/media/mmm_color_fmt.h ${CURDIR}/include/uapi/mmm_color_fmt.h
scripts/headers_install.sh ${CURDIR}/include/uapi/display/media/msm_sde_rotator.h ${CURDIR}/include/uapi/msm_sde_rotator.h


%install
mkdir -p %{buildroot}%{_includedir}
mkdir -p %{buildroot}%{_includedir}/drm
mkdir -p %{buildroot}%{_includedir}/media
cp include/uapi/display/drm/msm_drm_pp.h %{buildroot}%{_includedir}/drm
cp include/uapi/display/drm/sde_drm.h %{buildroot}%{_includedir}/drm
cp include/uapi/display/media/mmm_color_fmt.h %{buildroot}%{_includedir}/media
cp include/uapi/display/media/msm_sde_rotator.h %{buildroot}%{_includedir}/media


%files
%{_includedir}/drm/msm_drm_pp.h
%{_includedir}/drm/sde_drm.h
%{_includedir}/media/mmm_color_fmt.h
%{_includedir}/media/msm_sde_rotator.h
