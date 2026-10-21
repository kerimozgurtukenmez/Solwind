#!/bin/sh
# make remove-old: removes an install in your home folder made by the old install.sh, Solwind's (0.4.0) or Ccenter's
# (Solwind's name before 0.4.0), so the system-wide make install is the only copy. Run as you, not with sudo.
# A file counts only if it belongs to that install (its app folder with its launcher in bin/, files that run or link
# to that launcher, links of its background service), so nothing of another program is touched. Your settings stay
# (~/.config/solwind; Ccenter's ~/.config/ccenter is copied over on Solwind's first start).
# PATH blocks the old install.sh added (between "# >>> name >>>" lines, in ~/.bashrc, ~/.zshrc or fish's conf.d):
# one that adds the old app's own folder goes (that folder is deleted); one that adds ~/.local/bin stays, other
# programs may use that folder.
#   sh remove-old.sh                          find, show, ask, remove
#   sh remove-old.sh --check <home> <data> <config>
#       only find (also as root, for make install's check; no systemctl, no qs): prints what remove-old would remove
#       and what needs your hand; exit 1 if there is anything, else 0
set -eu

check=""
if [ "${1:-}" = --check ]; then
    check=1 HOME=$2 data=$3 config=$4
else
    data=${XDG_DATA_HOME:-$HOME/.local/share}
    config=${XDG_CONFIG_HOME:-$HOME/.config}
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
say() { printf '%s\n' "$*"; }
t() { if [ "$lang" = tr ]; then say "$2"; else say "$1"; fi; }
# ask "English" "Türkçe" default(y|n) -> 0 = yes; no answer at all (no terminal) counts as no
ask() {
    if [ "$lang" = tr ]; then _q=$2; _h="[E/h]"; [ "$3" = y ] || _h="[e/H]"
    else _q=$1; _h="[Y/n]"; [ "$3" = y ] || _h="[y/N]"; fi
    printf '%s %s ' "$_q" "$_h"
    if ! read -r _a; then say ""; return 1; fi
    case "$_a" in [eEyY]*) return 0 ;; [hHnN]*) return 1 ;; *) [ "$3" = y ] ;; esac
}
nl='
'
tab=$(printf '\t')

if [ -z "$check" ] && [ "$(id -u)" = 0 ]; then
    t "Run it as yourself, without sudo: make remove-old (it only changes your home folder)." \
      "Kendi kullanıcınla, sudo olmadan çalıştır: make remove-old (sadece kullanıcı klasörünü değiştirir)."
    exit 1
fi

# The real path (a linked home folder, e.g. /home -> /var/home: qs and readlink -f report the real one)
real_path() { readlink -f "$1" 2>/dev/null || printf '%s\n' "$1"; }
# Does file $1 hold only whole blocks: every begin line $2 closed by an end line $3, none inside another, at least
# one? A block missing its end line would make sed delete everything after its begin line.
block_ok() {
    awk -v b="$2" -v e="$3" '$0 == b { if (open) bad = 1; open = 1; n++; next }
        $0 == e { if (!open) bad = 1; open = 0 }
        END { exit (bad || open || n == 0) }' "$1"
}
# Can the file (the real one, if it's a link) be edited in place? sed -i writes a new file next to it.
editable() { _r=$(real_path "$1"); [ -f "$_r" ] && [ -w "$_r" ] && [ -w "$(dirname "$_r")" ]; }
# Does file $1 add exactly folder $2 to PATH? (the lines the old install.sh wrote)
adds() {
    case "$1" in
        *.fish) grep -qxF -e "fish_add_path -g '$2'" -e "fish_add_path -g $2" "$1" ;;
        *) grep -qxF "case \":\$PATH:\" in *\":$2:\"*) ;; *) export PATH=\"$2:\$PATH\" ;; esac" "$1" ;;
    esac
}

