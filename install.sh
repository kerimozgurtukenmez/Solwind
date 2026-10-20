#!/bin/sh
# Solwind installer: installs into your home folder, NO admin password needed.
# Everything lives in the app's own folder (~/.local/share/solwind); the launcher entry, the background service and
# the terminal command (~/.local/bin/solwind) are added where writable and skipped otherwise (the install still works).
# The only optional admin step is the keyboard light permission: explained and asked (default: no).
# To remove: ./uninstall.sh
set -eu

src=$(cd "$(dirname "$0")" && pwd)
data=${XDG_DATA_HOME:-$HOME/.local/share}
config=${XDG_CONFIG_HOME:-$HOME/.config}
app=$data/solwind
launcher=$app/bin/solwind                     # the launcher entry and the service call this by full path
rule=/etc/udev/rules.d/90-solwind.rules

# Message language: SOLWIND_LANG, else the language chosen in the app (settings.json; Ccenter's until Solwind's
# first start copies it over), else the system locale
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
# `foot -e ./install.sh`): that window closes the moment the script ends, so wait for Enter first, also after an
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
# Is the folder (or, if it doesn't exist yet, the closest existing parent) writable?
# (POSIX sh has no local variables: _cw_-prefixed names so functions don't clobber the caller's variables)
can_write() {
    _cw_d=$1
    while [ ! -e "$_cw_d" ]; do _cw_d=$(dirname "$_cw_d"); done
    [ -d "$_cw_d" ] && [ -w "$_cw_d" ]
}
owner() { _cw_d=$1; while [ ! -e "$_cw_d" ]; do _cw_d=$(dirname "$_cw_d"); done; stat -c %U "$_cw_d"; }
# File lists are one path per line, so paths with spaces stay whole (loops split on newlines only)
nl='
'
# Ccenter, Solwind's name before 0.4.0: what its install left behind is replaced (oc_* names; also real_path etc.)
. "$src/scripts/ccenter-leftovers.sh"
launcher_real=$(real_path "$data")/solwind/bin/solwind    # what links and qs report (a linked home folder)

t "Solwind setup" "Solwind kurulumu"
say "================"
say ""

# ---------- pre-checks ----------
missing=""
command -v qs >/dev/null 2>&1 || missing="$missing quickshell"
[ -d /usr/lib/qt6/qml/Caelestia ] || missing="$missing caelestia-shell"
command -v nbfc >/dev/null 2>&1 || t "Note: nbfc-linux not found; it is needed for fan control." "Not: nbfc-linux bulunamadı; fan kontrolü için gerekli."
if [ -n "$missing" ]; then
    t "Missing:$missing" "Eksik:$missing"
    t "Solwind does not work without these. Install them first, then try again." "Solwind bunlar olmadan çalışmaz. Önce kur, sonra tekrar dene."
    exit 1
fi
if [ -f /usr/share/solwind/shell.qml ]; then
    t "A system-wide Solwind is installed (/usr/share/solwind). Remove it first so the two installs don't get mixed up." \
      "Sistem genelinde kurulu bir Solwind var (/usr/share/solwind). İki kurulum karışmasın diye önce onu kaldır."
    exit 1
fi
# A system-wide Ccenter (the old Makefile's install, default prefix /usr) needs the admin password to remove: not here
for _p in /usr /usr/local; do
    [ -f "$_p/share/ccenter/shell.qml" ] || continue
    t "A system-wide Ccenter (Solwind's name before 0.4.0) is installed. Remove it first so the two installs don't get mixed up; its files:" \
      "Sistem genelinde kurulu bir Ccenter (Solwind'in 0.4.0'dan önceki adı) var. İki kurulum karışmasın diye önce onu kaldır; dosyaları:"
    for _f in "$_p/share/ccenter" "$_p/bin/ccenter" "$_p/share/applications/ccenter.desktop" \
              "$_p/share/icons/hicolor/scalable/apps/ccenter.svg" "$_p/lib/systemd/user/ccenter.service" "$_p/share/licenses/ccenter"; do
        if [ -e "$_f" ]; then say "  $_f"; fi
    done
    exit 1
