#!/bin/sh
# Removes Solwind (as installed by install.sh). Only deletes files verified to belong to Solwind, and what Ccenter
# (Solwind's name before 0.4.0) left behind. Asks separately about the keyboard permission and the settings.
set -eu

src=$(cd "$(dirname "$0")" && pwd)
data=${XDG_DATA_HOME:-$HOME/.local/share}
config=${XDG_CONFIG_HOME:-$HOME/.config}
app=$data/solwind
launcher=$app/bin/solwind
rule=/etc/udev/rules.d/90-solwind.rules

# Message language: SOLWIND_LANG, else the language chosen in the app (settings.json, else Ccenter's), else the
# system locale
settings=$config/solwind/settings.json
[ -e "$settings" ] || settings=$config/ccenter/settings.json
lang=${SOLWIND_LANG:-}
if [ -z "$lang" ]; then
    grep -q '"lang": *"tr"' "$settings" 2>/dev/null && lang=tr
    grep -q '"lang": *"en"' "$settings" 2>/dev/null && lang=en
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
    # No answer at all (no terminal, e.g. started with a file manager's "Run"): counts as no, so nothing happens unseen
    if ! read -r ans; then say ""; return 1; fi
    case "$ans" in
        [eEyY]*) return 0 ;;
        [hHnN]*) return 1 ;;
        *) [ "$3" = y ] ;;
    esac
}
# Started straight by a terminal window instead of from a shell you typed in (a file manager's "run in terminal" does
# `foot -e ./uninstall.sh`): that window closes the moment the script ends, so wait for Enter first, also after an
# error. Only with a terminal on both ends, so a pipe or another script never waits.
hold=""
if [ -t 0 ] && [ -t 1 ]; then
    parent=""
    [ -r "/proc/$PPID/comm" ] && read -r parent < "/proc/$PPID/comm" || true
    case "$parent" in
        "") ;;
        sh|ash|bash|dash|zsh|ksh|mksh|fish|tcsh|csh|nu|xonsh|elvish)
            # a shell started with -c (also -lc, -ic, --command) runs just this and then closes too
            tr '\0' '\n' < "/proc/$PPID/cmdline" 2>/dev/null | tail -n +2 \
                | grep -qE -- '^(-[A-Za-z]*c[A-Za-z]*|--command)$' && hold=1 || true ;;
        *) hold=1 ;;
    esac
fi
[ -z "$hold" ] || trap 'say ""; t "Press Enter to close this window." "Bu pencereyi kapatmak için Enter tuşuna bas."; read -r _hold || true' EXIT

# File lists are one path per line, so paths with spaces stay whole (loops split on newlines only)
nl='
'
# Ccenter's leftovers (oc_* names; also real_path etc.): removed too
. "$src/scripts/ccenter-leftovers.sh"
oc_find
app_real=$(real_path "$data")/solwind          # what links and qs report (a linked home folder)
# To delete: only Solwind's own files (launcher entry/service if they contain our app path,
# the terminal command if it links to our app)
files=""
for f in "$data/applications/solwind.desktop" "$data/systemd/user/solwind.service" "$config/systemd/user/solwind.service"; do
    [ -f "$f" ] && grep -qF "$app" "$f" && files="$files$f$nl"
done
# Startup files where install.sh added the PATH block (only the block between the markers is removed; a block that
# isn't closed, or a file that can't be written, is left for you: rcmanual)
mark_begin="# >>> solwind >>>"
mark_end="# <<< solwind <<<"
rcfiles="" rcmanual=""
for f in "$HOME/.bashrc" "$HOME/.zshrc"; do
    if [ -f "$f" ] && grep -qxF "$mark_begin" "$f"; then
        if rc_block_ok "$f" "$mark_begin" "$mark_end" && rc_writable "$f"; then rcfiles="$rcfiles$f$nl"
        else rcmanual="$rcmanual$f$nl"; fi
    fi
done
fishconf=$config/fish/conf.d/solwind.fish
[ -f "$fishconf" ] && grep -qF "$mark_begin" "$fishconf" && files="$files$fishconf$nl"
[ -L "$HOME/.local/bin/solwind" ] && [ "$(readlink -f "$HOME/.local/bin/solwind")" = "$app_real/bin/solwind" ] && files="$files$HOME/.local/bin/solwind$nl"

t "Removing Solwind" "Solwind kaldırılıyor"
say "===================="
if [ ! -d "$app" ] && [ -z "$files$rcfiles$rcmanual" ] && ! oc_found; then
    t "No installed Solwind found ($app does not exist)." "Kurulu bir Solwind bulunamadı ($app yok)."
