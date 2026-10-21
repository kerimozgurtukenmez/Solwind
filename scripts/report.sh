#!/bin/sh
# Diagnostic report for bug reports: collects everything useful into one text file in your home folder.
# Works even when Solwind doesn't start (it doesn't need the app running; if it runs, its own status is added).
# Read only: nothing on the system is changed, nothing is sent anywhere. You read the file and decide what to share.
# Privacy: no serial numbers, MAC/IP addresses or passwords are collected; your home folder, user name and computer
# name are replaced with ~, <user> and <host> at the end.
# Usage: report.sh [app folder]   (solwind report / the app's "Create report" button call it)
set +e
self=$(readlink -f "$0")
app=${1:-${self%/scripts/report.sh}}
out=$HOME/solwind-report-$(date +%Y%m%d-%H%M%S).txt

# Message language, same rules as bin/solwind (SOLWIND_LANG > app setting > system locale)
lang=${SOLWIND_LANG:-}
cfg=${XDG_CONFIG_HOME:-$HOME/.config}/solwind/settings.json
# (before 0.4.0 Solwind was called Ccenter; its settings are copied over on the first start)
[ -e "$cfg" ] || cfg=${XDG_CONFIG_HOME:-$HOME/.config}/ccenter/settings.json
if [ -z "$lang" ]; then
    grep -q '"lang": *"tr"' "$cfg" 2>/dev/null && lang=tr
    grep -q '"lang": *"en"' "$cfg" 2>/dev/null && lang=en
fi
[ -n "$lang" ] || case "${LC_ALL:-${LC_MESSAGES:-${LANG:-}}}" in tr*) lang=tr ;; *) lang=en ;; esac
t() { if [ "$lang" = tr ]; then printf '%s\n' "$2"; else printf '%s\n' "$1"; fi; }

have() { command -v "$1" >/dev/null 2>&1; }
# run <title> <command...>: a section with the command's output (stdout + stderr), at most 20 s
run() {
    title=$1; shift
    printf '\n----- %s -----\n$ %s\n' "$title" "$*"
    if have timeout; then timeout 20 "$@" 2>&1; else "$@" 2>&1; fi
    rc=$?
    [ $rc -ne 0 ] && printf '(exit code %s)\n' "$rc"
    return 0
}
section() { printf '\n\n===== %s =====\n' "$1"; }

{
printf 'Solwind diagnostic report\n'
printf 'Created: %s\n' "$(date -u '+%Y-%m-%d %H:%M:%S UTC')"
printf 'Solwind version: %s\n' "$(cat "$app/VERSION" 2>/dev/null || echo '?')"
printf 'App folder: %s\n' "$app"

section "System"
[ -r /etc/os-release ] && . /etc/os-release && printf 'OS: %s\n' "${PRETTY_NAME:-?}"
printf 'Kernel: %s\n' "$(uname -r)"
printf 'Desktop: %s / %s\n' "${XDG_CURRENT_DESKTOP:-?}" "${XDG_SESSION_TYPE:-?}"
printf 'Locale: %s\n' "${LANG:-?}"
have qs && printf 'Quickshell: %s\n' "$(qs --version 2>&1 | head -1)"
have hyprctl && printf 'Hyprland: %s\n' "$(hyprctl version 2>/dev/null | head -1)"
if have pacman; then
    printf 'Packages: %s\n' "$(pacman -Q quickshell quickshell-git caelestia-shell nbfc-linux python-gobject 2>/dev/null | tr '\n' ' ')"
fi
[ -d /usr/lib/qt6/qml/Caelestia ] && echo 'Caelestia QML plugin: found' || echo 'Caelestia QML plugin: NOT FOUND (/usr/lib/qt6/qml/Caelestia)'
have python3 && { python3 -c 'import gi; gi.require_version("Gio", "2.0")' 2>/dev/null && echo 'PyGObject: ok' || echo 'PyGObject: missing (no tray icon)'; }
printf 'polkit agent: %s\n' "$(pgrep -af 'polkit.*agent|hyprpolkit|polkit-gnome|polkit-kde|lxpolkit' 2>/dev/null | head -1 | cut -c1-80 || true)"

section "Laptop (no serial numbers)"
for f in sys_vendor product_name product_version product_family product_sku board_vendor board_name bios_version bios_date; do
    printf '%s: %s\n' "$f" "$(cat /sys/class/dmi/id/$f 2>/dev/null)"
done

section "Solwind instances"
# (a copy under the old name, Ccenter, counts too)
have qs && qs list --all 2>&1 | grep -iE 'instance|config path|launch time|solwind|ccenter' | grep -B1 -A1 -iE '(solwind|ccenter)/shell'
running=""
if have qs; then
    running=$(qs list --all 2>/dev/null | sed -n 's/.*Config path: \(.*\(solwind\|ccenter\)\/shell\.qml\)$/\1/Ip' | head -1)
fi
if [ -n "$running" ]; then
    rdir=${running%/shell.qml}
    run "Solwind status" qs ipc --any-display -p "$rdir" call cc status
    run "Profiles" qs ipc --any-display -p "$rdir" call cc profiles
    run "Keyboard" qs ipc --any-display -p "$rdir" call cc kbd
    run "Keyboard detection" qs ipc --any-display -p "$rdir" call cc kbdInfo
else
    echo 'Solwind is not running.'
fi

section "NBFC"
if have nbfc; then
    run "Version" nbfc --version
    have systemctl && printf 'nbfc_service: %s, %s\n' "$(systemctl is-active nbfc_service 2>&1)" "$(systemctl is-enabled nbfc_service 2>&1)"
    run "Status" nbfc status -a
    run "Model name" nbfc get-model-name
    [ -r /etc/nbfc/nbfc.json ] && run "Service config" cat /etc/nbfc/nbfc.json
else
    echo 'nbfc: NOT INSTALLED'
fi

section "Sensors"
run "Temperatures (sensors.sh)" sh "$app/scripts/sensors.sh" gpu
run "Hardware names (hwinfo.sh)" sh "$app/scripts/hwinfo.sh"
for d in /sys/bus/pci/devices/*; do
    [ "$(cat "$d/vendor" 2>/dev/null)" = 0x10de ] || continue
    case "$(cat "$d/class" 2>/dev/null)" in 0x03*) printf 'NVIDIA %s runtime power: %s\n' "${d##*/}" "$(cat "$d/power/runtime_status" 2>/dev/null)" ;; esac
