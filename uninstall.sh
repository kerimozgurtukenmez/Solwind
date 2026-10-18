#!/bin/sh
# Removes Ccenter (as installed by install.sh). Only deletes files verified to belong to Ccenter.
# Asks separately about the keyboard permission and the settings.
set -eu

data=${XDG_DATA_HOME:-$HOME/.local/share}
config=${XDG_CONFIG_HOME:-$HOME/.config}
app=$data/ccenter
launcher=$app/bin/ccenter
rule=/etc/udev/rules.d/90-ccenter.rules

# Message language: CCENTER_LANG, else the language chosen in the app (settings.json), else the system locale
lang=${CCENTER_LANG:-}
if [ -z "$lang" ]; then
    grep -q '"lang": *"tr"' "$config/ccenter/settings.json" 2>/dev/null && lang=tr
    grep -q '"lang": *"en"' "$config/ccenter/settings.json" 2>/dev/null && lang=en
fi
[ -n "$lang" ] || case "${LC_ALL:-${LC_MESSAGES:-${LANG:-}}}" in tr*) lang=tr ;; *) lang=en ;; esac

say() { printf '%s\n' "$*"; }
# t "English" "Türkçe": print the line in the chosen language
t() { if [ "$lang" = tr ]; then say "$2"; else say "$1"; fi; }
# ask "English question" "Türkçe soru" default(y|n) -> 0 = yes. Accepts y/n and e/h (evet/hayır) in both languages.
ask() {
    if [ "$lang" = tr ]; then _q=$2; _h="[E/h]"; [ "$3" = y ] || _h="[e/H]"
    else _q=$1; _h="[Y/n]"; [ "$3" = y ] || _h="[y/N]"; fi
    printf '%s %s ' "$_q" "$_h"
    read -r ans || ans=""
    case "$ans" in
        [eEyY]*) return 0 ;;
        [hHnN]*) return 1 ;;
        *) [ "$3" = y ] ;;
    esac
}

# To delete: only Ccenter's own files (launcher entry/service if they contain our app path,
# the terminal command if it links to our app)
files=""
for f in "$data/applications/ccenter.desktop" "$data/systemd/user/ccenter.service" "$config/systemd/user/ccenter.service"; do
    [ -f "$f" ] && grep -q "$app" "$f" && files="$files $f"
done
# Startup files where install.sh added the PATH block (only the block between the markers is removed)
mark_begin="# >>> ccenter >>>"
rcfiles=""
for f in "$HOME/.bashrc" "$HOME/.zshrc"; do
    [ -f "$f" ] && grep -qF "$mark_begin" "$f" && rcfiles="$rcfiles $f"
done
fishconf=$config/fish/conf.d/ccenter.fish
[ -f "$fishconf" ] && grep -qF "$mark_begin" "$fishconf" && files="$files $fishconf"
[ -L "$HOME/.local/bin/ccenter" ] && [ "$(readlink -f "$HOME/.local/bin/ccenter")" = "$launcher" ] && files="$files $HOME/.local/bin/ccenter"

t "Removing Ccenter" "Ccenter kaldırılıyor"
say "===================="
if [ ! -d "$app" ] && [ -z "$files$rcfiles" ]; then
    t "No installed Ccenter found ($app does not exist)." "Kurulu bir Ccenter bulunamadı ($app yok)."
else
    t "Will be deleted:" "Silinecekler:"
    [ -d "$app" ] && say "  $app/"
    for f in $files; do say "  $f"; done
    for f in $rcfiles; do t "  $f   (only the Ccenter PATH block)" "  $f   (sadece Ccenter'ın PATH bloğu)"; done
    ask "Continue?" "Devam edilsin mi?" y || { t "Cancelled, nothing changed." "İptal edildi, hiçbir şey değişmedi."; exit 0; }

    # If it's running, quit it cleanly (keyboard effect to the static colour, fans back to NBFC)
    if qs list --all 2>/dev/null | grep -q "$app/shell.qml"; then
        t "Quitting the running Ccenter…" "Çalışan Ccenter kapatılıyor…"
        qs ipc -p "$app" call cc quit >/dev/null 2>&1 || true
        sleep 2
    fi
    # Disable the background service if enabled
    if command -v systemctl >/dev/null 2>&1; then
        systemctl --user disable --now ccenter.service >/dev/null 2>&1 || true
    fi
    rm -rf "$app"
    for f in $files; do rm -f "$f"; done
    for f in $rcfiles; do sed -i '/^# >>> ccenter >>>$/,/^# <<< ccenter <<<$/d' "$f"; done
    command -v systemctl >/dev/null 2>&1 && systemctl --user daemon-reload 2>/dev/null || true
    t "App removed." "Uygulama kaldırıldı."
fi

# Keyboard light permission
if [ -f "$rule" ]; then
    say ""
    t "The keyboard light permission ($rule) is still there." "Klavye ışığı izni ($rule) duruyor."
    if ask "Remove this permission too? (asks for the admin password)" "Bu izin de kaldırılsın mı? (yönetici şifresi istenecek)" y; then
        # Give the light files back to root (owner root, 0644; kbd_rgb_mode keeps its write-only mode)
        reset_perms='for d in /sys/class/leds/*::kbd_backlight /sys/class/leds/*::kbd_backlight_* /sys/class/leds/*::kbd_zoned_backlight-*; do [ -d "$d" ] || continue; for f in brightness multi_intensity; do [ -e "$d/$f" ] && chown root:root "$d/$f" && chmod 0644 "$d/$f"; done; [ -e "$d/kbd_rgb_mode" ] && chown root:root "$d/kbd_rgb_mode"; done; for f in /sys/devices/platform/hp-wmi/rgb_zones/zone*; do [ -e "$f" ] && chown root:root "$f" && chmod 0644 "$f"; done; true'
        if sudo rm -f "$rule" && sudo udevadm control --reload-rules && sudo sh -c "$reset_perms"; then
            t "Keyboard permission removed (the light files are writable only by root again)." \
              "Klavye izni kaldırıldı (ışık dosyaları yine sadece root tarafından yazılabilir)."
        else
            t "Could not remove it. By hand: sudo rm $rule" "Kaldırılamadı. Elle: sudo rm $rule"
        fi
    fi
fi

# Settings
cfg=$config/ccenter
if [ -d "$cfg" ]; then
    say ""
    if ask "Delete your settings too ($cfg: fan profiles, curves, keyboard)?" "Ayarların ($cfg: fan profilleri, eğriler, klavye) da silinsin mi?" n; then
        rm -rf "$cfg"
        t "Settings deleted." "Ayarlar silindi."
    else
        t "Settings kept; if you install again you continue where you left off." "Ayarlar duruyor; tekrar kurarsan kaldığın yerden devam eder."
    fi
fi
say ""
t "Done." "Tamam."