else
    if [ -d "$app" ] || [ -n "$files$rcfiles$rcmanual" ]; then
        t "Will be deleted:" "Silinecekler:"
        [ -d "$app" ] && say "  $app/"
        _ifs=$IFS; IFS=$nl; set -f
        for f in $files; do say "  $f"; done
        for f in $rcfiles; do t "  $f   (only the Solwind PATH block)" "  $f   (sadece Solwind'in PATH bloğu)"; done
        for f in $rcmanual; do
            t "  $f   its Solwind PATH block is left as it is (it isn't closed by '$mark_end', or the file isn't writable): remove it by hand" \
              "  $f   Solwind PATH bloğu olduğu gibi kalacak ('$mark_end' ile kapanmıyor ya da dosya yazılamıyor): elle sil"
        done
        IFS=$_ifs; set +f
    fi
    if oc_found; then
        t "Left by Ccenter (Solwind's name before 0.4.0):" "Ccenter'den (Solwind'in 0.4.0'dan önceki adı) kalanlar:"
        oc_list ""
    fi
    if [ -n "$oc_unit_other" ]; then
        t "  A Ccenter background service that isn't from this install is on ($oc_unit_other): not touched. To turn it off: systemctl --user disable --now ccenter.service" \
          "  Bu kuruluma ait olmayan bir Ccenter arka plan servisi açık ($oc_unit_other): dokunulmayacak. Kapatmak için: systemctl --user disable --now ccenter.service"
    fi
    ask "Continue?" "Devam edilsin mi?" y || { t "Cancelled, nothing changed." "İptal edildi, hiçbir şey değişmedi."; exit 0; }

    # If it's running, quit it cleanly (keyboard effect to the static colour, fans back to NBFC)
    # (the launcher's quit finds it on any display and waits until it's gone; the wait below covers older launchers)
    if qs list --all 2>/dev/null | grep -qF "$app_real/shell.qml"; then
        t "Quitting the running Solwind…" "Çalışan Solwind kapatılıyor…"
        "$launcher" quit >/dev/null 2>&1 || qs ipc --any-display -p "$app" call cc quit >/dev/null 2>&1 || true
        n=0
        while [ "$n" -lt 50 ] && qs list --all 2>/dev/null | grep -qF "$app_real/shell.qml"; do sleep 0.2; n=$((n + 1)); done
    fi
    if oc_running; then
        t "Quitting the running Ccenter…" "Çalışan Ccenter kapatılıyor…"
        "$oc_launcher" quit >/dev/null 2>&1 || qs ipc --any-display -p "$oc_app" call cc quit >/dev/null 2>&1 || true
        n=0
        while [ "$n" -lt 50 ] && oc_running; do sleep 0.2; n=$((n + 1)); done
    fi
    # Disable the background service, but only if it is this installation's (its unit file is one of ours above)
    case "$nl$files" in
        *"/systemd/user/solwind.service$nl"*)
            command -v systemctl >/dev/null 2>&1 && systemctl --user disable --now solwind.service >/dev/null 2>&1 || true ;;
    esac
    rm -rf "$app" "$app.new" "$app.old"           # (.new/.old: left by an install that stopped half way)
    _ifs=$IFS; IFS=$nl; set -f
    for f in $files; do rm -f "$f"; done
    for f in $rcfiles; do rc_sed '/^# >>> solwind >>>$/,/^# <<< solwind <<<$/d' "$f"; done
    IFS=$_ifs; set +f
    oc_remove ""
    if [ -n "$oc_failed" ]; then
        t "Could not remove (remove these by hand):" "Kaldırılamadı (elle sil):"
        _ifs=$IFS; IFS=$nl; set -f
        for f in $oc_failed; do say "  $f"; done
        IFS=$_ifs; set +f
    fi
    command -v systemctl >/dev/null 2>&1 && systemctl --user daemon-reload 2>/dev/null || true
    t "App removed." "Uygulama kaldırıldı."
fi

# Keyboard light permission (also Ccenter's, under the old name)
rules=""
for f in "$rule" "$oc_rule"; do [ -f "$f" ] && rules="$rules $f"; done
if [ -n "$rules" ]; then
    rules=${rules# }
    say ""
    t "The keyboard light permission ($rules) is still there." "Klavye ışığı izni ($rules) duruyor."
    if ask "Remove this permission too? (asks for the admin password)" "Bu izin de kaldırılsın mı? (yönetici şifresi istenecek)" y; then
        # Give the light files back to root (owner root, 0644; kbd_rgb_mode keeps its write-only mode)
        reset_perms='for d in /sys/class/leds/*::kbd_backlight /sys/class/leds/*::kbd_backlight_* /sys/class/leds/*::kbd_zoned_backlight-*; do [ -d "$d" ] || continue; for f in brightness multi_intensity; do [ -e "$d/$f" ] && chown root:root "$d/$f" && chmod 0644 "$d/$f"; done; [ -e "$d/kbd_rgb_mode" ] && chown root:root "$d/kbd_rgb_mode"; done; for f in /sys/devices/platform/hp-wmi/rgb_zones/zone*; do [ -e "$f" ] && chown root:root "$f" && chmod 0644 "$f"; done; true'
        if sudo rm -f "$rule" "$oc_rule" && sudo udevadm control --reload-rules && sudo sh -c "$reset_perms"; then
            t "Keyboard permission removed (the light files are writable only by root again)." \
              "Klavye izni kaldırıldı (ışık dosyaları yine sadece root tarafından yazılabilir)."
        else
            t "Could not remove it. By hand: sudo rm $rules" "Kaldırılamadı. Elle: sudo rm $rules"
        fi
    fi
fi

# Settings (also Ccenter's, which Solwind copied on its first start)
cfg=""
for f in "$config/solwind" "$oc_cfg"; do [ -d "$f" ] && cfg="$cfg$f$nl"; done
if [ -n "$cfg" ]; then
    say ""
    _shown=$(printf '%s' "$cfg" | tr '\n' ' ')
    _shown=${_shown% }
    if ask "Delete your settings too ($_shown: fan profiles, curves, keyboard)?" "Ayarların ($_shown: fan profilleri, eğriler, klavye) da silinsin mi?" n; then
        _ifs=$IFS; IFS=$nl; set -f
        for f in $cfg; do rm -rf "$f"; done
        IFS=$_ifs; set +f
        t "Settings deleted." "Ayarlar silindi."
    else
        t "Settings kept; if you install again you continue where you left off." "Ayarlar duruyor; tekrar kurarsan kaldığın yerden devam eder."
    fi
fi
say ""
t "Done." "Tamam."