done
# A running copy under either name (also a development copy) would be cut off from its files
_running=$(qs list --all 2>/dev/null | sed -n 's/^ *Config path: \(.*\(solwind\|ccenter\)\/shell\.qml\)$/\1/Ip' | head -n 1)
if [ -n "$_running" ]; then
    # its own launcher quits it (the short command may not be on PATH, or may be another copy's)
    _rd=${_running%/shell.qml}; _q=""
    if [ -x "$_rd/bin/solwind" ]; then _q="$_rd/bin/solwind quit"; elif [ -x "$_rd/bin/ccenter" ]; then _q="$_rd/bin/ccenter quit"; fi
    if [ "$_running" = "$oc_app_real/shell.qml" ]; then
        t "Ccenter (Solwind's old name) is running right now. Quit it first (in the app 'Quit', or: ccenter quit), then try again." \
          "Ccenter (Solwind'in eski adı) şu an çalışıyor. Önce kapat (uygulamada 'Tamamen kapat' ya da: ccenter quit), sonra tekrar dene."
    elif [ -n "$_q" ]; then
        t "Solwind is running right now ($_running). Quit it first (in the app 'Quit', or: $_q), then try again." \
          "Solwind şu an çalışıyor ($_running). Önce kapat (uygulamada 'Tamamen kapat' ya da: $_q), sonra tekrar dene."
    else
        t "Solwind is running right now ($_running). Quit it first (in the app 'Quit'), then try again." \
          "Solwind şu an çalışıyor ($_running). Önce kapat (uygulamada 'Tamamen kapat'), sonra tekrar dene."
    fi
    exit 1
fi
oc_find
# A ccenter.service that is on but isn't the install found here (another data folder, a hand-made unit): its launcher
# can't see Solwind, so both could start on login and drive the fans at once
if [ -n "$oc_unit_other" ]; then
    t "A Ccenter background service is turned on that this installer doesn't know ($oc_unit_other)." \
      "Bu kurulumun tanımadığı bir Ccenter arka plan servisi açık ($oc_unit_other)."
    t "It could start next to Solwind on login. Turn it off first: systemctl --user disable --now ccenter.service" \
      "Oturum açılınca Solwind'in yanında başlayabilir. Önce kapat: systemctl --user disable --now ccenter.service"
    exit 1
fi

# ---------- plan: where each part goes, and whether it is writable ----------
if ! can_write "$app"; then
    _o=$(owner "$app")
    t "Can't create the app folder: $app (owner: $_o)." "Uygulama klasörü oluşturulamıyor: $app (sahibi: $_o)."
    t "This means your own user folder isn't writable; the install stopped without touching anything." \
      "Bu, kullanıcı klasörünün kendisinin yazılamadığı anlamına gelir; kurulum hiçbir şeye dokunmadan durduruldu."
    exit 1
fi
# If a file with the same name belongs to another program (doesn't mention our app path), leave it alone
foreign() { [ -e "$1" ] && ! grep -q "$app" "$1" 2>/dev/null; }
desktop=""; desktop_note=$(t "$data/applications is not writable" "$data/applications yazılamıyor")
if foreign "$data/applications/solwind.desktop"; then
    desktop_note=$(t "$data/applications/solwind.desktop belongs to another program; it won't be touched" \
                     "$data/applications/solwind.desktop başka bir programa ait; ona dokunulmayacak")
elif can_write "$data/applications"; then
    desktop=$data/applications/solwind.desktop
fi
unitdir=""; unit_note=$(t "the systemd user folders are not writable" "systemd kullanıcı klasörleri yazılamıyor")
for ud in "$data/systemd/user" "$config/systemd/user"; do
    if foreign "$ud/solwind.service"; then
        unit_note=$(t "$ud/solwind.service belongs to another program; it won't be touched" "$ud/solwind.service başka bir programa ait; ona dokunulmayacak")
        break
    fi
    can_write "$ud" && { unitdir=$ud; break; }
done
link=""; link_note=""
bindir=$HOME/.local/bin
if [ -e "$bindir/solwind" ] && [ "$(readlink -f "$bindir/solwind")" != "$launcher_real" ]; then
    link_note=$(t "$bindir/solwind already exists and doesn't belong to Solwind; it won't be touched." \
                  "$bindir/solwind zaten var ve Solwind'e ait değil; ona dokunulmayacak.")
