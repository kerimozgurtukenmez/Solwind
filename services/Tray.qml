pragma Singleton
import Quickshell
import Quickshell.Io
import QtQuick

// Tray icon: runs scripts/tray.py as a child process (shows up in Caelestia's bar).
// State goes to its stdin as JSON lines; tray.py writes clicks to stdout as commands.
Singleton {
    id: root
    readonly property bool enabled: Settings.uiOn("tray")
    // Tray icon: the app icon (theme icon "solwind" if it can't be loaded)
    readonly property string iconFile: Quickshell.shellPath("assets/solwind.svg")

    // bound in shell.qml
    property bool windowVisible: true
    property var profiles: []
    property string active: ""
    property bool boost: false
    property string tooltip: "Solwind"
    property var labels: ({})                 // menu texts in the UI language (bound in shell.qml)

    signal toggleRequested()
    signal profileRequested(string name)

    readonly property string stateLine: JSON.stringify({ profiles: profiles, active: active, boost: boost,
                                                         visible: windowVisible, tooltip: tooltip, labels: labels })
    onStateLineChanged: push()
    function push() { if (proc.running) proc.write(stateLine + "\n") }

    Process {
        id: proc
        running: root.enabled
        command: ["python3", Quickshell.shellPath("scripts/tray.py"), root.iconFile]
        stdinEnabled: true
        onStarted: root.push()
        stdout: SplitParser {
            onRead: line => {
                const sp = line.indexOf(" ")
                const cmd = sp < 0 ? line : line.slice(0, sp)
                const arg = sp < 0 ? "" : line.slice(sp + 1)
                if (cmd === "toggle") root.toggleRequested()
                else if (cmd === "profile") root.profileRequested(arg)
                else if (cmd === "boost" && !Nbfc.setBoost(parseInt(arg) || 0))
                    Notify.send(I18n.t("Max fan not turned on"), Nbfc.boostBlocked(), "normal", "dialog-information")
                else if (cmd === "quit") Nbfc.releaseAndQuit()
            }
        }
        stderr: SplitParser { onRead: line => console.warn("[solwind] tepsi: " + line) }
    }
}
