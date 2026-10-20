pragma Singleton
import Quickshell
import QtQuick
import qs.utils

// Start in the background on login: the systemd user service ccenter.service installed by install.sh / make install.
// The toggle only works for the copy the service runs: without an installation (development copy), or when the
// service runs another copy, it stays disabled. systemctl only changes something when the user flips the toggle.
Singleton {
    id: root
    readonly property string unit: "ccenter.service"
    property bool installed: false            // the service exists
    property string serviceDir: ""            // the app folder the service's launcher uses ("" = unknown)
    readonly property string ownDir: Quickshell.shellPath("shell.qml").replace(/\/shell\.qml$/, "")
    readonly property bool ours: installed && serviceDir === ownDir
    property bool enabled: false
    property bool busy: false
    property string message: ""

    function refresh() {
        // The launcher in ExecStart tells which app folder it runs (ccenter --version: "ccenter <version> (<folder>)")
        cmd.go(["sh", "-c",
                'systemctl --user show -p LoadState,UnitFileState "$1"; ' +
                'e=$(systemctl --user show -p ExecStart --value "$1"); ' +
                'case "$e" in *"path="*" ;"*) e=${e#*path=}; e=${e%% ;*}; ' +
                // only ask a Ccenter launcher (a ccenter.service of another program is not run)
                'head -n 2 "$e" 2>/dev/null | grep -q "^# Ccenter launcher" || exit 0; ' +
                'printf "Dir=%s\\n" "$(env -u CCENTER_DIR "$e" --version 2>/dev/null | sed -n "s/^ccenter .* (\\(.*\\))$/\\1/p")" ;; esac',
                "sh", unit], (code, out) => {
            root.installed = /LoadState=loaded/.test(out)
            root.enabled = /UnitFileState=enabled/.test(out)
            const m = /^Dir=(.*)$/m.exec(out)
            root.serviceDir = m ? m[1] : ""
        })
    }
    function setEnabled(v) {
        if (!ours) return
        busy = true
        message = ""
        cmd.go(["systemctl", "--user", v ? "enable" : "disable", unit], (code, out, err) => {
            root.busy = false
            if (code !== 0) root.message = I18n.t("Failed: %1").arg((err || out).trim().split("\n")[0])
            root.refresh()
        })
    }

    Cmd { id: cmd; timeout: 5000 }
    Component.onCompleted: refresh()
}
