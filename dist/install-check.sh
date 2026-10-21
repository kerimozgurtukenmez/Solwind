#!/bin/sh
# Checks for the Makefile, so make install / make uninstall never change the files of a running Solwind, install
# next to another copy or over another program's files, and the messages after them. Only reads. Not run for
# packaging (DESTDIR).
# Usage: sh dist/install-check.sh install|uninstall|installed|removed <prefix> [1 = the menu folder is new]
# As root the user is the one who asked for it (sudo: SUDO_USER, doas: DOAS_USER, pkexec: PKEXEC_UID): only their
# home folder is looked at, read only.
set -u
mode=$1
prefix=$2
apps_new=${3:-}
here=$(dirname "$0")
nl='
'
std=""; case "$prefix" in /usr|/usr/local) std=1 ;; esac
app=$prefix/share/solwind
unit=$prefix/lib/systemd/user/solwind.service

user=""
if [ "$(id -u)" = 0 ]; then
    if [ -n "${SUDO_USER:-}" ]; then user=$SUDO_USER
    elif [ -n "${DOAS_USER:-}" ]; then user=$DOAS_USER
    elif [ -n "${PKEXEC_UID:-}" ]; then user=$(getent passwd "$PKEXEC_UID" | cut -d: -f1); fi
    home=""; [ -z "$user" ] || home=$(getent passwd "$user" | cut -d: -f6)
    data=$home/.local/share config=$home/.config      # (their XDG variables aren't known here)
else
    home=$HOME
    data=${XDG_DATA_HOME:-$HOME/.local/share} config=${XDG_CONFIG_HOME:-$HOME/.config}
fi

# Message language: SOLWIND_LANG, else the language chosen in the app, else the system locale
lang=${SOLWIND_LANG:-}
settings=$config/solwind/settings.json
[ -e "$settings" ] || settings=$config/ccenter/settings.json
if [ -z "$lang" ]; then
    grep -q '"lang": *"tr"' "$settings" 2>/dev/null && lang=tr
    grep -q '"lang": *"en"' "$settings" 2>/dev/null && lang=en
fi
[ -n "$lang" ] || case "${LC_ALL:-${LC_MESSAGES:-${LANG:-}}}" in tr*) lang=tr ;; *) lang=en ;; esac
t() { if [ "$lang" = tr ]; then printf '%s\n' "$2"; else printf '%s\n' "$1"; fi; }
real_path() { readlink -f "$1" 2>/dev/null || printf '%s\n' "$1"; }
# how to run this again, with the prefix if it isn't the default one
again="sudo make $mode"; [ "$prefix" = /usr/local ] || again="$again PREFIX=$prefix"

# As root, the user's home must be known: an older copy in it would be missed (and keep being the one that runs)
need_home() {
    [ -n "$home" ] && [ -d "$home" ] && return 0
    t "Couldn't tell whose home folder to check for an older copy. Run it from your own user with sudo: $again" \
      "Eski bir kopya için hangi kullanıcı klasörüne bakılacağı anlaşılamadı. Kendi kullanıcınla sudo ile çalıştır: $again"
    exit 1
}
# Without root, the folders must be writable
need_rights() {
    [ "$(id -u)" = 0 ] && return 0
    _d=$prefix
    while [ ! -e "$_d" ]; do _d=$(dirname "$_d"); done
    [ -w "$_d" ] && return 0
    t "This changes files under $prefix: run it with sudo ($again)." "Bu, $prefix altındaki dosyaları değiştirir: sudo ile çalıştır ($again)."
    exit 1
}
# Is the installed app running (for any user)? The folders qs was started with (-p). Without pgrep it can't tell.
running() {
    if ! command -v pgrep >/dev/null 2>&1; then echo "?"; return 0; fi
    _want=$(real_path "$app")
    for _p in $(pgrep -x 'qs|quickshell' 2>/dev/null); do
        tr '\0' '\n' < "/proc/$_p/cmdline" 2>/dev/null | awk 'prev == "-p" || prev == "--path" { print; exit } { prev = $0 }'
    done | while IFS= read -r _d; do
        _d=${_d%/}; _d=${_d%/shell.qml}
        [ "$(real_path "$_d")" = "$_want" ] && printf '%s\n' "$_d"
    done
    return 0
}
not_running() {
    _r=$(running)
    [ -z "$_r" ] && return 0
    if [ "$_r" = "?" ]; then
        t "pgrep (procps) is needed to check that Solwind isn't running; install it, then run $again again." \
          "Solwind'in çalışmadığını denetlemek için pgrep (procps) gerekli; kur, sonra $again komutunu yeniden çalıştır."
    else
        t "Solwind is running from $app. Quit it first (solwind quit, or 'Quit' in the app), then run $again again." \
          "Solwind şu an $app klasöründen çalışıyor. Önce kapat (solwind quit ya da uygulamada 'Tamamen kapat'), sonra $again komutunu yeniden çalıştır."
    fi
    exit 1
}
# The package that owns a file, if a package manager knows it (a distro package must be removed with its tool)
package_of() {
    if command -v pacman >/dev/null 2>&1; then pacman -Qqo "$1" 2>/dev/null
    elif command -v dpkg >/dev/null 2>&1; then dpkg -S "$1" 2>/dev/null | cut -d: -f1
    elif command -v rpm >/dev/null 2>&1; then rpm -qf "$1" 2>/dev/null; fi
    return 0
}
# Your background service is on: its link points at this install's unit file
service_on() {
    for _l in "$config/systemd/user/"*.wants/solwind.service; do
        [ -L "$_l" ] && [ "$(readlink "$_l")" = "$unit" ] && return 0
    done
    return 1
}