done

section "Keyboard light"
run "Detection (kbdinfo.sh)" sh "$app/scripts/kbdinfo.sh"
for d in /sys/class/leds/*kbd*; do
    [ -e "$d" ] && run "Files of ${d##*/}" ls -ln "$d/"
done
[ -d /sys/devices/platform/hp-wmi/rgb_zones ] && run "HP four-zone files (rgb_zones)" ls -ln /sys/devices/platform/hp-wmi/rgb_zones/
[ -r /sys/module/hp_wmi/taint ] && run "hp_wmi module taint (O = out-of-tree)" cat /sys/module/hp_wmi/taint
# Which hp_wmi is loaded matters: the keyboard files differ between the kernel's own and the extra module's versions
if [ -d /sys/module/hp_wmi ]; then
    command -v modinfo >/dev/null 2>&1 && run "hp_wmi module file" modinfo -F filename hp_wmi
    [ -r /sys/module/hp_wmi/srcversion ] && run "hp_wmi srcversion" cat /sys/module/hp_wmi/srcversion
fi
[ -f /etc/udev/rules.d/90-solwind.rules ] && run "Permission rule" cat /etc/udev/rules.d/90-solwind.rules
[ -f /etc/udev/rules.d/90-ccenter.rules ] && run "Old permission rule (Ccenter)" cat /etc/udev/rules.d/90-ccenter.rules

section "Settings"
[ -r "$cfg" ] && { printf '(%s)\n' "$cfg"; cat "$cfg"; } || echo "(no settings file: $cfg)"

section "Background service"
if have systemctl; then
    run "Status" systemctl --user --no-pager status solwind.service
    have journalctl && run "Journal (last 80 lines)" journalctl --user -u solwind.service -n 80 --no-pager
    # the old name's service (Ccenter, before 0.4.0), if it is still there
    old=$(systemctl --user is-enabled ccenter.service 2>/dev/null)
    [ -n "$old" ] && [ "$old" != not-found ] \
        && printf '\nOld ccenter.service: %s, %s (%s)\n' "$old" "$(systemctl --user is-active ccenter.service 2>&1)" \
                  "$(systemctl --user show -p FragmentPath --value ccenter.service 2>&1)"
fi

section "App logs (latest runs, also ones that crashed; kept until reboot)"
rt=${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/quickshell/by-id
n=0
if have qs && [ -d "$rt" ]; then
    # newest first; an instance belongs to Solwind if its lock file names a .../solwind/shell.qml config (or the old
    # name's .../ccenter/shell.qml)
    for d in $(ls -dt "$rt"/* 2>/dev/null); do
        [ -f "$d/log.qslog" ] || continue
        tr -d '\000' < "$d/instance.lock" 2>/dev/null | grep -qiE '(solwind|ccenter)/shell\.qml' || continue
        n=$((n + 1))
        run "Log of instance ${d##*/} ($(date -r "$d/log.qslog" '+%Y-%m-%d %H:%M'), last 250 lines)" qs log -t 250 "$d/log.qslog"
        [ $n -ge 3 ] && break
    done
fi
[ $n -eq 0 ] && echo '(no Solwind logs found; they are cleared on reboot)'
} > "$out" 2>&1

# Terminal colour codes (qs log) out
sed -i 's/\x1b\[[0-9;]*m//g' "$out"
# Privacy: home folder, user name and computer name out
host=$(cat /proc/sys/kernel/hostname 2>/dev/null)
user=$(id -un)
esc() { printf '%s' "$1" | sed 's/[][\.*^$/|&]/\\&/g'; }
sed -i "s|$(esc "$HOME")|~|g" "$out"
[ ${#user} -ge 3 ] && sed -i "s|\b$(esc "$user")\b|<user>|g" "$out"
[ ${#host} -ge 3 ] && sed -i "s|\b$(esc "$host")\b|<host>|g" "$out"

t "Report saved: $out" "Rapor kaydedildi: $out"
t "Please read it before sharing; attach it to a GitHub issue: https://github.com/kerimozgurtukenmez/Solwind/issues" \
  "Paylaşmadan önce göz at; GitHub'da bir issue'ya ekleyebilirsin: https://github.com/kerimozgurtukenmez/Solwind/issues"
