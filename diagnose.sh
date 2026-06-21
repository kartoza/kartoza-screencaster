#!/usr/bin/env bash
# Full system diagnosis for kartoza-screencaster: display server,
# desktop portal, PipeWire, GStreamer plugins, ffmpeg encoders, audio
# sources, V4L2 devices, and wlroots/X11 fallback tools.
#
# Run inside the same shell you use for `cr` (i.e. inside `nix develop`)
# so the GStreamer plugin path the app actually sees is the one printed.

set -uo pipefail

section() { printf '\n=== %s ===\n' "$1"; }
have() { command -v "$1" >/dev/null 2>&1; }

# Check a GStreamer element by name and print a tick or cross.
check_gst() {
    local el=$1
    if gst-inspect-1.0 --exists "$el" 2>/dev/null; then
        printf '  %-20s present\n' "$el"
    else
        printf '  %-20s MISSING\n' "$el"
    fi
}

check_bin() {
    local prog=$1
    if have "$prog"; then
        printf '  %-15s %s\n' "$prog" "$(command -v "$prog")"
    else
        printf '  %-15s MISSING\n' "$prog"
    fi
}

# ----- Host & session -------------------------------------------------------

section "Host"
uname -a
if have lsb_release; then
    lsb_release -d 2>/dev/null | sed 's/^/  /'
elif [ -r /etc/os-release ]; then
    grep -E '^(NAME|VERSION)=' /etc/os-release | sed 's/^/  /'
fi

section "Session"
printf '  XDG_SESSION_TYPE    = %s\n' "${XDG_SESSION_TYPE:-(unset)}"
printf '  XDG_CURRENT_DESKTOP = %s\n' "${XDG_CURRENT_DESKTOP:-(unset)}"
printf '  XDG_SESSION_DESKTOP = %s\n' "${XDG_SESSION_DESKTOP:-(unset)}"
printf '  WAYLAND_DISPLAY     = %s\n' "${WAYLAND_DISPLAY:-(unset)}"
printf '  DISPLAY             = %s\n' "${DISPLAY:-(unset)}"

# ----- xdg-desktop-portal ---------------------------------------------------

section "xdg-desktop-portal"
if have busctl; then
    if busctl --user --no-pager list 2>/dev/null \
        | grep -E 'org\.freedesktop\.portal\.(Desktop|impl)' \
        | sed 's/^/  /'; then
        :
    else
        echo "  (no portal services on session bus)"
    fi
else
    echo "  (busctl not available)"
fi

