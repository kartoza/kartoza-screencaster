Name:           kartoza-screencaster
Version:        0.7.6
Release:        1%{?dist}
Summary:        Screen recording tool for Wayland

License:        MIT
URL:            https://github.com/kartoza/kartoza-screencaster
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  golang >= 1.21
BuildRequires:  git
Requires:       wl-screenrec
Requires:       ffmpeg
Requires:       pipewire
Requires:       libnotify

%description
A screen recording tool for Wayland compositors (Hyprland, Sway, etc.)
with multi-monitor support, audio processing, and webcam integration.

Features:
- Multi-monitor screen recording with cursor detection
- Separate audio recording with noise reduction
- Webcam recording and vertical video creation
- Audio normalization using EBU R128 standards
- Hardware and software video encoding

%prep
%setup -q

%build
export CGO_ENABLED=0
export GO111MODULE=on
go build -ldflags "-s -w -X main.version=%{version}" -o %{name} .

%install
install -D -m 0755 %{name} %{buildroot}%{_bindir}/%{name}
install -D -m 0644 resources/%{name}.desktop %{buildroot}%{_datadir}/applications/%{name}.desktop
install -D -m 0644 resources/icon_ready.svg %{buildroot}%{_datadir}/icons/hicolor/scalable/apps/%{name}.svg

%files
%license LICENSE
%doc README.md
%{_bindir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/scalable/apps/%{name}.svg

%changelog
* Thu Feb 06 2026 Tim Sutton <tim@kartoza.com> - 0.7.6-1
- Add room noise capture for audio calibration
- Remember last used presenter name
- Fix audio analysis hanging
- Fix form responsiveness
- Fix GIF animation in vertical video

* Sat Jan 18 2026 Tim Sutton <tim@kartoza.com> - 0.1.0-1
- Initial release