case $mode in
install)
    need_rights
    [ "$(id -u)" != 0 ] || need_home
    not_running
    # What Solwind needs to run (as the old install.sh checked)
    _miss=""
    command -v qs >/dev/null 2>&1 || _miss="$_miss quickshell"
    [ -d /usr/lib/qt6/qml/Caelestia ] || _miss="$_miss caelestia-shell"
    if [ -n "$_miss" ]; then
        t "Missing:$_miss. Solwind doesn't work without it; install it first, then run $again again." \
          "Eksik:$_miss. Solwind bunsuz çalışmaz; önce kur, sonra $again komutunu yeniden çalıştır."
        exit 1
    fi
    command -v nbfc >/dev/null 2>&1 || t "Note: nbfc-linux isn't installed; it is needed for fan control." "Not: nbfc-linux kurulu değil; fan kontrolü için gerekli."
    # Files of a distro package are not overwritten
    _pkg=$(package_of "$prefix/bin/solwind")
    if [ -n "$_pkg" ]; then
        t "Solwind under $prefix comes from the package $_pkg; make install would overwrite its files. Use the package, or another PREFIX." \
          "$prefix altındaki Solwind $_pkg paketinden geliyor; make install onun dosyalarının üzerine yazardı. Paketi kullan ya da başka bir PREFIX seç."
        exit 1
    fi
    # A 'solwind' command of another program is not overwritten
    if [ -e "$prefix/bin/solwind" ] && ! head -n 2 "$prefix/bin/solwind" 2>/dev/null | grep -q '^# Solwind launcher'; then
        t "$prefix/bin/solwind already exists and isn't Solwind's; it is not overwritten. Nothing was installed." \
          "$prefix/bin/solwind zaten var ve Solwind'in değil; üzerine yazılmaz. Hiçbir şey kurulmadı."
        exit 1
    fi
    # Another system-wide Solwind under the other standard prefix: two 'solwind' commands on PATH
    if [ -n "$std" ]; then
        for _p in /usr /usr/local; do
            [ "$_p" != "$prefix" ] && [ -f "$_p/share/solwind/shell.qml" ] || continue
            _pkg=$(package_of "$_p/bin/solwind")
            if [ -n "$_pkg" ]; then
                t "Solwind is also installed under $_p, from the package $_pkg. Remove that package first." \
                  "Solwind $_p altında da kurulu, $_pkg paketinden. Önce o paketi kaldır."
            else
                t "Solwind is also installed under $_p. Remove that one first: sudo make uninstall PREFIX=$_p" \
                  "Solwind $_p altında da kurulu. Önce onu kaldır: sudo make uninstall PREFIX=$_p"
            fi
            exit 1
        done
    fi
    # A system-wide Ccenter (the old Makefile installed it under /usr): it can't see Solwind, both could drive the fans
    for _p in /usr /usr/local; do
        [ -f "$_p/share/ccenter/shell.qml" ] || continue
        t "Ccenter (Solwind's name before 0.4.0) is installed under $_p. It can't see Solwind, so both could drive the fans at once." \
          "Ccenter (Solwind'in 0.4.0'dan önceki adı) $_p altında kurulu. Solwind'i göremez, ikisi aynı anda fanları sürebilir."
        t "Remove it first (if you turned its background service on: systemctl --user disable --now ccenter.service), with:" \
          "Önce kaldır (arka plan servisini açtıysan önce: systemctl --user disable --now ccenter.service), şununla:"
        _rm=""
        for _f in "$_p/share/ccenter" "$_p/share/licenses/ccenter" "$_p/bin/ccenter" "$_p/share/applications/ccenter.desktop" \
                  "$_p/share/icons/hicolor/scalable/apps/ccenter.svg" "$_p/lib/systemd/user/ccenter.service"; do
            [ -e "$_f" ] && _rm="$_rm $_f"
        done
        printf '  sudo rm -r%s\n' "$_rm"
        exit 1
    done
    # An install in your home folder made by the old install.sh (Solwind 0.4.0, or Ccenter): the same check
    # make remove-old uses, so what stops here is what remove-old removes (or tells you to do)
    if ! _found=$(sh "$here/remove-old.sh" --check "$home" "$data" "$config"); then
        t "Found an install in your home folder made by the old install.sh (Solwind, or Ccenter, its old name):" \
          "Kullanıcı klasöründe eski install.sh'in yaptığı bir kurulum var (Solwind ya da eski adıyla Ccenter):"
        printf '%s\n' "$_found"
        t "Two installs would mix up (and Ccenter can't see Solwind). Remove it first, without sudo: make remove-old" \
          "İki kurulum karışır (ve Ccenter Solwind'i göremez). Önce kaldır, sudo olmadan: make remove-old"
        t "Then run $again again." "Sonra $again komutunu yeniden çalıştır."
        exit 1
    fi
    ;;
