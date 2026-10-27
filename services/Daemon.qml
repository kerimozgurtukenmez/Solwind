pragma Singleton
import Quickshell
import Quickshell.Io
import QtQuick
import Solwind

// The way to solwindd (ARCH-PLAN.md phase 5). The daemon reads the sensors and NBFC, drives the fans and the keyboard
// light, sends the notifications and owns settings.json; the services next to this one only mirror its D-Bus objects
// for the pages and send it what the user changes. Without the daemon nothing is driven, and the window says so.
Singleton {
    id: root

    readonly property bool connected: Solwind.connected === true
    // Started with --observe (development): it only reads and refuses every change
    readonly property bool observe: connected && Solwind.daemon.Mode === "observe"
    // This window app and solwindd come from one install; another version means one was updated while the other ran
    // (the UI may then ask for what the daemon doesn't have): `solwind quit`, then start it again
    FileView { id: versionFile; path: Quickshell.shellPath("VERSION"); blockLoading: true; printErrors: false }
    readonly property string uiVersion: versionFile.text().trim()
    readonly property bool versionMismatch: connected && uiVersion !== "" && typeof Solwind.daemon.Version === "string"
                                            && Solwind.daemon.Version !== uiVersion
    onVersionMismatchChanged: if (versionMismatch)
        console.warn("[solwind] solwindd " + Solwind.daemon.Version + ", this window app " + uiVersion + ": restart Solwind (solwind quit; solwind)")
    readonly property string downText: I18n.t("Solwind's background service (solwindd) is not running")

    // A call: { ok, value (the first answer), values, error, errorName }. Without the daemon it isn't even tried.
    function call(obj, method, sig, args) {
        if (Solwind.connected !== true) return { ok: false, value: undefined, values: [], error: downText, errorName: "solwind.Error.Failed" }
        return Solwind.call(obj, method, sig || "", args || [])
    }
    // A change whose answer nobody waits for: a refusal is only logged (the daemon's state shows what happened)
    function send(obj, method, sig, args) {
        const r = call(obj, method, sig, args)
        if (!r.ok) console.warn("[solwind] " + obj + "." + method + ": " + r.error)
        return r
    }
    // A text the daemon makes (status, profile list ...), "" if it can't be had
    function text(obj, method) {
        const r = call(obj, method)
        return r.ok ? String(r.value) : ""
    }
}