elif can_write "$bindir"; then
    link=$bindir/solwind
else
    _o=$(owner "$bindir")
    link_note=$(t "$bindir is not writable (owner: $_o); the terminal command link will be skipped." \
                  "$bindir yazılamıyor (sahibi: $_o); terminal komutu bağlantısı atlanacak.")
fi

# Short 'solwind' command: ~/.local/bin is not in PATH everywhere (e.g. bash on Arch doesn't add it). If the command's
# folder is missing from the PATH of the terminal running this script, offer to add it to the shells' startup files.
# The added lines sit between markers, so uninstall.sh can remove exactly them.
cmddir=$bindir; [ -n "$link" ] || cmddir=$app/bin
mark_begin="# >>> solwind >>>"
mark_end="# <<< solwind <<<"
in_path() { case ":$PATH:" in *":$1:"*) return 0 ;; esac; return 1; }
writable_file() { if [ -e "$1" ]; then [ -w "$1" ]; else can_write "$(dirname "$1")"; fi; }
rcfiles=""; fishconf=""; path_done=""          # path_done: an earlier install already added the block
if ! in_path "$cmddir"; then
    # bash / zsh: if the shell is in use (its startup file exists or it is the login shell) and our lines aren't there yet
    for _rc in "$HOME/.bashrc:bash" "$HOME/.zshrc:zsh"; do
        _f=${_rc%%:*}; _sh=${_rc##*:}
        if [ -f "$_f" ] || [ "${SHELL##*/}" = "$_sh" ]; then
            if grep -qF "$mark_begin" "$_f" 2>/dev/null; then path_done=1
            elif [ -f "$_f" ] && oc_adds "$_f" "$cmddir"; then
                path_done=1                    # Ccenter's block adds the same folder (it becomes ours, or stays)
            elif writable_file "$_f"; then rcfiles="$rcfiles$_f$nl"; fi
        fi
    done
    # fish: a separate drop-in file in conf.d (config.fish is not touched), unless fish already has the folder in PATH
    _fc=$config/fish/conf.d/solwind.fish
    if command -v fish >/dev/null 2>&1 && [ -d "$config/fish" ] && ! fish -c "contains -- '$cmddir' \$PATH" </dev/null >/dev/null 2>&1; then
        if [ ! -e "$_fc" ] || grep -qF "$mark_begin" "$_fc"; then writable_file "$_fc" && fishconf=$_fc; fi
    fi
fi

t "Will be installed (all in your home folder, no admin password needed):" "Kurulacaklar (hepsi senin kullanıcı klasöründe, yönetici şifresi gerekmez):"
t "  $app/   app, command and icon" "  $app/   uygulama, komut ve ikon"
if [ -n "$desktop" ]; then t "  $desktop   app menu entry" "  $desktop   uygulama menüsü kaydı"
else t "  (skipped) app menu entry: $desktop_note" "  (atlanacak) uygulama menüsü kaydı: $desktop_note"; fi
if [ -n "$unitdir" ] && command -v systemctl >/dev/null 2>&1 && [ "$(systemctl --user is-enabled solwind.service 2>/dev/null)" = enabled ]; then
    t "  $unitdir/solwind.service   background service (it is on and stays on)" "  $unitdir/solwind.service   arka plan servisi (açık, açık kalır)"
elif [ -n "$unitdir" ]; then t "  $unitdir/solwind.service   background service (off unless you turn it on)" "  $unitdir/solwind.service   arka plan servisi (sen açmadıkça kapalı)"
else t "  (skipped) background service: $unit_note" "  (atlanacak) arka plan servisi: $unit_note"; fi
if [ -n "$link" ]; then t "  $link   terminal command (a link to the app)" "  $link   terminal komutu (uygulamaya bağlantı)"
else t "  (skipped) terminal command: $link_note" "  (atlanacak) terminal komutu: $link_note"; fi
if oc_found; then
    say ""
    t "Ccenter (Solwind's name before 0.4.0) is installed; Solwind replaces it:" \
      "Ccenter (Solwind'in 0.4.0'dan önceki adı) kurulu; Solwind onun yerine geçer:"
    oc_list "$cmddir"
    if [ -n "$oc_unit_on" ] && [ -n "$unitdir" ]; then
        t "  Solwind's background service is turned on and started instead" \
          "  yerine Solwind'in arka plan servisi açılıp başlatılacak"
    elif [ -n "$oc_unit_on" ]; then
        t "  Solwind's background service can't be installed ($unit_note): turn it on later in the app" \
          "  Solwind'in arka plan servisi kurulamıyor ($unit_note): sonra uygulamadan aç"
    fi
    if [ -f "$oc_cfg/settings.json" ] && [ ! -e "$config/solwind/settings.json" ]; then
        t "  Your settings ($oc_cfg) are copied to $config/solwind when Solwind first starts; the old folder stays." \
          "  Ayarların ($oc_cfg), Solwind ilk açıldığında $config/solwind klasörüne kopyalanır; eski klasör yerinde kalır."
    fi
fi
say ""
ask "Continue?" "Devam edilsin mi?" y || { t "Install cancelled, nothing changed." "Kurulum iptal edildi, hiçbir şey değişmedi."; exit 0; }

# ---------- install ----------
# The new copy is built next to the old one and swapped in only when complete: a copy that fails half way (disk
# full, ...) leaves the installed app as it was
new=$app.new
old=$app.old
rm -rf "$new" "$old"
mkdir -p "$new/bin" "$new/dist"
cp -r "$src/shell.qml" "$src/VERSION" "$src/LICENSE" "$src/components" "$src/modules" "$src/services" "$src/utils" "$src/scripts" "$src/assets" "$new/"
cp "$src/dist/90-solwind.rules" "$new/dist/"            # used by the app's grant button
install -m 755 "$src/bin/solwind" "$new/bin/solwind"
if [ -e "$app" ]; then mv "$app" "$old"; fi
mv "$new" "$app"
rm -rf "$old"
# The menu entry and the service call the launcher by full path (the session's PATH may not have ~/.local/bin);
# a path with spaces is quoted
exec_path=$launcher
case "$launcher" in *[[:space:]]*) exec_path="\"$launcher\"" ;; esac
if [ -n "$desktop" ]; then
    mkdir -p "$(dirname "$desktop")"
    sed -e "s|@BINDIR@/solwind|$exec_path|g" -e "s|^Icon=solwind\$|Icon=$app/assets/solwind.svg|" \
        "$src/dist/solwind.desktop" > "$desktop"
