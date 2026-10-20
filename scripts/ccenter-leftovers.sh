# Solwind was called Ccenter before 0.4.0. install.sh and uninstall.sh source this file: it finds what the old name's
# install.sh left in the home folder, so a new install can replace it and uninstall.sh can remove it.
# A file counts only if it belongs to that install (it runs or links to the old launcher, or carries the old PATH
# block's markers), so nothing of another program is touched.
# Needs from the caller: data, config, nl (a newline), t. POSIX sh has no local variables: _oc_-prefixed names.
# Under set -e a function whose last command fails ends the script (bash): every function here that isn't a test
# (used only in conditions) ends with return 0.

# ---------- shared with install.sh / uninstall.sh ----------
# The real path (a linked home folder, e.g. /home -> /var/home: qs and readlink -f report the real one)
real_path() { readlink -f "$1" 2>/dev/null || printf '%s\n' "$1"; }
# Does file $1 hold only whole blocks: every begin line $2 closed by an end line $3, none inside another, at least
# one? A block missing its end line would make sed delete everything after its begin line.
rc_block_ok() {
    awk -v b="$2" -v e="$3" '$0 == b { if (open) bad = 1; open = 1; n++; next }
        $0 == e { if (!open) bad = 1; open = 0 }
        END { exit (bad || open || n == 0) }' "$1"
}
# Can the file (the real one, if it's a link) be edited in place? sed -i writes a new file next to it.
rc_writable() { _oc_r=$(real_path "$1"); [ -f "$_oc_r" ] && [ -w "$_oc_r" ] && [ -w "$(dirname "$_oc_r")" ]; }
# sed -i on the real file: run on a link (dotfile managers) it would replace the link with a plain copy
rc_sed() { sed -i "$1" "$(real_path "$2")"; }

# ---------- Ccenter's install ----------
oc_app=$data/ccenter
oc_launcher=$oc_app/bin/ccenter
oc_app_real=$(real_path "$data")/ccenter
oc_rule=/etc/udev/rules.d/90-ccenter.rules     # its keyboard light permission (root's file: asked about separately)
oc_cfg=$config/ccenter                         # its settings: Solwind copies them over on its first start
oc_begin="# >>> ccenter >>>"
oc_end="# <<< ccenter <<<"

# Sets oc_files (one per line), oc_unit (its service file), oc_unit_on (that service was on), oc_unit_other (a
# ccenter.service that is on but isn't this install's: its file), oc_rc (startup files with its PATH block),
# oc_rc_manual (ones with a block that can't be edited safely: not closed, or not writable), oc_fish (fish's file)
oc_find() {
    oc_files="" oc_unit="" oc_unit_on="" oc_unit_other="" oc_rc="" oc_rc_manual="" oc_fish=""
    # the app folder, with the old app's own files in it (and what an install that stopped half way left)
    if [ -f "$oc_app/shell.qml" ] && [ -f "$oc_app/services/Nbfc.qml" ]; then
        oc_files="$oc_app$nl"
        for _oc_f in "$oc_app.new" "$oc_app.old"; do
            if [ -d "$_oc_f" ]; then oc_files="$oc_files$_oc_f$nl"; fi
        done
    fi
    # the menu entry and the service: only if they run the old launcher
    for _oc_f in "$data/applications/ccenter.desktop" "$data/systemd/user/ccenter.service" "$config/systemd/user/ccenter.service"; do
        if [ -f "$_oc_f" ] && grep -qF "$oc_launcher" "$_oc_f"; then
            oc_files="$oc_files$_oc_f$nl"
            case "$_oc_f" in *.service) oc_unit=$_oc_f ;; esac
        fi
    done
    # the terminal command: only if it links to the old launcher
    if [ -L "$HOME/.local/bin/ccenter" ] && [ "$(readlink -f "$HOME/.local/bin/ccenter")" = "$oc_app_real/bin/ccenter" ]; then
        oc_files="$oc_files$HOME/.local/bin/ccenter$nl"
    fi
    if command -v systemctl >/dev/null 2>&1 && [ "$(systemctl --user is-enabled ccenter.service 2>/dev/null)" = enabled ]; then
        if [ -n "$oc_unit" ]; then oc_unit_on=1
        else oc_unit_other=$(systemctl --user show -p FragmentPath --value ccenter.service 2>/dev/null); oc_unit_other=${oc_unit_other:-ccenter.service}; fi
    fi
    for _oc_f in "$HOME/.bashrc" "$HOME/.zshrc"; do
        if [ -f "$_oc_f" ] && grep -qxF "$oc_begin" "$_oc_f"; then
            if rc_block_ok "$_oc_f" "$oc_begin" "$oc_end" && rc_writable "$_oc_f"; then oc_rc="$oc_rc$_oc_f$nl"
            else oc_rc_manual="$oc_rc_manual$_oc_f$nl"; fi
        fi
    done
    _oc_f=$config/fish/conf.d/ccenter.fish
    if [ -f "$_oc_f" ] && grep -qxF "$oc_begin" "$_oc_f"; then oc_fish=$_oc_f; fi
    return 0
}
oc_found() { [ -n "$oc_files$oc_rc$oc_rc_manual$oc_fish" ]; }
oc_running() { qs list --all 2>/dev/null | grep -qF "$oc_app_real/shell.qml"; }

