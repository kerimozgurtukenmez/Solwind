pragma Singleton
import Quickshell
import QtQuick

// Single source of truth for fan settings (independent of the UI, always running).
// The UI (FanCard/ProfileCard) only displays this and reports changes here. When the window is closed and the
// UI is destroyed, fan control, profiles, terminal commands (IPC) and the tray keep working from here.
// st = { mode: 0 auto | 1 fixed | 2 curve, fixed: 0..1, curve: [{t,s}], smooth, src: cpu|gpu|max, hyst }
Singleton {
    id: root

    readonly property var defaultCurve: [{ t: 45, s: 20 }, { t: 55, s: 40 }, { t: 65, s: 60 }, { t: 75, s: 80 }, { t: 85, s: 100 }]
    readonly property var autoSt: ({ mode: 0, fixed: 0.5, curve: defaultCurve, smooth: true, src: "cpu", hyst: 3 })
    // Fills missing fields with defaults (for st objects coming from the settings file or a profile)
    function full(s) {
        const o = Object.assign({}, autoSt, s || {})
        if (!o.curve || o.curve.length < 2) o.curve = defaultCurve
        return o
    }

    property var fanSt: []                   // st per fan, in fan order
    property var globalSt: autoSt
    property bool globalOn: false
    property bool restored: false
    property bool applying: false            // while loading from settings/a profile, don't count changes as manual edits

    // ---------- startup: restore saved settings ----------
    // Fans without saved settings get no commands; they just show their current NBFC state.
    function restore() {
        if (Nbfc.fans.length === 0) return
        if (restored && fanSt.length === Nbfc.fans.length) return
        restored = true
        applying = true
        const id = Nbfc.configId
        const sts = [], saved = []
        for (let i = 0; i < Nbfc.fans.length; i++) {
            const s = Settings.fanOf(id, i)
            if (s) { sts.push(full(s)); saved.push(i) }
            else {
                const f = Nbfc.fans[i]
                sts.push(full(f.auto ? { mode: 0 } : { mode: 1, fixed: Math.max(0, Math.min(1, f.target / 100)) }))
            }
        }
        fanSt = sts
        const g = Settings.globalOf(id)
        if (g) globalSt = full(g.st)
        globalOn = !!(g && g.on)
        if (globalOn) Nbfc.applyTarget(-1, globalSt)
        else for (const i of saved) Nbfc.applyTarget(i, fanSt[i])
        applying = false
    }
    Connections {
        target: Nbfc
        function onFansChanged() { Qt.callLater(root.restore) }
        // Another config has its own saved settings: loaded on the next status read (Nbfc.forgetFans has already
        // dropped the old config's ones). Also covers a status read that came before /etc/nbfc/nbfc.json was read.
        function onConfigIdChanged() { root.restored = false }
    }

    // ---------- changes coming from the UI ----------
    function setFan(i, st) {
        const a = fanSt.slice()
        a[i] = full(st)
        fanSt = a
        if (!globalOn) Nbfc.applyTarget(i, a[i])
        Settings.setFan(Nbfc.configId, i, a[i])
        manualEdit()
    }
    function setGlobal(st) {
        globalSt = full(st)
        if (globalOn) Nbfc.applyTarget(-1, globalSt)
        Settings.setGlobal(Nbfc.configId, globalOn, globalSt)
        manualEdit()
    }
    // Global on: the global setting is written to every fan; global off: each fan goes back to its own setting
    function setGlobalOn(on) {
        if (globalOn === on) return
        globalOn = on
        reapply()
        Settings.setGlobal(Nbfc.configId, globalOn, globalSt)
        manualEdit()
    }
    function reapply() {
        if (globalOn) Nbfc.applyTarget(-1, globalSt)
        else for (let i = 0; i < fanSt.length; i++) Nbfc.applyTarget(i, fanSt[i])
    }
    function manualEdit() { if (!applying) Settings.setActive(Nbfc.configId, "") }

    // ---------- profiles ----------
    function curveSt(pts, hyst) { return { mode: 2, fixed: 0.5, curve: pts, smooth: true, src: "max", hyst: hyst } }
    // Built-in profiles: the same curve on all fans via Global, following the hotter of CPU and GPU.
    // `name` is a language-independent id (saved in settings as the active profile); `label` is translated for display.
    readonly property var builtins: [
        { name: "auto", label: "NBFC Auto", builtin: true, icon: "auto_mode", fans: {}, global: null },
        { name: "quiet", label: "Quiet", builtin: true, icon: "volume_off",
          global: { on: true, st: curveSt([{ t: 45, s: 0 }, { t: 55, s: 25 }, { t: 65, s: 40 }, { t: 75, s: 60 }, { t: 85, s: 100 }], 4) } },
        { name: "balanced", label: "Balanced", builtin: true, icon: "balance",
          global: { on: true, st: curveSt([{ t: 45, s: 20 }, { t: 55, s: 35 }, { t: 65, s: 55 }, { t: 75, s: 75 }, { t: 85, s: 100 }], 3) } },
        { name: "performance", label: "Performance", builtin: true, icon: "bolt",
          global: { on: true, st: curveSt([{ t: 40, s: 40 }, { t: 50, s: 55 }, { t: 60, s: 70 }, { t: 70, s: 85 }, { t: 80, s: 100 }], 2) } }
    ]
    function allProfiles() { return builtins.concat(Settings.profilesOf(Nbfc.configId)) }
    // Name shown in the UI: built-ins are translated, user profiles keep their own name
    function displayName(p) { return p.builtin ? I18n.t(p.label) : p.name }
    // Older versions saved the built-in profiles by their Turkish names
    readonly property var legacyIds: ({ "NBFC Otomatik": "auto", "Sessiz": "quiet", "Dengeli": "balanced", "Performans": "performance" })
    function activeId() { const a = Settings.activeOf(Nbfc.configId); return legacyIds[a] ?? a }
    function activeName() { const p = allProfiles().find(x => x.name === activeId()); return p ? displayName(p) : "" }
    // Every name a profile answers to: id, English label and its translations (case-insensitive)
    function namesOf(p) {
        if (!p.builtin) return [p.name.toLocaleLowerCase()]
        return [p.name, p.label].concat(I18n.languages.map(l => I18n.tIn(l.id, p.label))).map(x => x.toLocaleLowerCase())
    }
    // A user profile may not take a name a built-in profile answers to
    function reservedName(n) { const k = String(n).trim().toLocaleLowerCase(); return builtins.some(p => namesOf(p).indexOf(k) >= 0) }

    function snapshot(name) {
        const fans = {}
        for (let i = 0; i < fanSt.length; i++) fans[String(i)] = fanSt[i]
        return { name: name, fans: fans, global: { on: globalOn, st: globalSt } }
    }
    function applyProfile(p) {
        if (!restored) return
        applying = true
        const id = Nbfc.configId
        const a = []
        for (let i = 0; i < Nbfc.fans.length; i++) {
            const s = p.fans ? Settings.cleanSt(p.fans[String(i)]) : null
            a.push(full(s ?? { mode: 0 }))
            Settings.setFan(id, i, a[i])
        }
        fanSt = a
        const g = p.global ? Settings.cleanSt(p.global.st) : null
        if (g) globalSt = full(g)
        globalOn = !!(p.global && p.global.on && g)
        reapply()
        Settings.setGlobal(id, globalOn, globalSt)
        Settings.setActive(id, p.name)
        applying = false
    }
    // UI, terminal and tray apply profiles by name (taken from the real list, so Qt list types can't get in the way)
    function applyByName(name, fromIpc) {
        const key = String(name).trim().toLocaleLowerCase()
        const p = allProfiles().find(x => namesOf(x).indexOf(key) >= 0)
        if (!p) return I18n.t("Profile not found: %1").arg(name) + "\n" + profileList()
        if (!restored) return I18n.t("Fans not read yet (is NBFC running?), try again")
        if (!Nbfc.running) return I18n.t("The NBFC service is not running")
        if (Nbfc.readOnly) return I18n.t("NBFC is in read-only mode, fan speeds can't be written")
        applyProfile(p)
        if (fromIpc && Notify.profiles) Notify.send(I18n.t("Fan profile: %1").arg(displayName(p)), "", "low", "dialog-information")
        return I18n.t("Applied: %1").arg(displayName(p))
    }
    function profileList() {
        const active = activeId()
        return allProfiles().map(p => (p.name === active ? "* " : "  ") + displayName(p) + (p.builtin ? "  (" + p.name + ")" : "")).join("\n")
    }
    function statusText() {
        const modes = [I18n.t("Auto"), I18n.t("Fixed"), I18n.t("Curve")]
        const lines = [
            I18n.t("Service: %1").arg(Nbfc.running ? I18n.t("running") : I18n.t("stopped")) + (Nbfc.readOnly ? " (" + I18n.t("read-only") + ")" : ""),
            "Config: " + (Nbfc.configId || "-"),
            I18n.t("Profile: %1").arg(activeName() || I18n.t("custom settings")),
            "CPU: " + (isFinite(Nbfc.temp) ? Nbfc.temp.toFixed(1) + "°C" : "-") + "   GPU: " + (isFinite(Sensors.gpu) ? Sensors.gpu.toFixed(0) + "°C" : "-"),
            "Global: " + (globalOn ? I18n.t("on (%1)").arg(modes[globalSt.mode]) : I18n.t("off"))
                + (Nbfc.boosting ? "   " + I18n.t("Max fan: on") : "")
        ]
        for (let i = 0; i < Nbfc.fans.length; i++) {
            const f = Nbfc.fans[i]
            const m = globalOn ? globalSt.mode : (fanSt[i] ? fanSt[i].mode : 0)
            lines.push(I18n.t("Fan %1 (%2): %3 · NBFC %4 · current %5% · target %6%").arg(i + 1).arg(f.name).arg(modes[m])
                       .arg(f.auto ? I18n.t("auto") : I18n.t("manual")).arg(Math.round(f.current)).arg(Math.round(f.target)))
        }
        return lines.join("\n")
    }
}