fi
if [ -n "$unitdir" ]; then
    mkdir -p "$unitdir"
    sed "s|@BINDIR@/solwind|$exec_path|g" "$src/dist/solwind.service" > "$unitdir/solwind.service"
fi
if [ -n "$link" ]; then
    mkdir -p "$(dirname "$link")"
    ln -sf "$launcher" "$link"
fi
if oc_found; then
    oc_remove "$cmddir"
    if [ -z "$oc_failed" ]; then
        t "Ccenter (the old name) removed." "Ccenter (eski ad) kaldırıldı."
    else
        t "Ccenter (the old name) removed, except (remove these by hand):" "Ccenter (eski ad) kaldırıldı, şunlar hariç (elle sil):"
        _ifs=$IFS; IFS=$nl; set -f
        for _f in $oc_failed; do say "  $_f"; done
        IFS=$_ifs; set +f
    fi
fi
if command -v systemctl >/dev/null 2>&1 && [ -n "$unitdir$oc_unit" ]; then systemctl --user daemon-reload 2>/dev/null || true; fi
# Ccenter's background service was on: Solwind's takes its place. Turned on right away, so stopping the installer
# later (Ctrl+C at a question) doesn't lose it; started only at the end (after the keyboard permission step, which
# changes the light's files).
oc_enabled=""
if [ -n "$oc_unit_on" ] && [ -n "$unitdir" ]; then
    if systemctl --user enable solwind.service >/dev/null 2>&1; then oc_enabled=1
    else
        t "Could not turn on the background service; in the app: Fan control > Background > Start in the background on login." \
          "Arka plan servisi açılamadı; uygulamada: Fan Kontrol > Arka plan > Oturum açılınca arka planda başlat."
    fi
fi
t "App installed." "Uygulama kuruldu."
say ""

