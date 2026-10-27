pragma Singleton
import Quickshell
import QtQuick
import qs.utils

// The Plugins page's side: Solwind's plugin catalog and installing plugins, through solwind-plugin (next to solwindd,
// SOLWIND_LIB), as the user - the same as `solwind plugin ...`. solwindd never downloads or builds anything; it loads
// plugins when it starts, so after a change it has to be started again (restart()).
Singleton {
    id: root
    readonly property string tool: Quickshell.env("SOLWIND_LIB") ? Quickshell.env("SOLWIND_LIB") + "/solwind-plugin" : ""
    readonly property string catalog: Quickshell.shellPath("plugin-catalog.json")
    property var plugins: []                 // [{ id, kind, description, tested, state, fits }] (solwind-plugin list --json)
    property var others: []                  // installed, not in the catalog
    property bool loaded: false
    property string loadError: ""
    property string busyId: ""               // the plugin an action runs for ("*" = update all)
    readonly property bool busy: busyId !== ""
    property string message: ""              // the last action's result (its last lines)
    property bool messageError: false
    property bool changed: false             // something was installed or removed: solwindd has to start again

    function run(args, done) {
        cmd.go(["env", "SOLWIND_LANG=" + I18n.lang, root.tool, "--catalog", root.catalog].concat(args), done)
    }
    function refresh() {
        if (!tool) { loadError = I18n.t("solwind-plugin isn't there (start Solwind with the solwind command)"); return }
        lister.go(["env", "SOLWIND_LANG=" + I18n.lang, root.tool, "--catalog", root.catalog, "list", "--json"], (code, out, err) => {
            root.loaded = true
            try {
                const d = JSON.parse(out)
                root.plugins = d.plugins || []
                root.others = d.others || []
                root.loadError = code === 0 ? "" : (err || out).trim()
            } catch (e) {
                root.loadError = (err || out).trim() || I18n.t("The plugin list couldn't be read")
            }
        })
    }
    function act(id, args) {
        if (busy) return
        busyId = id
        message = ""
        run(args, (code, out, err) => {
            const lines = (out + err).trim().split("\n").filter(l => l.trim() !== "")
            root.messageError = code !== 0
            root.message = lines.slice(-12).join("\n")
            if (code === 0) root.changed = true
            root.busyId = ""
            root.refresh()
        })
    }
    function install(id) { act(id, ["install", id]) }
    function update() { act("*", ["update"]) }
    function remove(id) { act(id, ["remove", id]) }
    // solwindd loads plugins when it starts: the service again, and this window back once it is up (both go: the
    // window app is part of the service). Run outside the service, so it isn't stopped with it.
    readonly property bool canRestart: Autostart.ours
    function restart() {
        if (!canRestart) return
        restarter.go(["systemd-run", "--user", "--quiet", "--collect", "sh", "-c",
                      'systemctl --user restart solwind.service && sleep 2 && exec "$0" open',
                      Quickshell.env("SOLWIND_SELF") || "solwind"], () => {})
    }

    Cmd { id: cmd; timeout: 1800000 }
    Cmd { id: lister; timeout: 30000 }
    Cmd { id: restarter; timeout: 10000 }
}