# ---------- what is there ----------
# todo: one action per line, fields separated by tabs:
#   rm <path> | unit <name> <file> | block <name> <file> | link <file> (a leftover link of a background service)
# notes: what stays, and why; manual: what you have to do by hand before make install
todo="" notes="" manual="" running="" was_on=""
for n in solwind ccenter; do
    app=$data/$n
    launcher=$app/bin/$n
    units=""
    # the app folder, with the launcher inside it (make install keeps the launcher in <prefix>/bin instead)
    if [ -f "$app/shell.qml" ] && [ -f "$app/services/Nbfc.qml" ] && [ -f "$launcher" ]; then
        todo="${todo}rm$tab$app$nl"
        for f in "$app.new" "$app.old"; do [ -d "$f" ] && todo="${todo}rm$tab$f$nl"; done
        if [ -z "$check" ] && qs list --all 2>/dev/null | grep -qF "$(real_path "$app")/shell.qml"; then running="$running  $app$nl"; fi
    fi
    for f in "$data/applications/$n.desktop" "$data/systemd/user/$n.service" "$config/systemd/user/$n.service"; do
        [ -f "$f" ] && grep -qF "$launcher" "$f" || continue
        case "$f" in
            *.service) todo="${todo}unit$tab$n$tab$f$nl"; units="$units$f$nl" ;;
            *) todo="${todo}rm$tab$f$nl" ;;
        esac
    done
    if [ -L "$HOME/.local/bin/$n" ] && [ "$(real_path "$HOME/.local/bin/$n")" = "$(real_path "$data")/$n/bin/$n" ]; then
        todo="${todo}rm$tab$HOME/.local/bin/$n$nl"
    fi
    # links of its background service (turned on): to its own unit file, to nothing (its files are gone), or, for
    # Ccenter, to a unit that isn't from this install (then you turn it off)
    for l in "$config/systemd/user/"*.wants/"$n.service"; do
        [ -L "$l" ] || continue
        target=$(readlink "$l")
        case "$nl$units" in *"$nl$target$nl"*) was_on=1; continue ;; esac
        if [ ! -e "$l" ]; then todo="${todo}link$tab$l$nl"
        elif [ "$n" = ccenter ]; then
            manual="$manual$(t "  ccenter.service is turned on ($target), not from an install found here; turn it off: systemctl --user disable --now ccenter.service" \
                               "  ccenter.service açık ($target), burada bulunan bir kuruluma ait değil; kapat: systemctl --user disable --now ccenter.service")$nl"
        fi
    done
    for f in "$HOME/.bashrc" "$HOME/.zshrc" "$config/fish/conf.d/$n.fish"; do
        [ -f "$f" ] && grep -qxF "# >>> $n >>>" "$f" || continue
        if ! adds "$f" "$app/bin"; then
            notes="$notes$(t "  $f: the PATH block ('# >>> $n >>>') adds a folder other programs may use; it stays (Solwind doesn't need it)." \
                             "  $f: PATH bloğu ('# >>> $n >>>') başka programların da kullanabileceği bir klasör ekliyor; kalıyor (Solwind'in ona ihtiyacı yok).")$nl"
        elif block_ok "$f" "# >>> $n >>>" "# <<< $n <<<" && editable "$f"; then
            todo="${todo}block$tab$n$tab$f$nl"
        else
            notes="$notes$(t "  $f: the PATH block ('# >>> $n >>>') isn't closed or the file isn't writable; left as it is, remove it by hand." \
                             "  $f: PATH bloğu ('# >>> $n >>>') kapanmıyor ya da dosya yazılamıyor; olduğu gibi kalıyor, elle sil.")$nl"
        fi
    done
done

if [ -n "$check" ]; then
    printf '%s' "$todo" | while IFS=$tab read -r kind a b; do
        case $kind in rm|link) say "  $a" ;; *) say "  $b" ;; esac
    done
    printf '%s' "$manual"
    [ -z "$todo$manual" ] || exit 1
    exit 0
fi

t "Removing an install in your home folder made by the old install.sh" "Eski install.sh'in kullanıcı klasörüne yaptığı kurulum kaldırılıyor"
say "=================================================================="
if [ -n "$running" ]; then
    t "It is running right now; quit it first (solwind quit / ccenter quit, or 'Quit' in the app):" \
      "Şu an çalışıyor; önce kapat (solwind quit / ccenter quit ya da uygulamada 'Tamamen kapat'):"
    printf '%s' "$running"
    exit 1
fi
if [ -z "$todo" ]; then
    t "Nothing to remove: no install from the old install.sh in your home folder." \
      "Kaldırılacak bir şey yok: kullanıcı klasöründe eski install.sh'in kurulumu yok."
    [ -z "$notes" ] || printf '%s' "$notes"
    [ -z "$manual" ] || { t "To do by hand:" "Elle yapılacak:"; printf '%s' "$manual"; }
    exit 0
