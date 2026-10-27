pragma Singleton
import Quickshell
import QtQuick
import qs.utils

// Start in the background on login: the systemd user service solwind.service installed by make install runs solwindd
// (and pulls in the tray icon's solwind-tray.service). The toggle only works for the copy the service runs: the
// launcher tells this window app which solwindd it belongs to (SOLWIND_LIB); without an installation (development
// copy), or when the service runs another copy's solwindd, it stays disabled. systemctl only changes something when
// the user flips the toggle, except a daemon-reload (no root, nothing restarted) when make install replaced the unit.
Singleton {
    id: root
    readonly property string unit: "solwind.service"
    property bool installed: false            // the service exists
    property string serviceDir: ""            // the solwindd the service runs ("" = unknown)
    readonly property string ownDaemon: Quickshell.env("SOLWIND_LIB") ? Quickshell.env("SOLWIND_LIB") + "/solwindd" : ""
    readonly property bool ours: installed && ownDaemon !== "" && serviceDir === ownDaemon
    property bool enabled: false
    property bool busy: false
    property string message: ""

    function refresh() {
        // After make install changed the unit file, systemd still holds the old one (NeedDaemonReload): reload first
        cmd.go(["sh", "-c",
                '[ "$(systemctl --user show -p NeedDaemonReload --value "$1")" != yes ] || systemctl --user daemon-reload; ' +
                'systemctl --user show -p LoadState,UnitFileState,ExecStart "$1"',
                "sh", unit], (code, out) => {
            root.installed = /LoadState=loaded/.test(out)
            root.enabled = /UnitFileState=enabled/.test(out)
            const m = /^ExecStart=.*?path=([^ ;]+) ;/m.exec(out)
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