uninstall)
    need_rights
    [ "$(id -u)" != 0 ] || need_home
    # Files of a distro package are removed with the package manager, not here
    _pkg=$(package_of "$prefix/bin/solwind")
    if [ -n "$_pkg" ]; then
        t "Solwind under $prefix comes from the package $_pkg: remove the package instead." \
          "$prefix altındaki Solwind $_pkg paketinden geliyor: onun yerine paketi kaldır."
        exit 1
    fi
    not_running
    # Without its file, a turned-on service can't be turned off cleanly any more (systemctl --user is yours, not root's)
    if service_on; then
        t "Your background service is on. Turn it off first: systemctl --user disable solwind.service (or in the app), then run $again again." \
          "Arka plan servisin açık. Önce kapat: systemctl --user disable solwind.service (ya da uygulamadan), sonra $again komutunu yeniden çalıştır."
        exit 1
    fi
    ;;
installed)
    echo
    t "Solwind is installed under $prefix." "Solwind $prefix altına kuruldu."
    if [ -n "$std" ]; then
        t "  Start: solwind   (or 'Solwind' in the app menu)" "  Başlatmak: solwind   (ya da uygulama menüsünden 'Solwind')"
        if [ -n "$apps_new" ]; then
            # (Quickshell's desktop entry monitor skips folders that don't exist when it starts: desktopentrymonitor.cpp)
            t "  $prefix/share/applications is new: the app menu shows Solwind after you log in again (or restart the shell)." \
              "  $prefix/share/applications yeni oluştu: uygulama menüsü Solwind'i yeniden oturum açınca (ya da kabuğu yeniden başlatınca) gösterir."
        fi
    else
        # (on the machine this was written on, systemd's user unit path, PATH and XDG_DATA_DIRS held /usr and
        # /usr/local but no other prefix; other systems may differ, hence "may not")
        t "  Start: $prefix/bin/solwind" "  Başlatmak: $prefix/bin/solwind"
        t "  $prefix isn't a standard place: the command may not be on PATH, the menu entry may not show, and the" \
          "  $prefix standart bir yer değil: komut PATH'te olmayabilir, menü kaydı görünmeyebilir ve"
        t "  background service can't be turned on (systemd doesn't look for services under $prefix)." \
          "  arka plan servisi açılamaz (systemd $prefix altında servis aramaz)."
    fi
    if service_on; then
        t "  Your background service is on; it starts on your next login. To start it now:" \
          "  Arka plan servisin açık; sonraki oturum açılışında başlar. Şimdi başlatmak için:"
        echo "    systemctl --user daemon-reload && systemctl --user start solwind.service"
    elif [ -n "$std" ]; then
        t "  Start in the background on login (optional): in the app, Fan control > Background" \
          "  Oturum açılınca arka planda başlatmak (isteğe bağlı): uygulamada Fan Kontrol > Arka plan"
    fi
    if [ -f /etc/udev/rules.d/90-ccenter.rules ]; then
        t "  The keyboard permission is still Ccenter's rule (it keeps working). To move it to the new name: sudo make udev" \
          "  Klavye izni hâlâ Ccenter'in kuralı (çalışmaya devam eder). Yeni ada taşımak için: sudo make udev"
    elif [ ! -f /etc/udev/rules.d/90-solwind.rules ] && ls -d /sys/class/leds/*kbd_backlight* /sys/class/leds/*kbd_zoned_backlight* >/dev/null 2>&1; then
        t "  Keyboard light permission (optional): in the app, Keyboard > Grant permission, or: sudo make udev" \
          "  Klavye ışığı izni (isteğe bağlı): uygulamada Klavye > İzin ver, ya da: sudo make udev"
    fi
    if [ -f "$config/ccenter/settings.json" ] && [ ! -e "$config/solwind/settings.json" ]; then
        t "  Your Ccenter settings are copied over when Solwind first starts." "  Ccenter ayarların Solwind ilk açıldığında kopyalanır."
    fi
    ;;
removed)
    echo
    t "Solwind removed. Your settings stay in $config/solwind; delete it by hand if you want." \
      "Solwind kaldırıldı. Ayarların $config/solwind klasöründe duruyor; istersen elle sil."
    ;;
esac
exit 0
