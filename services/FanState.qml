pragma Singleton
import Quickshell
import QtQuick
import Solwind

// The fan settings and profiles, as the pages know them. solwindd keeps them (restores them per NBFC config, applies
// profiles, saves them); this mirrors its /solwind/Fans and sends the cards' changes.
// st = { mode: 0 auto | 1 fixed | 2 curve, fixed: 0..1, curve: [{t,s}], smooth, src: cpu|gpu|max, hyst }
Singleton {
    id: root

    readonly property var f: Solwind.fans
    readonly property bool up: Solwind.connected === true

    // A card's starting point before the daemon has answered (the daemon's own defaults are the same)
    readonly property var defaultCurve: [{ t: 45, s: 20 }, { t: 55, s: 40 }, { t: 65, s: 60 }, { t: 75, s: 80 }, { t: 85, s: 100 }]
    readonly property var autoSt: ({ mode: 0, fixed: 0.5, curve: defaultCurve, smooth: true, src: "cpu", hyst: 3 })

    readonly property var fanSt: up && f.FanSettings ? Array.from(f.FanSettings) : []    // st per fan, in fan order
    readonly property var globalSt: up && f.Global && f.Global.mode !== undefined ? f.Global : autoSt
    readonly property bool globalOn: up && f.GlobalOn === true
    readonly property bool restored: up && f.Restored === true

    // ---------- changes coming from the UI ----------
    function setFan(i, st) { Daemon.send("fans", "SetFan", "ia{sv}", [i, st]) }
    function setGlobal(st) { Daemon.send("fans", "SetGlobal", "a{sv}", [st]) }
    function setGlobalOn(on) { Daemon.send("fans", "SetGlobalOn", "b", [on === true]) }

    // ---------- profiles ----------
    // [{ name (the id for built-ins), label (translated by the daemon), builtin, icon }], built-ins first
    readonly property var profiles: up && f.Profiles ? Array.from(f.Profiles) : []
    readonly property var builtins: profiles.filter(p => p.builtin)
    function allProfiles() { return profiles }
    // Name shown in the UI: built-ins in the UI language, user profiles keep their own name
    function displayName(p) { return p.builtin ? (p.label ?? p.name) : p.name }
    function activeId() { return up ? (f.ActiveProfile ?? "") : "" }
    function activeName() { const a = activeId(); const p = profiles.find(x => x.name === a); return p ? displayName(p) : "" }
    // A user profile may not take a name a built-in profile answers to (in any language, any case)
    function reservedName(n) { return up && Array.from(f.ReservedNames ?? []).indexOf(String(n).trim().toLocaleLowerCase()) >= 0 }
    // What ProfileCard saves: the daemon takes the current settings itself (Settings.saveProfile)
    function snapshot(name) { return { name: name } }

    // UI, terminal and tray apply profiles by name. fromIpc: a terminal / keybind asked (notified if notify.profile)
    function applyByName(name, fromIpc) { return applyByNameResult(name, fromIpc).text }
    // { ok, usage (the name matched no profile), text }
    function applyByNameResult(name, fromIpc) {
        const r = Daemon.call("fans", "ApplyProfile", "sb", [String(name), fromIpc === true])
        return r.ok ? { ok: true, usage: false, text: String(r.value) }
                    : { ok: false, usage: r.errorName === "solwind.Error.Usage", text: r.error }
    }
    function profileList() { return Daemon.text("fans", "ProfileList") }
    function statusText() { return Daemon.text("fans", "StatusText") }
}