# ---------- short 'solwind' command (optional) ----------
path_added=""
if [ -n "$rcfiles$fishconf" ]; then
    t "Short 'solwind' command" "Kısa 'solwind' komutu"
    say "-----------------------"
    t "$cmddir is not in your terminal's PATH, so typing 'solwind' would say 'command not found'." \
      "$cmddir terminalinin PATH'inde değil; 'solwind' yazınca 'command not found' der."
    t "Solwind can add it with a short block (between '$mark_begin' lines) to:" \
      "Solwind bunu şu dosyalara kısa bir blokla ('$mark_begin' satırları arasında) ekleyebilir:"
    _ifs=$IFS; IFS=$nl; set -f
    for _f in $rcfiles $fishconf; do say "  $_f"; done
    IFS=$_ifs; set +f
    t "Nothing else in these files is changed. Undo: ./uninstall.sh removes exactly this block." \
      "Bu dosyalarda başka hiçbir şey değişmez. Geri alma: ./uninstall.sh tam olarak bu bloğu siler."
    if ask "Add it?" "Eklensin mi?" y; then
        _ifs=$IFS; IFS=$nl; set -f
        for _f in $rcfiles; do
            # make sure the block starts on its own line
            if [ -s "$_f" ] && [ -n "$(tail -c1 "$_f")" ]; then printf '\n' >> "$_f"; fi
            printf '%s\n%s\n%s\n' "$mark_begin" \
                "case \":\$PATH:\" in *\":$cmddir:\"*) ;; *) export PATH=\"$cmddir:\$PATH\" ;; esac" "$mark_end" >> "$_f"
        done
        IFS=$_ifs; set +f
        if [ -n "$fishconf" ]; then
            mkdir -p "$(dirname "$fishconf")"
            printf "%s\nfish_add_path -g '%s'\n%s\n" "$mark_begin" "$cmddir" "$mark_end" > "$fishconf"
        fi
        path_added=1
        t "Added. Works in newly opened terminals." "Eklendi. Yeni açtığın terminallerde çalışır."
    else
        t "Not added. You can always use the full path: $launcher" "Eklenmedi. Tam yolu her zaman kullanabilirsin: $launcher"
    fi
    say ""
fi