fi
t "Will be removed:" "Kaldırılacaklar:"
printf '%s' "$todo" | while IFS=$tab read -r kind a b; do
    case $kind in
        rm) say "  $a" ;;
        unit) t "  $b   (the background service; turned off first)" "  $b   (arka plan servisi; önce kapatılır)" ;;
        link) t "  $a   (a link of its background service to a file that is gone)" "  $a   (arka plan servisinin artık olmayan bir dosyaya bağlantısı)" ;;
        block) t "  $b   only the PATH block between the '# >>> $a >>>' lines" "  $b   sadece '# >>> $a >>>' satırları arasındaki PATH bloğu" ;;
    esac
done
[ -z "$notes" ] || { t "Left as they are:" "Olduğu gibi kalanlar:"; printf '%s' "$notes"; }
[ -z "$manual" ] || { t "To do by hand (make install asks for it):" "Elle yapılacak (make install bunu ister):"; printf '%s' "$manual"; }
t "Your settings are not touched." "Ayarlarına dokunulmaz."
say ""
ask "Continue?" "Devam edilsin mi?" y || { t "Cancelled, nothing changed." "İptal edildi, hiçbir şey değişmedi."; exit 0; }

# ---------- remove ----------
failed=""
units=""
_ifs=$IFS; IFS=$nl; set -f
for line in $todo; do
    IFS=$tab; set -- $line; IFS=$nl
    case $1 in
        unit) systemctl --user disable --now "$2.service" >/dev/null 2>&1 || true
              rm -f "$3" || failed="$failed  $3$nl"; units="$units$3$nl" ;;
        rm) rm -rf "$2" || failed="$failed  $2$nl" ;;
        link) rm -f "$2" || failed="$failed  $2$nl" ;;
        block) _r=$(real_path "$3")
               if sed -i "/^# >>> $2 >>>\$/,/^# <<< $2 <<<\$/d" "$_r"; then
                   # fish's file held only the block: nothing is left in it
                   case "$3" in *.fish) grep -q '[^[:space:]]' "$_r" || rm -f "$3" || true ;; esac
               else failed="$failed  $3$nl"; fi ;;
    esac
done
IFS=$_ifs; set +f
# systemctl disable removes the service's links; if it couldn't, a link to the deleted file would be left
for l in "$config/systemd/user/"*.wants/*.service; do
    [ -L "$l" ] || continue
    case "$nl$units" in *"$nl$(readlink "$l")$nl"*) rm -f "$l" || true ;; esac
done
[ -z "$units" ] || systemctl --user daemon-reload >/dev/null 2>&1 || true

say ""
if [ -z "$failed" ]; then t "Removed." "Kaldırıldı."
else t "Removed, except these (remove them by hand):" "Kaldırıldı, şunlar hariç (elle sil):"; printf '%s' "$failed"; fi
[ -z "$manual" ] || { t "Still to do by hand:" "Hâlâ elle yapılacak:"; printf '%s' "$manual"; }
t "Next: sudo make install" "Sıradaki: sudo make install"
t "Terminals that already ran solwind or ccenter may remember the old path: run hash -r there, or open a new terminal." \
  "solwind ya da ccenter'i daha önce çalıştırmış terminaller eski yolu hatırlayabilir: orada hash -r çalıştır ya da yeni terminal aç."
if [ -n "$was_on" ]; then
    t "Its background service was on. After installing, turn Solwind's on: systemctl --user enable --now solwind.service" \
      "Arka plan servisi açıktı. Kurduktan sonra Solwind'inkini aç: systemctl --user enable --now solwind.service"
    t "(or in the app: Fan control > Background)" "(ya da uygulamada: Fan Kontrol > Arka plan)"
fi
if [ -f /etc/udev/rules.d/90-ccenter.rules ]; then
    t "The keyboard permission is still Ccenter's rule; it keeps working. To move it to the new name: sudo make udev" \
      "Klavye izni hâlâ Ccenter'in kuralı; çalışmaya devam eder. Yeni ada taşımak için: sudo make udev"
fi
exit 0
