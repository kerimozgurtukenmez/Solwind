#!/bin/sh
# Diagnostic report for bug reports: collects everything useful into one text file in your home folder.
# Works even when Solwind doesn't start (it doesn't need the app running; if solwindd, the background service that
# drives the fans and the keyboard light, runs, its state is added; it is only asked, over D-Bus).
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
# solwindd over D-Bus, read only: dtext <title> <object> <interface> <method> (a text it makes or a method's answer),
# dprop <title> <object> <interface> <property>. Strings are printed as text, other values as JSON (one item a line).
dbus_up() { have busctl && busctl --user status solwind.Daemon >/dev/null 2>&1; }
dbus_print() {
    if have python3; then
        python3 -I -c '
import json, sys
call = sys.argv[1] == "call"          # a method: data = its answers; a property: data = the value
raw = sys.stdin.read()
try: v = json.loads(raw)
except ValueError: print(raw.rstrip()); sys.exit()
def plain(x):
    if isinstance(x, dict) and "type" in x and "data" in x: return plain(x["data"])
    if isinstance(x, dict): return {k: plain(y) for k, y in x.items()}
    if isinstance(x, list): return [plain(y) for y in x]
    return x
d = plain(v.get("data"))
for x in (d if call and isinstance(d, list) else [d]):
    if isinstance(x, str): print(x)
    elif isinstance(x, list): print("\n".join(json.dumps(y, ensure_ascii=False) for y in x) or "[]")
    else: print(json.dumps(x, ensure_ascii=False))' "$1"
    else
        cat
    fi
}
dtext() { printf '\n----- %s -----\n' "$1"; shift; busctl --user call --json=short solwind.Daemon "$@" 2>&1 | dbus_print call; }
dprop() { printf '\n----- %s -----\n' "$1"; shift; busctl --user get-property --json=short solwind.Daemon "$@" 2>&1 | dbus_print prop; }

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

section "solwindd (the background service)"
if dbus_up; then
    dtext "Status" /solwind solwind.Daemon Status
    dprop "Plugins" /solwind solwind.Daemon Plugins
    dprop "Plugin files not loaded" /solwind solwind.Daemon PluginFailures
    dtext "Fans" /solwind/Fans solwind.Fans StatusText
    dtext "Profiles" /solwind/Fans solwind.Fans ProfileList
    dprop "Fan settings" /solwind/Fans solwind.Fans FanSettings
    dtext "Keyboard" /solwind/Keyboard solwind.Keyboard StatusText
    dtext "Keyboard detection" /solwind/Keyboard solwind.Keyboard InfoText
    dtext "Keyboard detection (raw)" /solwind/Keyboard solwind.Keyboard DetectionText
    dprop "Temperatures" /solwind/Sensors solwind.Sensors Readings
    dprop "Hardware names" /solwind/Sensors solwind.Sensors Hardware
    dprop "Fan speeds of the first fan driver (RPM)" /solwind/Sensors solwind.Sensors Rpms
else
    echo 'solwindd is not running.'
fi

section "Plugins installed in the home folder"
pdir=${XDG_DATA_HOME:-$HOME/.local/share}/solwind/plugins
if [ -d "$pdir" ]; then
    run "$pdir (file, mode, owner)" find "$pdir" \( -name '*.so' -o -name '*.json' \) -printf '%P %m %u\n'
    # (what solwind plugin recorded: commit, repository, SDK)
    run "solwind plugin's records" find "$pdir" -name '*.json' -exec cat {} +
else
    echo "none ($pdir doesn't exist)"
fi

section "Solwind windows (Quickshell instances)"
# (a copy under the old name, Ccenter, counts too)
have qs && qs list --all 2>&1 | grep -iE 'instance|config path|launch time|solwind|ccenter' | grep -B1 -A1 -iE '(solwind|ccenter)/shell'
running=""
if have qs; then
    running=$(qs list --all 2>/dev/null | sed -n 's/.*Config path: \(.*\(solwind\|ccenter\)\/shell\.qml\)$/\1/Ip' | head -1)
fi
# An app of 0.5.x and older does the work itself: ask it (the newer UI only passes these on to solwindd, asked above)
if [ -n "$running" ] && ! dbus_up; then
    rdir=${running%/shell.qml}
    run "Solwind status" qs ipc --any-display -p "$rdir" call cc status
    run "Profiles" qs ipc --any-display -p "$rdir" call cc profiles
    run "Keyboard" qs ipc --any-display -p "$rdir" call cc kbd
    run "Keyboard detection" qs ipc --any-display -p "$rdir" call cc kbdInfo
elif [ -z "$running" ]; then
    echo 'No Solwind window app is running.'
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

section "Sensors (read from /sys here)"
# The temperatures of the drivers Solwind uses (nothing else is woken up: NVMe, Wi-Fi, RAM sensors), every fan speed
for h in /sys/class/hwmon/hwmon*; do
    [ -r "$h/name" ] || continue
    n=$(cat "$h/name" 2>/dev/null); line=""
    case "$n" in
        coretemp|k10temp|zenpower|acpitz|i915|xe|amdgpu|nouveau)
            for f in "$h"/temp*_input; do
                [ -r "$f" ] || continue
                l=$(cat "${f%_input}_label" 2>/dev/null) || l=${f##*/}
                line="$line ${l:-${f##*/}}=$(cat "$f" 2>/dev/null)"
            done ;;
    esac
    for f in "$h"/fan*_input; do [ -r "$f" ] && line="$line ${f##*/}=$(cat "$f" 2>/dev/null)"; done
    [ -n "$line" ] && printf '%s (%s):%s\n' "${h##*/}" "$n" "$line"
done
printf 'CPU: %s, %s threads\n' "$(sed -n 's/^model name[[:space:]]*: //p' /proc/cpuinfo | head -1)" "$(nproc 2>/dev/null)"
have lspci && run "Display devices" sh -c 'lspci -nn | grep -iE "vga|3d|display"'
for d in /sys/bus/pci/devices/*; do
    [ "$(cat "$d/vendor" 2>/dev/null)" = 0x10de ] || continue
    case "$(cat "$d/class" 2>/dev/null)" in 0x03*) printf 'NVIDIA %s runtime power: %s\n' "${d##*/}" "$(cat "$d/power/runtime_status" 2>/dev/null)" ;; esac
done

section "Keyboard light (read from /sys here)"
for d in /sys/class/leds/*kbd*; do
    [ -e "$d" ] || continue
    printf '\n%s: driver %s\n' "${d##*/}" "$(basename "$(readlink -f "$d/device/driver" 2>/dev/null)" 2>/dev/null)"
    run "Files of ${d##*/}" ls -ln "$d/"
done
# USB keyboard controllers whose colours the kernel doesn't expose (ASUS Aura 0b05, ITE / Lenovo Legion 048d)
for u in /sys/bus/usb/devices/*; do
    case "$(cat "$u/idVendor" 2>/dev/null)" in 0b05|048d) ;; *) continue ;; esac
    printf 'USB %s:%s %s\n' "$(cat "$u/idVendor")" "$(cat "$u/idProduct" 2>/dev/null)" "$(cat "$u/product" 2>/dev/null)"
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
