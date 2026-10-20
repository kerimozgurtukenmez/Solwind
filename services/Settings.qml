pragma Singleton
import Quickshell
import Quickshell.Io
import QtQuick
import qs.utils

// Persistent settings: ~/.config/solwind/settings.json (or $XDG_CONFIG_HOME/solwind).
// The app writes only this file; it is created on the first change. Deleting it resets everything.
// Format: { version, safety, configs: { "<NBFC config name>": { fans: { "0": st, ... }, global: { on, st } } } }
Singleton {
    id: root

    readonly property string dir: (Quickshell.env("XDG_CONFIG_HOME") || Quickshell.env("HOME") + "/.config") + "/solwind"
    readonly property string path: dir + "/settings.json"
    property var data: ({ version: 1, configs: {} })
    property bool dirReady: false

    // ---------- validation: everything read from the file is clamped; invalid entries become null ----------
    function num(v, lo, hi) {
        const n = Number(v)
        return isFinite(n) ? Math.max(lo, Math.min(hi, n)) : NaN
    }
    function cleanSt(st) {
        if (!st || typeof st !== "object") return null
        const mode = Math.round(num(st.mode, 0, 2))
        if (!isFinite(mode)) return null
        const fixed = num(st.fixed, 0, 1)
        // Array.from also accepts Qt's list type (Repeater modelData etc.); Array.isArray rejects it
        let curve = st.curve && typeof st.curve.length === "number"
            ? Array.from(st.curve).map(p => ({ t: Math.round(num(p && p.t, 0, 110)), s: Math.round(num(p && p.s, 0, 100)) }))
                      .filter(p => isFinite(p.t) && isFinite(p.s))
                      .sort((a, b) => a.t - b.t)
            : []
        if (mode === 1 && !isFinite(fixed)) return null
        if (mode === 2 && curve.length < 2) return null
        const src = ["cpu", "gpu", "max"].indexOf(st.src) >= 0 ? st.src : "cpu"
        const hyst = Math.round(num(st.hyst, 0, 10))
        return { mode: mode, fixed: isFinite(fixed) ? fixed : 0.5, curve: curve.length >= 2 ? curve : null, smooth: st.smooth !== false, src: src, hyst: isFinite(hyst) ? hyst : 3 }
    }

    function cfg(id) {
        const c = data.configs ? data.configs[id || "default"] : null
        return c && typeof c === "object" ? c : null
    }
    function fanOf(id, i) {
        const c = cfg(id)
        return c && c.fans ? cleanSt(c.fans[String(i)]) : null
    }
    function globalOf(id) {
        const c = cfg(id)
        if (!c || !c.global) return null
        const st = cleanSt(c.global.st)
        return st ? { on: c.global.on === true, st: st } : null
    }
    // ---------- profiles (per config): configs[id].profiles = [{ name, fans: { "0": st }, global: { on, st } }] ----------
    function cleanName(n) { return typeof n === "string" ? n.trim().slice(0, 32) : "" }
    function cleanProfile(p) {
        if (!p || typeof p !== "object") return null
        const name = cleanName(p.name)
        if (name === "") return null
        const fans = {}
        if (p.fans && typeof p.fans === "object")
            for (const k in p.fans) { const s = cleanSt(p.fans[k]); if (s && /^\d+$/.test(k)) fans[k] = s }
        const gs = p.global ? cleanSt(p.global.st) : null
        return { name: name, fans: fans, global: gs ? { on: p.global.on === true, st: gs } : null }
    }
    function profilesOf(id) {
        const c = cfg(id)
        return c && c.profiles && typeof c.profiles.length === "number" ? Array.from(c.profiles).map(cleanProfile).filter(p => p !== null) : []
    }
    function activeOf(id) {
        const c = cfg(id)
        return c && typeof c.active === "string" ? c.active : ""
    }
    function saveProfile(id, p) {
        edit(d => {
            const c = slot(d, id)
            // Names are matched case-insensitively (FanState.applyByName): "gece" replaces "Gece"
            const key = String(p.name).toLocaleLowerCase()
            const same = x => x && typeof x.name === "string" && x.name.toLocaleLowerCase() === key
            if (typeof c.active === "string" && c.active.toLocaleLowerCase() === key) c.active = p.name
            const list = Array.isArray(c.profiles) ? c.profiles.filter(x => x && !same(x)) : []
            list.push(p)
            c.profiles = list
        })
    }
    function removeProfile(id, name) {
        edit(d => {
            const c = slot(d, id)
            if (Array.isArray(c.profiles)) c.profiles = c.profiles.filter(x => x && x.name !== name)
            if (c.active === name) c.active = ""
        })
    }
    function setActive(id, name) {
        if (activeOf(id) === name) return
        edit(d => { slot(d, id).active = name })
    }

    // Notification settings (default: on)
    function notifyOn(key) { return !(data.notify && data.notify[key] === false) }
    function setNotify(key, v) { edit(d => { if (!d.notify || typeof d.notify !== "object") d.notify = {}; d.notify[key] = v === true }) }

    // UI options (default: on), e.g. "tray"
    function uiOn(key) { return !(data.ui && data.ui[key] === false) }
    // UI language: "tr" | "en" | "" (follow the system language)
    function uiLang() {
        const l = data.ui && typeof data.ui.lang === "string" ? data.ui.lang : ""
        return ["tr", "en"].indexOf(l) >= 0 ? l : ""
    }
    function setUiLang(l) { edit(d => { if (!d.ui || typeof d.ui !== "object") d.ui = {}; d.ui.lang = l }) }
    function setUi(key, v) { edit(d => { if (!d.ui || typeof d.ui !== "object") d.ui = {}; d.ui[key] = v === true }) }

    // Keyboard effect: { effect, source: "single"|"multi"|"theme", colors: ["#rrggbb"...], speed: 0..1,
    //                   min: 0..1, max: 0..1 (lowest/highest breathing level), themeCount: 3..6, color: "#rrggbb" }
    //   effect: "static" | "breathing" | "cycle" (colour cycle)
    function kbdOf() {
        const k = data.kbd && typeof data.kbd === "object" ? data.kbd : {}
        const hex = x => typeof x === "string" && /^#[0-9a-fA-F]{6}$/.test(x)
        const colors = k.colors && typeof k.colors.length === "number" ? Array.from(k.colors).filter(hex).slice(0, 6) : []
        const sp = num(k.speed, 0, 1)
        let lo = num(k.min, 0, 1), hi = num(k.max, 0, 1)
        if (!isFinite(lo)) lo = 0
        if (!isFinite(hi)) hi = 1
        if (lo > hi) lo = hi
        return {
            effect: ["static", "breathing", "cycle"].indexOf(k.effect) >= 0 ? k.effect : "static",
            themeCount: isFinite(num(k.themeCount, 3, 6)) ? Math.round(num(k.themeCount, 3, 6)) : 3,
            source: ["single", "multi", "theme"].indexOf(k.source) >= 0 ? k.source : "single",
            colors: colors.length ? colors : ["#ff0000", "#00ff00", "#0040ff"],
            speed: isFinite(sp) ? sp : 0.5,
            min: lo,
            max: hi,
            color: hex(k.color) ? k.color : ""
        }
    }
    function setKbd(patch) {
        edit(d => { d.kbd = Object.assign({}, d.kbd && typeof d.kbd === "object" ? d.kbd : {}, patch) })
    }

    function safety() {
        const s = num(data.safety, 70, 100)
        return isFinite(s) ? s : NaN
    }

    // ---------- writing ----------
    function edit(fn) {
        const d = JSON.parse(JSON.stringify(data))
        if (!d.configs || typeof d.configs !== "object") d.configs = {}
        fn(d)
        d.version = 1
        data = d
        saveTimer.restart()
    }
    function slot(d, id) {
        const k = id || "default"
        if (!d.configs[k] || typeof d.configs[k] !== "object") d.configs[k] = {}
        return d.configs[k]
    }
    function setFan(id, i, st) {
        edit(d => { const c = slot(d, id); if (!c.fans) c.fans = {}; c.fans[String(i)] = st })
    }
    function setGlobal(id, on, st) {
        edit(d => { slot(d, id).global = { on: on, st: st } })
    }
    function setSafety(v) {
        edit(d => { d.safety = v })
    }

    function write() {
        if (!dirReady) {
            mkdir.go(["mkdir", "-p", dir], code => {
                if (code !== 0) { console.warn("[solwind] could not create the settings folder: " + root.dir); return }
                root.dirReady = true
                root.write()
            })
            return
        }
        file.setText(JSON.stringify(data, null, 2) + "\n")
    }

    // Before quitting: write the pending change now (otherwise a change from the last 500 ms is lost)
    function flush() {
        if (!saveTimer.running) return
        saveTimer.stop()
        write()
    }

    // Batch rapid changes into one write
    Timer { id: saveTimer; interval: 500; onTriggered: root.write() }
    Cmd { id: mkdir }

    FileView {
        id: file
        path: root.path
        blockLoading: true          // settings must be ready before the fan cards are created
        atomicWrites: true          // write to a temp file first, then move it into place: never a half-written file
        blockWrites: true           // don't return before the write is done, so flush() on quit doesn't lose the last change
        printErrors: false          // no error if the file doesn't exist yet
        onLoaded: {
            try {
                const d = JSON.parse(text())
                if (d && typeof d === "object") {
                    root.data = d
                    root.dirReady = true
                }
            } catch (e) {
                console.warn("[solwind] could not read settings.json, using defaults: " + e)
            }
        }
    }
}
