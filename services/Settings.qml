pragma Singleton
import Quickshell
import QtQuick
import Solwind

// The settings the pages show and change. solwindd owns ~/.config/solwind/settings.json (it reads, validates and
// writes it; two programs writing one file would lose changes), so this reads them from the daemon and sends changes
// to it.
Singleton {
    id: root

    readonly property var d: Solwind.daemon
    readonly property bool up: Solwind.connected === true

    // Notification settings (default: on): "enabled" (all of them), "profile" (a profile changed from a terminal)
    function notifyOn(key) {
        if (!up) return true
        return key === "enabled" ? d.NotifyEnabled !== false : key === "profile" ? d.NotifyProfile !== false : true
    }
    function setNotify(key, v) { Daemon.send("daemon", "SetNotify", "sb", [key, v === true]) }

    // UI options (default: on): "tray". Off while there is no daemon (the tray's actions all need it)
    function uiOn(key) { return up && !(key === "tray" && d.TrayEnabled === false) }
    function setUi(key, v) { Daemon.send("daemon", "SetUi", "sb", [key, v === true]) }

    // UI language: "tr" | "en" | "" (follow the system language)
    function uiLang() { const l = up ? (d.SavedLanguage ?? "") : ""; return ["tr", "en"].indexOf(l) >= 0 ? l : "" }
    function setUiLang(l) { Daemon.send("daemon", "SetLanguage", "s", [l]) }

    // The safety limit (°C), NaN if unknown; ServiceCard sets it while its slider moves
    function safety() { return up && typeof Solwind.fans.Safety === "number" ? Solwind.fans.Safety : NaN }
    function setSafety(v) {
        Nbfc.holdSafety()
        if (!Daemon.send("fans", "SetSafety", "i", [Math.round(v)]).ok) Nbfc.safetyHeld = false   // (refused: the daemon's again)
    }

    // ---------- profiles (of the NBFC config the fans belong to; `id` is always that one) ----------
    function profilesOf(id) { return FanState.profiles.filter(p => !p.builtin).map(p => ({ name: p.name })) }
    function activeOf(id) { return FanState.activeId() }
    function saveProfile(id, p) { Daemon.send("fans", "SaveProfile", "s", [String(p.name)]) }   // (the daemon takes the current settings)
    function removeProfile(id, name) { Daemon.send("fans", "RemoveProfile", "s", [String(name)]) }
    // A saved profile becomes the active one in the daemon already (SaveProfile)
    function setActive(id, name) {}
    // Nothing waits here: the daemon writes the file (and before it quits)
    function flush() {}
}