# ---------- keyboard light permission (optional) ----------
# The rule gives write access to the keyboard light's files to THIS user only (their owner becomes this user's id).
# Older versions made them writable for every user (0666); that is reset first.
uid=$(id -u)
reset_perms='for d in /sys/class/leds/*::kbd_backlight /sys/class/leds/*::kbd_backlight_* /sys/class/leds/*::kbd_zoned_backlight-*; do [ -d "$d" ] || continue; for f in brightness multi_intensity; do [ -e "$d/$f" ] && chown root:root "$d/$f" && chmod 0644 "$d/$f"; done; [ -e "$d/kbd_rgb_mode" ] && chown root:root "$d/kbd_rgb_mode"; done; for f in /sys/devices/platform/hp-wmi/rgb_zones/zone*; do [ -e "$f" ] && chown root:root "$f" && chmod 0644 "$f"; done; true'
led="" wfile=""                                     # the light, and a file of it that the permission makes writable
for d in /sys/class/leds/*::kbd_backlight /sys/class/leds/*::kbd_backlight_* /sys/class/leds/*::kbd_zoned_backlight-*; do
    [ -e "$d/brightness" ] && { led=$d; wfile=$d/brightness; break; }
done
# HP four-zone keyboards with older copies of the hp-wmi-fan-and-backlight-control module: colour files on the device
if [ -z "$led" ] && [ -e /sys/devices/platform/hp-wmi/rgb_zones/zone00 ]; then
    led=/sys/devices/platform/hp-wmi/rgb_zones; wfile=$led/zone00
fi
legacy=""                                           # an older, less strict rule (under either name)
for _r in "$rule" "$oc_rule"; do
    if [ -f "$_r" ] && grep -q -e 'chmod 0666' -e '/bin/sh' "$_r"; then legacy=1; fi
done
oldrule=""; [ ! -f "$oc_rule" ] || oldrule=1        # Ccenter's rule (the old name): replaced by ours
if [ -z "$led" ]; then
    t "No keyboard light found; skipped the keyboard permission step." "Klavye ışığı bulunamadı; klavye izni adımı atlandı."
    if [ -n "$oldrule" ]; then
        t "Ccenter's permission rule ($oc_rule) is still there; Solwind doesn't use it. To remove it: sudo rm $oc_rule" \
          "Ccenter'in izin kuralı ($oc_rule) duruyor; Solwind onu kullanmaz. Silmek için: sudo rm $oc_rule"
    fi
elif [ -w "$wfile" ] && [ -z "$legacy" ] && [ -z "$oldrule" ]; then
    t "The keyboard light is already writable; no permission step needed." "Klavye ışığına zaten yazılabiliyor; izin adımı gerekmiyor."
else
    t "Keyboard light permission (optional)" "Klavye ışığı izni (isteğe bağlı)"
    say "--------------------------------"
    if [ -n "$legacy" ]; then
        t "An older, less strict keyboard permission is installed, from when Solwind was called Ccenter (it may let EVERY user of this computer change the keyboard light)." \
          "Eski, daha gevşek bir klavye izni kurulu, Solwind'in adı Ccenter iken eklenmiş (bu bilgisayardaki HER kullanıcının klavye ışığını değiştirmesine izin verebilir)."
        t "The new version gives access to your user only." "Yeni sürüm sadece senin kullanıcına izin verir."
    elif [ -n "$oldrule" ]; then
        t "Ccenter (Solwind's old name) added this permission as $oc_rule." \
          "Bu izni Ccenter (Solwind'in eski adı) $oc_rule olarak eklemiş."
        t "It is replaced with Solwind's own rule, under the new name." "Yeni addaki Solwind kuralıyla değiştirilir."
    fi
    t "Light found: ${led##*/}" "Bulunan ışık: ${led##*/}"
    t "For Solwind to change the keyboard light, your user needs write access to the light's color files" \
      "Solwind'in klavye ışığını değiştirebilmesi için kullanıcının ışığın renk dosyalarına"
    t "(and brightness files, if it has them): a permanent version of what you do by hand with 'sudo tee'." \
      "(varsa parlaklık dosyalarına da) yazabilmesi gerekir: elle 'sudo tee' ile yaptığın şeyin kalıcı hali."
    say ""
    if [ -n "$oldrule" ]; then
        t "What is done : the file $rule is added (a udev rule) and the old $oc_rule is deleted; no other file is touched." \
          "Ne yapılır : $rule dosyası eklenir (udev kuralı), eski $oc_rule silinir; başka hiçbir dosyaya dokunulmaz."
    else
        t "What is done : the file $rule is added (a udev rule); no other file is touched." \
          "Ne yapılır : $rule dosyası eklenir (udev kuralı); başka hiçbir dosyaya dokunulmaz."
    fi
    t "What changes : only your user (id $uid) can change the keyboard light." \
      "Ne değişir : sadece senin kullanıcın (id $uid) klavye ışığını değiştirebilir."
    t "               It gives no access to anything else (fans, files, the system) and nothing to other users." \
      "             Başka hiçbir şeye (fanlar, dosyalar, sistem) erişim vermez, diğer kullanıcılara hiçbir şey vermez."
    t "Undo         : ./uninstall.sh, or in the app Keyboard > 'Remove permission'." \
      "Geri alma  : ./uninstall.sh ya da uygulamada Klavye > 'İzni kaldır'."
    t "You can also grant it later with the 'Grant permission' button in the app." \
      "İstersen şimdi değil, sonra uygulamadaki 'İzin ver' butonundan da verebilirsin."
    say ""
    if [ -n "$legacy$oldrule" ]; then _q_en="Replace it with the new version? (asks for the admin password)"; _q_tr="Yeni sürümle değiştirilsin mi? (yönetici şifresi istenecek)"; _def=y
    else _q_en="Grant the keyboard light permission? (asks for the admin password)"; _q_tr="Klavye ışığı izni verilsin mi? (yönetici şifresi istenecek)"; _def=n; fi
    if ask "$_q_en" "$_q_tr" "$_def"; then
        sed "s/@UID@/$uid/g" "$app/dist/90-solwind.rules" > "$app/dist/90-solwind.rules.$uid"
        if [ -n "$oldrule" ]; then
            t "Will run (as admin): check the rule, give the light files back to root, install $rule for user id $uid, delete $oc_rule, reload udev" \
              "Çalıştırılacak (yönetici olarak): kuralı denetle, ışık dosyalarını root'a geri ver, $rule kuralını kullanıcı id $uid için kur, $oc_rule kuralını sil, udev'i yenile"
        else
            t "Will run (as admin): check the rule, give the light files back to root, install $rule for user id $uid, reload udev" \
              "Çalıştırılacak (yönetici olarak): kuralı denetle, ışık dosyalarını root'a geri ver, $rule kuralını kullanıcı id $uid için kur, udev'i yenile"
        fi
        # The rule is copied to a root-owned temporary file and checked there, so it can't change after the check
        if sudo install -Dm644 "$app/dist/90-solwind.rules.$uid" "$rule.new" \
           && { sudo sh "$app/scripts/kbd-rule-check.sh" "$rule.new" "$uid" || { sudo rm -f "$rule.new"; false; }; } \
           && sudo sh -c "$reset_perms" \
           && sudo mv "$rule.new" "$rule" \
           && { [ -z "$oldrule" ] || sudo rm -f "$oc_rule"; } \
           && sudo udevadm control --reload-rules \
           && sudo udevadm trigger --action=change --subsystem-match=leds \
           && sudo udevadm trigger --action=change --subsystem-match=platform --sysname-match=hp-wmi \
           && sudo udevadm settle; then
            if [ -w "$wfile" ]; then t "Permission granted." "İzin verildi."
            else t "The rule was added but the permission isn't visible yet; it applies after a reboot." "Kural eklendi ama izin henüz görünmüyor; yeniden başlatınca geçerli olur."; fi
        else
            t "Could not grant the permission (no password, or an error). You can grant it later from the app." \
              "İzin verilemedi (şifre girilmedi ya da hata). Sonra uygulamadan verebilirsin."
        fi
        rm -f "$app/dist/90-solwind.rules.$uid"
    else
        t "Keyboard permission not changed. You can do it any time in the Keyboard tab." \
          "Klavye izni değiştirilmedi. Klavye sekmesinden istediğin zaman yapabilirsin."
    fi
fi

# The background service turned on above (as Ccenter's was) starts now. systemctl returns as soon as the process is
# started, so it counts as started only once the app answers (at most ~10 s).
if [ -n "$oc_enabled" ]; then
    say ""
    systemctl --user start solwind.service >/dev/null 2>&1 || true
    _n=0
    while [ "$_n" -lt 20 ] && ! "$launcher" status >/dev/null 2>&1; do sleep 0.5; _n=$((_n + 1)); done
    if "$launcher" status >/dev/null 2>&1; then
        t "Background service turned on and started (as Ccenter's was)." "Arka plan servisi açıldı ve başlatıldı (Ccenter'inki gibi)."
    else
        t "The background service is turned on but didn't start; to see why: systemctl --user status solwind.service" \
          "Arka plan servisi açıldı ama başlamadı; nedeni için: systemctl --user status solwind.service"
    fi
fi

say ""
t "Install complete." "Kurulum tamam."
[ -n "$desktop" ] && t "  To start: 'Solwind' in the app menu" "  Başlatmak: uygulama menüsünden 'Solwind'"
if [ -n "$path_added$path_done" ]; then
    t "  Terminal: solwind   (in newly opened terminals)" "  Terminal: solwind   (yeni açtığın terminallerde)"
elif in_path "$cmddir"; then
    say "  Terminal: solwind"
else
    t "  Terminal: $launcher" "  Terminal: $launcher"
fi
t "  To remove: ./uninstall.sh" "  Kaldırmak: ./uninstall.sh"
if oc_found; then
    t "  The old 'ccenter' command is gone: keybinds and scripts that run ccenter (or its full path) need 'solwind' now," \
      "  Eski 'ccenter' komutu kaldırıldı: ccenter'ı (ya da tam yolunu) çalıştıran kısayollar ve betikler artık 'solwind' kullanmalı;"
    t "  and window rules for the class 'ccenter' need 'solwind'." "  'ccenter' sınıfı için yazılmış pencere kuralları da 'solwind' olmalı."
fi