# Does file $1 add exactly folder $2 to PATH? (The line the old install.sh wrote, also the one Solwind's writes.)
oc_adds() {
    case "$1" in
        *.fish) grep -qxF -e "fish_add_path -g '$2'" -e "fish_add_path -g $2" "$1" ;;
        *) grep -qxF "case \":\$PATH:\" in *\":$2:\"*) ;; *) export PATH=\"$2:\$PATH\" ;; esac" "$1" ;;
    esac
}
# What happens to the old PATH block in file $1; $2: Solwind's command folder (empty when uninstalling: drop)
#   keep  it adds that same folder: it becomes Solwind's block (bash/zsh: the markers are renamed, the line stays;
#         fish: solwind.fish is written), so the 'solwind' command keeps working in new terminals
#   drop  it adds the old app's own folder, which is deleted (or uninstalling): the block goes
#   leave it adds some other folder (e.g. ~/.local/bin while Solwind's command lives in its app folder), which other
#         programs may need: left as it is
oc_path_action() {
    if [ -z "$2" ]; then echo drop
    elif oc_adds "$1" "$2"; then
        case "$1" in
            *.fish) if [ -e "${1%/*}/solwind.fish" ]; then echo drop; else echo keep; fi ;;
            *) if grep -qxF "# >>> solwind >>>" "$1"; then echo drop; else echo keep; fi ;;
        esac
    elif oc_adds "$1" "$oc_app/bin"; then echo drop
    else echo leave; fi
}

# The plan: what happens to each part ($1: as for oc_path_action)
oc_list() {
    _oc_ifs=$IFS; IFS=$nl; set -f
    for _oc_f in $oc_files; do
        case "$_oc_f" in
            "$oc_app") t "  $_oc_f/   the old app: deleted" "  $_oc_f/   eski uygulama: silinecek" ;;
            *) t "  $_oc_f   deleted" "  $_oc_f   silinecek" ;;
        esac
    done
    for _oc_f in $oc_rc $oc_fish; do
        case "$(oc_path_action "$_oc_f" "$1")" in
            keep) case "$_oc_f" in
                      *.fish) t "  $_oc_f   deleted; ${_oc_f%/*}/solwind.fish adds the same folder instead" \
                                "  $_oc_f   silinecek; yerine ${_oc_f%/*}/solwind.fish aynı klasörü ekler" ;;
                      *) t "  $_oc_f   its PATH block becomes Solwind's (the same line; only the '$oc_begin' markers are renamed)" \
                           "  $_oc_f   PATH bloğu Solwind'in olur (aynı satır; sadece '$oc_begin' işaretlerinin adı değişir)" ;;
                  esac ;;
            drop) case "$_oc_f" in
                      *.fish) t "  $_oc_f   deleted" "  $_oc_f   silinecek" ;;
                      *) t "  $_oc_f   its PATH block (between the '$oc_begin' lines) is removed" \
                           "  $_oc_f   PATH bloğu ('$oc_begin' satırları arası) silinecek" ;;
                  esac ;;
            *) t "  $_oc_f   its PATH block adds a folder other programs may use: left as it is" \
                 "  $_oc_f   PATH bloğu başka programların da kullanabileceği bir klasör ekliyor: olduğu gibi kalacak" ;;
        esac
    done
    for _oc_f in $oc_rc_manual; do
        t "  $_oc_f   its PATH block is left as it is (it isn't closed by '$oc_end', or the file isn't writable)" \
          "  $_oc_f   PATH bloğu olduğu gibi kalacak ('$oc_end' ile kapanmıyor ya da dosya yazılamıyor)"
    done
    IFS=$_oc_ifs; set +f
    if [ -n "$oc_unit" ]; then
        if [ -n "$oc_unit_on" ]; then
            t "  its background service (ccenter.service) is on: it is turned off" \
              "  arka plan servisi (ccenter.service) açık: kapatılacak"
        else
            t "  its background service (ccenter.service) is off" "  arka plan servisi (ccenter.service) kapalı"
        fi
    fi
    return 0
}

# Removes what oc_find found ($1: as for oc_path_action). The app must not be running (the callers check). A part
# that can't be removed doesn't stop the rest: oc_failed lists it (one per line).
oc_remove() {
    oc_failed=""
    if [ -n "$oc_unit" ] && command -v systemctl >/dev/null 2>&1; then
        systemctl --user disable --now ccenter.service >/dev/null 2>&1 || true
    fi
    _oc_ifs=$IFS; IFS=$nl; set -f
    for _oc_f in $oc_files; do rm -rf "$_oc_f" || oc_failed="$oc_failed$_oc_f$nl"; done
    for _oc_f in $oc_rc; do
        case "$(oc_path_action "$_oc_f" "$1")" in
            keep) rc_sed 's/^# >>> ccenter >>>$/# >>> solwind >>>/; s/^# <<< ccenter <<<$/# <<< solwind <<</' "$_oc_f" \
                      || oc_failed="$oc_failed$_oc_f$nl" ;;
            drop) rc_sed '/^# >>> ccenter >>>$/,/^# <<< ccenter <<<$/d' "$_oc_f" || oc_failed="$oc_failed$_oc_f$nl" ;;
        esac
    done
    IFS=$_oc_ifs; set +f
    if [ -n "$oc_fish" ]; then
        case "$(oc_path_action "$oc_fish" "$1")" in
            keep) printf "%s\nfish_add_path -g '%s'\n%s\n" "# >>> solwind >>>" "$1" "# <<< solwind <<<" > "${oc_fish%/*}/solwind.fish" \
                      && rm -f "$oc_fish" || oc_failed="$oc_failed$oc_fish$nl" ;;
            drop) rm -f "$oc_fish" || oc_failed="$oc_failed$oc_fish$nl" ;;
        esac
    fi
    # systemctl disable removes the service's links; if it couldn't, a link to the deleted file would be left
    if [ -n "$oc_unit" ]; then
        for _oc_f in "$config/systemd/user/"*.wants/ccenter.service; do
            if [ -L "$_oc_f" ] && [ "$(readlink "$_oc_f")" = "$oc_unit" ] && [ ! -e "$_oc_f" ]; then rm -f "$_oc_f" || true; fi
        done
    fi
    return 0
}