if [ -d /usr/share/xdg-desktop-portal/portals ]; then
    echo "  Installed portal backends:"
    for f in /usr/share/xdg-desktop-portal/portals/*.portal; do
        [ -e "$f" ] || continue
        basename "$f" .portal | sed 's/^/    /'
    done
fi

# ----- PipeWire -------------------------------------------------------------

section "PipeWire"
if have pw-cli; then
    pw-cli info 2>/dev/null | head -10 | sed 's/^/  /'
else
    echo "  (pw-cli not available — not a problem if the user-session pipewire is running)"
fi
if have systemctl; then
    for svc in pipewire pipewire-pulse wireplumber; do
        state=$(systemctl --user is-active "$svc" 2>/dev/null || echo unknown)
        printf '  %-30s %s\n' "$svc" "$state"
    done
fi

# ----- GStreamer ------------------------------------------------------------

section "GStreamer plugin search paths"
echo "  GST_PLUGIN_SYSTEM_PATH_1_0:"
printf '%s' "${GST_PLUGIN_SYSTEM_PATH_1_0:-}" | tr ':' '\n' | sed 's/^/    /'
[ -z "${GST_PLUGIN_SYSTEM_PATH_1_0:-}" ] && echo "    (unset)"
echo "  GST_PLUGIN_PATH:"
printf '%s' "${GST_PLUGIN_PATH:-}" | tr ':' '\n' | sed 's/^/    /'
[ -z "${GST_PLUGIN_PATH:-}" ] && echo "    (unset)"

if have gst-inspect-1.0; then
    section "GStreamer sources (need pipewiresrc on GNOME/KDE Wayland)"
    for el in pipewiresrc v4l2src pulsesrc alsasrc ximagesrc waylandsrc; do
        check_gst "$el"
    done

    section "GStreamer video encoders (need one H.264 encoder)"
    for el in x264enc avenc_libx264 openh264enc vaapih264enc nvh264enc \
              omxh264enc vp8enc vp9enc theoraenc jpegenc; do
        check_gst "$el"
    done

    section "GStreamer audio encoders and muxers"
    for el in opusenc vorbisenc lamemp3enc avenc_aac voaacenc \
              mp4mux webmmux matroskamux oggmux avimux; do
        check_gst "$el"
    done
else
    section "GStreamer"
    echo "  (gst-inspect-1.0 not available — gstreamer not in PATH)"
fi

# ----- ffmpeg ---------------------------------------------------------------

section "ffmpeg"
if have ffmpeg; then
    ffmpeg -hide_banner -version 2>&1 | head -1 | sed 's/^/  /'
    echo "  Configured input devices (need pulse or pipewire for portal-free audio):"
    ffmpeg -hide_banner -devices 2>/dev/null | sed 's/^/    /'
    echo "  Encoders we care about:"
    ffmpeg -hide_banner -encoders 2>/dev/null \
        | grep -E '\b(libx264|libx265|libvpx|libvpx-vp9|aac|libmp3lame|libopus|libvorbis)\b' \
        | sed 's/^/    /'
    echo "  Muxers we care about:"
    ffmpeg -hide_banner -formats 2>/dev/null \
        | grep -E '^\s+E\s+(mp4|matroska|webm|wav|ogg|adts)\b' \
        | sed 's/^/    /'
else
    echo "  (ffmpeg not available)"
fi

# ----- Audio sources --------------------------------------------------------

section "Audio sources"
if have pactl; then
    pactl list short sources 2>/dev/null | sed 's/^/  /' || echo "  (pactl call failed)"
elif have pw-cli; then
    echo "  (using pw-cli — pactl absent)"
    pw-cli ls Node 2>/dev/null \
        | awk '/id [0-9]+/ {id=$2} /media\.class.*Audio\/Source/ {print "    node "id}' \
        | head -20
else
    echo "  (no pactl/pw-cli)"
fi

# ----- V4L2 webcams ---------------------------------------------------------

section "V4L2 video devices"
if compgen -G '/dev/video*' >/dev/null; then
    for d in /dev/video*; do
        printf '  %s\n' "$d"
        if have v4l2-ctl; then
            v4l2-ctl --device "$d" --info 2>/dev/null \
                | grep -E 'Card type|Driver name' \
                | sed 's/^/    /'
        fi
    done
else
    echo "  (no /dev/video* devices)"
fi

# ----- Capture-time helpers -------------------------------------------------

section "wlroots screen tools (used when the compositor supports wlr-screencopy)"
for tool in grim wl-screenrec wf-recorder slurp; do
    check_bin "$tool"
done

section "X11 screen tools (used on X11 sessions)"
for tool in xrandr xdpyinfo xwininfo; do
    check_bin "$tool"
done

section "Portal-recording chain status"
case "${XDG_SESSION_TYPE:-}" in
    wayland)
        echo "  Wayland session detected."
        case "${XDG_CURRENT_DESKTOP:-}" in
            *GNOME*|*KDE*|*Plasma*)
                echo "  Compositor is GNOME or KDE — recording must use xdg-desktop-portal"
                echo "  ScreenCast + PipeWire. The required elements are pipewiresrc and"
                echo "  one of the H.264 encoders listed above (or vp8enc if we fall back"
                echo "  to webm)."
                ;;
            *Hyprland*|*sway*|*Wayfire*|*COSMIC*|*River*)
                echo "  Wlroots-family compositor — uses grim and wl-screenrec, not the portal."
                ;;
            *)
                echo "  Compositor not recognised (XDG_CURRENT_DESKTOP=${XDG_CURRENT_DESKTOP:-unset});"
                echo "  app will fall back to the portal path."
                ;;
        esac
        ;;
    x11)
        echo "  X11 session — uses QScreen::grabWindow (preview) and ffmpeg x11grab (recording)."
        ;;
    *)
        echo "  Session type not recognised (XDG_SESSION_TYPE=${XDG_SESSION_TYPE:-unset})."
        ;;
esac

echo
echo "=== End of diagnosis ==="
