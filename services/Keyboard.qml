pragma Singleton
import Quickshell
import Quickshell.Io
import QtQuick
import qs.utils

// Keyboard backlight. Detection (scripts/kbdinfo.sh) finds the laptop maker, the kernel driver and the light it
// exposes, and picks a backend. SAFETY: only files a kernel driver exposes in /sys are used (LED class files under
// /sys/class/leds, or the hp-wmi module's rgb_zones); no raw WMI/ACPI/EC/USB access. The driver validates every value.
// Backends:
//   led-rgb   one RGB light: brightness + multi_intensity "r g b"            (HP Victus/Omen single zone: hp::kbd_backlight)
//   hp-zoned  four-zone keyboards with one LED per zone (the kernel's naming: *::kbd_zoned_backlight-<zone>;
//             TUXOV's hp-wmi-fan-and-backlight-control 0.0.4+ uses right/middle/left/wasd, HP's proposed kernel patch
//             right/center/left/wasd; that module's 0.0.3 numbers them: hp::kbd_backlight_zone0..3);
//             every zone gets the same colour and brightness
//   hp-rgbzones HP four-zone keyboards with older copies of that module (0.0.2, e.g. the one victus-control installs):
//             /sys/devices/platform/hp-wmi/rgb_zones/zone00..03, hex "RRGGBB". No brightness file, so colour only;
//             every zone gets the same colour.
//   asus-tuf  ASUS TUF: asus::kbd_backlight brightness (0..3) + kbd_rgb_mode "cmd mode r g b speed" for the colour.
//             Always cmd 0 (apply only, never 1 = save to BIOS) and mode 0 (static). kbd_rgb_mode can't be read back.
//   led-mono  brightness only (ASUS non-RGB, Lenovo IdeaPad/ThinkPad, others)
// Software effects run only on drivers they were tested with (effectsAllowed). Write access comes from the optional
// udev rule (dist/90-ccenter.rules), which gives write access to the user who granted it only.
// Effects are done in software (this service always runs, so they continue while the window is closed):
//   breathing: intensity eases between the lowest and highest level; the colour changes at the darkest point.
//   cycle:     continuous smooth transition between colours along the hue, without dimming.
Singleton {
    id: root

    property string backend: ""               // "" (none) | led-rgb | hp-zoned | hp-rgbzones | asus-tuf | led-mono
    property var zones: []                    // light folders written together (one, or the HP zones); hp-rgbzones: the zone files
    readonly property string dir: zones.length ? zones[0] : ""
    readonly property bool available: backend !== ""
    property string name: ""                  // e.g. hp::kbd_backlight (or "hp::kbd_zoned_backlight ×4")
    property string driver: ""                // kernel driver of the light, e.g. hp-wmi, asus-nb-wmi, ideapad_acpi
    property var dmi: ({ vendor: "", product: "", family: "", board: "" })
    property string kernel: ""
    property var notes: []                    // extra findings: legion-usb, rog-usb, hp-none
    readonly property bool rgb: backend === "led-rgb" || backend === "hp-zoned" || backend === "hp-rgbzones" || backend === "asus-tuf"
    readonly property bool hasBrightness: available && backend !== "hp-rgbzones"
    // Software effects write many times per second: only where they were tested (HP single zone). On four-zone HP
    // keyboards every zone write is a full colour-table read + write over WMI in both module versions, 4x per frame.
    readonly property bool effectsAllowed: rgb && driver === "hp-wmi" && backend === "led-rgb"
    readonly property bool tested: backend === "led-rgb" && driver === "hp-wmi"
    property bool writable: false
    property bool ruleLegacy: false           // an older, less strict rule is installed (0666 or running a shell)
    property bool ready: false                // first read finished?
    property int maxBrightness: 255
    property var maxIntensity: [255, 255, 255]
    property int brightness: 0
    property color color: "white"             // chosen colour (at full intensity; brightness is separate)
    property string error: ""
    // Where the udev rule granting write access lives: "etc" (added by install.sh / "grant" button, removable from the app),
    // "lib" (added by a system package), "" (none)
    property string ruleAt: ""
    property bool permBusy: false

    // ---------- effect settings (settings.json -> kbd) ----------
    property string effect: "static"          // static | breathing | cycle
    property string source: "single"          // single (chosen colour) | multi (colour list) | theme (Caelestia)
    property var colors: ["#ff0000", "#00ff00", "#0040ff"]
    property real speed: 0.5                  // 0 slow .. 1 fast
    readonly property real period: 8 - 7 * speed   // one breath / one colour (s): 8 .. 1
    property bool paused: false               // effect pauses while the permission is being revoked (the saved choice is kept)
    readonly property bool effectActive: effect !== "static" && effectsAllowed && writable && !paused
    property int cycleIndex: 0                // index of the colour currently shown (the UI highlights it)
    property int themeCount: 3                // number of Caelestia colours (3..6)
    property real minLevel: 0                 // lowest breathing level (0..1); 0 = fully off
    property real maxLevel: 1                 // highest breathing level (0..1)

    // Caelestia colours are made vivid for the LED (theme colours are pastel; low saturation looks whitish on an LED)
    function vivid(c) {
        c = Qt.lighter(c, 1)
        return c.hsvSaturation < 0.05 ? Qt.rgba(1, 1, 1, 1) : Qt.hsva(c.hsvHue, Math.max(c.hsvSaturation, 0.85), 1, 1)
    }
    // Theme colours: the theme's primary colour first, then from secondary/tertiary and the theme's accent colours
    // the hues furthest from the ones already picked (themeCount in total). All from the theme; look-alikes are skipped.
    readonly property var themeColors: {
        const base = [vivid(Colours.primary)]
        const pool = [Colours.secondary, Colours.tertiary].concat(Colours.accents).map(vivid)
        const hue = c => c.hsvHue < 0 ? -1 : c.hsvHue
        const dist = (a, b) => { const x = Math.abs(hue(a) - hue(b)); return Math.min(x, 1 - x) }
        const out = base
        while (out.length < themeCount && pool.length > 0) {
            let best = 0, bestD = -1
            for (let i = 0; i < pool.length; i++) {
                const d = Math.min.apply(null, out.map(o => dist(o, pool[i])))
                if (d > bestD) { bestD = d; best = i }
            }
            out.push(pool.splice(best, 1)[0])
        }
        return out
    }
    // Colours the effect goes through
    readonly property var cycle: source === "multi" ? colors.map(x => Qt.lighter(x, 1))
                               : source === "theme" ? themeColors : [color]

    // ---------- reading ----------
    // One read at a time. A request during a read gets a fresh read right after it (its callback runs after that one),
    // so no callback is lost and none gets a state read before it asked
    property var readWaiting: []              // callbacks for the next read
    property var readCurrent: []              // callbacks of the read in progress
    property bool readAgain: false
    function refresh(done) {
        if (done) readWaiting = readWaiting.concat([done])
        if (reader.busy) { readAgain = true; return }
        readCurrent = readWaiting
        readWaiting = []
        readAgain = false
        reader.go(["sh", Quickshell.shellPath("scripts/kbdinfo.sh")], (code, out) => {
            root.parse(out)
            const cbs = root.readCurrent
            root.readCurrent = []
            for (const f of cbs) f()
            if (root.readAgain) root.refresh()
        })
    }
    function parse(out) {
        const leds = []                       // [{ name, driver, path, v: {file: value}, w: {file: true}, x: {file: true} }]
        const byName = {}
        const rgbZones = []                   // hp-wmi rgb_zones files, same shape as leds
        const usb = []
        let rule = ""
        for (const line of out.split("\n")) {
            const p = line.split("|")
            if (p[0] === "dmi" && p.length >= 6) dmi = { vendor: p[1], product: p[2], family: p[4], board: p[5] }
            else if (p[0] === "kernel") kernel = p[1] || ""
            else if (p[0] === "led" && p.length >= 4) { const l = { name: p[1], driver: p[2], path: p.slice(3).join("|"), v: {}, w: {}, x: {} }; leds.push(l); byName[p[1]] = l }
            else if (p[0] === "zone" && p.length >= 4) { const l = { name: p[1], driver: p[2], path: p.slice(3).join("|"), v: {}, w: {}, x: {} }; rgbZones.push(l); byName[p[1]] = l }
            else if ((p[0] === "v" || p[0] === "w" || p[0] === "x") && byName[p[1]]) byName[p[1]][p[0]][p[2]] = p[0] === "v" ? p.slice(3).join("|") : true
            else if (p[0] === "usb") usb.push({ vid: p[1], pid: p[2], name: p[3] || "" })
            else if (p[0] === "rule") rule = rule || (p[1] === "etc" ? (p[2] === "legacy" ? "etc-legacy" : "etc") : "lib")
        }
        if (!ready) loadSaved()               // load saved effect settings on the first read (the settings file is ready by now)
        ruleAt = rule.startsWith("etc") ? "etc" : rule
        ruleLegacy = rule === "etc-legacy"

        // Pick the backend
        const isRgb = l => !!l.v.multi_intensity && /red/.test(l.v.multi_index || "") && /green/.test(l.v.multi_index || "")
        // Numbered zone LEDs of one driver are one keyboard too, never a single light (that would write one zone only)
        const numbered = leds.filter(l => /::kbd_backlight_zone\d+$/.test(l.name))
        const named = leds.filter(l => l.name.indexOf("::kbd_zoned_backlight-") >= 0)
        const zoned = named.length > 0 ? named
                    : numbered.length > 0 && numbered.every(l => l.driver === numbered[0].driver) ? numbered : []
        const single = leds.find(l => /::kbd_backlight$/.test(l.name))
                    || leds.find(l => l.name.indexOf("::kbd_backlight") >= 0 && zoned.indexOf(l) < 0)
        const isHex = l => /^[0-9A-Fa-f]{6}$/.test(l.v.color || "")
        let use = [], kind = ""
        if (zoned.length > 0 && zoned.every(isRgb)) { use = zoned; kind = "hp-zoned" }
        else if (rgbZones.length > 0 && rgbZones.every(isHex)) { use = rgbZones; kind = "hp-rgbzones" }
        else if (single && isRgb(single)) { use = [single]; kind = "led-rgb" }
        else if (single && single.x.kbd_rgb_mode) { use = [single]; kind = "asus-tuf" }
        else if (single) { use = [single]; kind = "led-mono" }
        else if (zoned.length > 0) { use = zoned; kind = "led-mono" }
        backend = kind
        zones = use.map(l => l.path)
        name = kind === "hp-rgbzones" ? "hp-wmi rgb_zones ×" + use.length
             : use.length > 1 ? use[0].name.replace(/(-[^-]*|_zone\d+)$/, "") + " ×" + use.length : (use.length ? use[0].name : "")
        driver = use.length ? use[0].driver : ""
        const first = use[0]
        writable = use.length > 0 && (kind === "hp-rgbzones" ? use.every(l => !!l.w.color)
                 : use.every(l => l.w.brightness
                    && (kind === "led-rgb" || kind === "hp-zoned" ? !!l.w.multi_intensity : true)
                    && (kind === "asus-tuf" ? !!l.w.kbd_rgb_mode : true)))
        maxBrightness = first ? (parseInt(first.v.max_brightness) || 255) : 255
        // hp-rgbzones has no brightness: treat it as always full so nothing tries to "turn the light on"
        brightness = kind === "hp-rgbzones" ? maxBrightness : first ? (parseInt(first.v.brightness) || 0) : 0
        brightAt = Date.now()
        // The light shows the chosen colour unless an effect is chosen: while one is selected (also when a write error
        // stopped it or there's no permission) it may still show one of the effect's frames; the chosen colour is kept then.
        // An effect saved on a light that has no effects (settings from another keyboard) doesn't count.
        const fromHw = !effectsAllowed || (effect === "static" && Settings.kbdOf().effect === "static")
        if (kind === "hp-rgbzones") {
            maxIntensity = [255, 255, 255]
            if (fromHw) {
                const h = parseInt(first.v.color, 16)
                color = Qt.rgba((h >> 16 & 255) / 255, (h >> 8 & 255) / 255, (h & 255) / 255, 1)
            }
        } else if (kind === "led-rgb" || kind === "hp-zoned") {
            maxIntensity = (first.v.multi_max_intensity || "255 255 255").trim().split(/\s+/).map(x => parseInt(x) || 255)
            if (fromHw) {
                const it = first.v.multi_intensity.trim().split(/\s+/).map(x => parseInt(x) || 0)
                const order = (first.v.multi_index || "red green blue").trim().split(/\s+/)
                const ch = n => { const k = order.indexOf(n); return k < 0 ? 0 : it[k] / maxIntensity[k] }
                color = Qt.rgba(ch("red"), ch("green"), ch("blue"), 1)
            }
        } else maxIntensity = [255, 255, 255]

        // Findings worth explaining (RGB the kernel doesn't expose)
        const vendor = dmi.vendor.toLowerCase()
        const n = []
        if (vendor.indexOf("lenovo") >= 0 && usb.some(u => u.vid === "048d" && /^c9/i.test(u.pid))) n.push("legion-usb")
        if (vendor.indexOf("asus") >= 0 && kind !== "asus-tuf" && usb.some(u => u.vid === "0b05" && /n-key/i.test(u.name))) n.push("rog-usb")
        if ((vendor === "hp" || vendor.indexOf("hewlett") >= 0) && kind === "") n.push("hp-none")
        notes = n

        // ASUS TUF: the colour can't be read back, so the saved colour is applied once at startup
        if (!ready && kind === "asus-tuf" && writable && Settings.kbdOf().color !== "") queue("color", colorValue(color, 1))
        ready = true
    }

    // ---------- writing ----------
    // colour (0..1) x level -> "R G B" (hardware scale)
    function intensity(c, level) {
        return [c.r, c.g, c.b].map((x, i) => Math.round(Math.max(0, Math.min(1, x * level)) * (maxIntensity[i] ?? 255))).join(" ")
    }
    // The value written for a colour, per backend
    function colorValue(c, level) {
        if (backend === "hp-rgbzones")       // "RRGGBB" (no "#")
            return hex(Qt.rgba(Math.max(0, Math.min(1, c.r * level)), Math.max(0, Math.min(1, c.g * level)),
                               Math.max(0, Math.min(1, c.b * level)), 1)).slice(1)
        if (backend === "asus-tuf") {
            // cmd 0 = apply (1 would save to BIOS: never), mode 0 = static, speed 1 (ignored by static)
            const v = [c.r, c.g, c.b].map(x => Math.round(Math.max(0, Math.min(1, x * level)) * 255))
            return "0 0 " + v.join(" ") + " 1"
        }
        return intensity(c, level)
    }
    readonly property string colorFile: backend === "asus-tuf" ? "kbd_rgb_mode" : "multi_intensity"
    // The file a colour is written to: hp-rgbzones zones are the files themselves, the others are LED folders
    function colorPath(z) { return backend === "hp-rgbzones" ? z : z + "/" + colorFile }
    // Changes show in the UI immediately; the files are written at most ~12 times/s (while dragging)
    property var pending: ({})
    function setBrightness(b) {
        if (!hasBrightness) return
        if (!writable) { refresh(); return }  // without permission don't change the state; it may have been granted meanwhile, so check again
        brightness = Math.max(0, Math.min(maxBrightness, Math.round(b)))
        queue("brightness", String(brightness))
    }
    function setColor(c) {
        if (!rgb) return
        if (!writable) { refresh(); return }
        c = Qt.lighter(c, 1)                  // may also be a "#rrggbb" string (terminal, presets): convert to a colour
        color = Qt.rgba(c.r, c.g, c.b, 1)
        Settings.setKbd({ color: hex(color) })
        if (!effectActive) queue("color", colorValue(color, 1))   // with an effect running, the next frame applies it
        // Picking a colour at brightness 0 shows nothing; turn the light on so the user sees it. The key that turns the
        // light off (Fn) changes the brightness behind Ccenter's back, so a value older than a second is read again.
        if (!hasBrightness) return
        const age = Date.now() - brightAt
        if (brightness === 0) setBrightness(maxBrightness)
        else if ((age < 0 || age > 1000) && !brightReader.busy && pending.brightness === undefined) {
            brightAt = Date.now()
            brightReader.go(["cat", dir + "/brightness"], (code, out) => {
                const b = parseInt(out)
                // (a brightness the user set meanwhile wins over what was read before it was written)
                if (code !== 0 || !isFinite(b) || !root.hasBrightness || root.pending.brightness !== undefined) return
                root.brightness = b
                if (b === 0) root.setBrightness(root.maxBrightness)
            })
        }
    }
    property double brightAt: 0                // when the brightness was last read from the light
    function queue(kind, value) {
        if (!available) return
        const p = Object.assign({}, pending)
        p[kind] = value
        pending = p
        if (!flush.running) flush.start()
    }
    function writeNext() {
        // Colour first, then brightness (so the light never shows the new colour at the old brightness)
        if (pending.color !== undefined) sendColor(pending.color)
        if (pending.brightness !== undefined && hasBrightness) for (const z of zones) send(z + "/brightness", pending.brightness)
        pending = ({})
    }
    // Zoned lights: one write per zone (each a WMI colour-table read + write on HP), so fewer while dragging
    Timer { id: flush; interval: root.zones.length > 1 ? 250 : 80; onTriggered: root.writeNext() }

    // One persistent writer process (awk): starting a process per write would be costly during effects (25 frames/s).
    // awk reads its input buffered (bash "read" reads a pipe byte by byte, ~2x the CPU; Quickshell FileView ~3x).
    // Each write is two lines: file path, value. If a write fails, awk prints an error and exits.
    // Paths only ever come from detection (the light's own folder), never from user input.
    function send(path, value) {
        if (writer.running) writer.write(path + "\n" + value + "\n")
    }
    function sendColor(value) { for (const z of zones) send(colorPath(z), value) }
    function writeFailed(why) {
        error = I18n.t("Could not write to the keyboard light") + (why ? ": " + why : "")
        if (effect !== "static") lostEffect = true
        effect = "static"                     // stop the effect (not saved to settings)
        healthy.stop()
        refresh(() => root.scheduleRecover())  // re-read the real value and permission
    }
    // After a write error (the module was reloaded, the light went away, permission changed) the light is looked for
    // again: after 2, 4, 8, 16, 32 s, then every minute while it is missing. Once it's there and writable, a stopped
    // effect comes back, or the chosen colour is written again; the error goes once that works. A light that is there
    // but keeps failing is left alone after 5 tries (until a permission action); a minute without errors after a
    // recovery resets the count, and so does a light that comes back after being missing.
    property bool lostEffect: false           // the effect was stopped by a write error, not by the user
    property bool lightMissing: false         // the last recovery look found no light
    property int recoverTries: 0
    function scheduleRecover() {
        if (recoverTries >= 5 && available) return
        recover.interval = Math.min(60000, 2000 * Math.pow(2, recoverTries))
        recoverTries++
        recover.restart()
    }
    function tryRecover() {
        if (!available) { lightMissing = true; scheduleRecover(); return }
        if (lightMissing) {
            lightMissing = false
            recoverTries = 0
            // It has just appeared: udev may not have handed it to the user yet, so look once more before giving up
            if (!writable) { scheduleRecover(); return }
        }
        if (!writable) return                 // no permission: granting it brings the effect back (permDone)
        error = ""
        if (lostEffect) {
            lostEffect = false
            restoreSavedEffect()
        } else if (rgb && Settings.kbdOf().color !== "") {
            // A static write failed: write the chosen colour again (if it fails again, the error comes back)
            color = Settings.kbdOf().color
            queue("color", colorValue(color, 1))
        }
        healthy.restart()
    }
    Timer { id: recover; onTriggered: root.refresh(() => root.tryRecover()) }
    Timer { id: healthy; interval: 60000; onTriggered: root.recoverTries = 0 }
    Process {
        id: writer
        running: root.available
        command: ["awk", "NR % 2 { f = $0; next } { print > f; close(f) }"]
        stdinEnabled: true
        stderr: SplitParser { onRead: line => root.writeFailed(line.replace(/^.*fatal: /, "")) }
        // If it exits (error), restart it so later writes work
        onExited: if (root.available) restartWriter.start()
    }
    Timer { id: restartWriter; interval: 1000; onTriggered: writer.running = true }

    // ---------- effects ----------
    function setEffect(e) {
        if (e === effect && !lostEffect) return
        lostEffect = false                    // the user chose (also "static" after an error stopped the effect): keep it
        if (e === "cycle" && source === "single") setSource("multi")   // a cycle needs more than one colour
        effect = e
        Settings.setKbd({ effect: e })
        if (e === "static") queue("color", colorValue(color, 1))   // effect off: back to the chosen colour
        else { t0 = Date.now(); lastFrame = "" }
    }
    // Returns false for "single" while a colour cycle is chosen (a cycle needs more than one colour)
    function setSource(s) {
        if (s === "single" && effect === "cycle") return false
        source = s
        Settings.setKbd({ source: s })
        lastFrame = ""
        return true
    }
    // The saved effect back (after a write error or a permission change); a cycle can't run on a single colour
    function restoreSavedEffect() {
        const e = Settings.kbdOf().effect
        if (e === "cycle" && source === "single") setSource("multi")
        effect = e
        lastFrame = ""
    }
    function setThemeCount(n) { themeCount = Math.max(3, Math.min(6, Math.round(n))); Settings.setKbd({ themeCount: themeCount }) }
    function setColors(list) { colors = list.slice(0, 6); Settings.setKbd({ colors: colors }) }
    function setSpeed(v) {
        // When the speed changes, continue from the same point of the breath (keep the phase)
        const now = Date.now(), p = ((now - t0) / 1000) / period
        speed = Math.max(0, Math.min(1, v))
        t0 = now - p * period * 1000
        saveSpeed.restart()
    }
    Timer { id: saveSpeed; interval: 500; onTriggered: Settings.setKbd({ speed: root.speed }) }
    // Lowest/highest level; if one passes the other, it pushes it along
    function setRange(lo, hi) {
        lo = Math.max(0, Math.min(1, lo))
        hi = Math.max(0, Math.min(1, hi))
        if (lo > hi) { if (lo !== minLevel) hi = lo; else lo = hi }
        minLevel = lo
        maxLevel = hi
        saveRange.restart()
    }
    Timer { id: saveRange; interval: 500; onTriggered: Settings.setKbd({ min: root.minLevel, max: root.maxLevel }) }

    function mix(a, b, f) { return Qt.rgba(a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f, 1) }

    property double t0: Date.now()
    property string lastFrame: ""
    // Mix along the hue: no dark/muddy midpoint between vivid colours (red -> yellow -> green)
    function mixHsv(a, b, f) {
        if (a.hsvSaturation < 0.05 || b.hsvSaturation < 0.05) return mix(a, b, f)   // white/grey: plain RGB mix
        let d = b.hsvHue - a.hsvHue
        if (d > 0.5) d -= 1
        if (d < -0.5) d += 1
        const h = ((a.hsvHue + d * f) % 1 + 1) % 1
        return Qt.hsva(h, a.hsvSaturation + (b.hsvSaturation - a.hsvSaturation) * f, a.hsvValue + (b.hsvValue - a.hsvValue) * f, 1)
    }
    // At very low levels the channels round to tiny integers and the hue shifts (pink -> [1 0 0] red, for example);
    // when the strongest channel drops below this threshold the light turns off instead of showing a wrong colour
    readonly property int lowCut: 8
    function frame(c, level) {
        const v = [c.r, c.g, c.b].map((x, i) => Math.max(0, Math.min(1, x * level)) * (maxIntensity[i] ?? 255))
        if (Math.max.apply(null, v) < lowCut) return "0 0 0"
        return v.map(Math.round).join(" ")
    }
    function tick() {
        const n = cycle.length
        if (n === 0) return
        const now = Date.now()
        if (now < t0) t0 = now                 // the clock jumped back: start over instead of freezing until it catches up
        const t = (now - t0) / 1000 / period
        const k = Math.floor(t)
        const f = t - k
        if (cycleIndex !== k % n) cycleIndex = k % n
        let v
        if (effect === "cycle") {
            // Colour cycle: from colour k to the next with an ease-in/ease-out transition; the light never dims
            const e = (1 - Math.cos(Math.PI * f)) / 2
            v = frame(n > 1 ? mixHsv(cycle[k % n], cycle[(k + 1) % n], e) : cycle[0], 1)
        } else {
            // Breathing: one colour per breath; the colour changes at the darkest point (f = 0), nothing in between
            const l = Math.pow((1 - Math.cos(2 * Math.PI * f)) / 2, 1.8)   // 0 -> 1 -> 0, a bit longer in the dark part (perceived brightness is not linear)
            v = frame(cycle[k % n], minLevel + (maxLevel - minLevel) * l)
        }
        if (v !== lastFrame) { lastFrame = v; sendColor(v) }
    }
    Timer {
        // ~80 frames per breath is smooth enough: 25 frames/s when fast, fewer when slow (less CPU).
        // Zoned keyboards need one write per zone per frame, so they get half the frame rate.
        interval: Math.max(40, Math.min(100, root.period * 1000 / 80)) * (root.zones.length > 1 ? 2 : 1)
        repeat: true
        running: root.effectActive && writer.running
        onTriggered: root.tick()
    }

    // Flush pending (500 ms after dragging) speed/level saves into the settings right away
    function flushSaves() {
        if (saveSpeed.running) { saveSpeed.stop(); Settings.setKbd({ speed: speed }) }
        if (saveRange.running) { saveRange.stop(); Settings.setKbd({ min: minLevel, max: maxLevel }) }
    }
    // On quit: don't leave the keyboard dark in the middle of an effect; go back to the chosen static colour (then cb)
    // A colour or brightness change still waiting for its write (80/250 ms) is written too, colour first.
    function restoreForQuit(cb) {
        flushSaves()
        const c = effectActive ? colorValue(color, 1) : pending.color
        const writes = []                                   // file, value, file, value, ...
        if (c !== undefined) for (const z of zones) writes.push(colorPath(z), c)
        if (pending.brightness !== undefined && hasBrightness) for (const z of zones) writes.push(z + "/brightness", pending.brightness)
        flush.stop()
        pending = ({})
        if (effectActive) effect = "static"                 // not saved: the effect resumes on the next start
        if (writes.length === 0 || !writable) { cb(); return }
        oneShot.go(["sh", "-c", 'while [ $# -gt 1 ]; do printf "%s\\n" "$2" > "$1"; shift 2; done', "sh"].concat(writes), () => cb())
    }

    // ---------- write permission (only when the user asks; via the system password dialog) ----------
    // The rule template ships with the app (dist/90-ccenter.rules). @UID@ is replaced with the user who authenticated
    // (PKEXEC_UID, set by pkexec itself), so only that user gets write access. The rendered rule is written to a
    // root-owned file first and checked line by line (scripts/kbd-rule-check.sh: only the expected chown lines);
    // nothing is changed if the check fails. Then the old world-writable permissions (0666, earlier versions) are
    // reset, the rule goes to /etc/udev/rules.d and udev applies it right away.
    readonly property string ruleFile: Quickshell.shellPath("dist/90-ccenter.rules")
    readonly property string ruleCheck: Quickshell.shellPath("scripts/kbd-rule-check.sh")
    readonly property string resetPerms:
        'for d in /sys/class/leds/*::kbd_backlight /sys/class/leds/*::kbd_backlight_* /sys/class/leds/*::kbd_zoned_backlight-*; do ' +
        '[ -d "$d" ] || continue; for f in brightness multi_intensity; do [ -e "$d/$f" ] && chown root:root "$d/$f" && chmod 0644 "$d/$f"; done; ' +
        '[ -e "$d/kbd_rgb_mode" ] && chown root:root "$d/kbd_rgb_mode"; done; ' +
        'for f in /sys/devices/platform/hp-wmi/rgb_zones/zone*; do [ -e "$f" ] && chown root:root "$f" && chmod 0644 "$f"; done; '
    function grantPermission() {
        // While the files change hands (old permissions reset, then the new owner set) a running effect must not write,
        // or it hits "Permission denied" in between. Pause it; permDone() resumes it once the new state has been read.
        if (effectActive) paused = true
        permBusy = true
        error = ""
        perm.go(["pkexec", "sh", "-c",
                 'uid=$PKEXEC_UID; case "$uid" in ""|*[!0-9]*) echo "unknown user" >&2; exit 1 ;; esac; ' +
                 'new=/etc/udev/rules.d/90-ccenter.rules.new; ' +
                 'sed "s/@UID@/$uid/g" "$1" > "$new" && sh "$2" "$new" "$uid" || { rm -f "$new"; exit 1; }; ' +
                 resetPerms +
                 'chmod 644 "$new" && mv "$new" /etc/udev/rules.d/90-ccenter.rules && ' +
                 'udevadm control --reload-rules && udevadm trigger --action=change --subsystem-match=leds && ' +
                 'udevadm trigger --action=change --subsystem-match=platform --sysname-match=hp-wmi && udevadm settle',
                 "sh", ruleFile, ruleCheck],
                (code, out, err) => root.permDone(code, out, err))
    }
    function revokePermission() {
        // Without permission the effect can't write: switch the keyboard to the chosen static colour and pause the effect.
        // The saved choice is kept, so the effect resumes once permission is granted again.
        if (effectActive) { paused = true; sendColor(colorValue(color, 1)) }
        permBusy = true
        error = ""
        perm.go(["pkexec", "sh", "-c",
                 'rm -f /etc/udev/rules.d/90-ccenter.rules && udevadm control --reload-rules; ' + resetPerms + 'true'],
                (code, out, err) => root.permDone(code, out, err))
    }
    function permDone(code, out, err) {
        permBusy = false
        // 126/127: password dialog dismissed / not authorised
        if (code !== 0) error = code === 126 || code === 127 ? I18n.t("Cancelled.") : I18n.t("Failed: %1").arg((err || out).trim().split("\n")[0])
        else {
            restoreSavedEffect()              // a write that failed earlier may have stopped the effect: bring back the saved one
            lostEffect = false
            lightMissing = false
            recoverTries = 0
        }
        // Unpause only after the new permission state has been read: if permission is gone, writable=false stops the effect;
        // if it was cancelled, the effect continues (unpausing earlier would make the effect write without permission and stop itself)
        refresh(() => { root.paused = false })
    }
    Cmd { id: perm }

    // For the terminal (IPC): "#rrggbb" and percent
    function hex(c) {
        const h = x => ("0" + Math.round(x * 255).toString(16)).slice(-2)
        return ("#" + h(c.r) + h(c.g) + h(c.b)).toUpperCase()
    }
    function kindText() {
        return backend === "hp-zoned" || backend === "hp-rgbzones" ? I18n.t("RGB, %1 zones (same color on all)").arg(zones.length)
             : backend === "asus-tuf" ? I18n.t("RGB, single zone, ASUS TUF")
             : backend === "led-rgb" ? I18n.t("RGB, single zone")
             : I18n.t("single color, %1 brightness levels").arg(maxBrightness)
    }
    // Detection report (ccenter kbd info): what was found, step by step, for bug reports
    function infoText() {
        const lines = [
            I18n.t("Laptop: %1").arg([dmi.vendor, dmi.product].filter(x => x).join(" · ") || "?") + (dmi.board ? " (" + dmi.board + ")" : ""),
            I18n.t("Kernel: %1").arg(kernel || "?"),
            I18n.t("Driver: %1").arg(driver || "-"),
            I18n.t("Light: %1").arg(available ? name + " (" + kindText() + ")" : I18n.t("none found")),
            I18n.t("Permission: %1").arg(!available ? "-" : writable ? I18n.t("yes") + (ruleLegacy ? " (" + I18n.t("old, less strict rule") + ")" : "") : I18n.t("no")),
            I18n.t("Support: %1").arg(supportText())
        ]
        for (const n of notes) lines.push("- " + noteText(n))
        return lines.join("\n")
    }
    function supportText() {
        if (!available) return I18n.t("not supported")
        if (tested) return I18n.t("full (tested)")
        if (backend === "hp-rgbzones") return I18n.t("color only, same on all zones (not tested on this keyboard type)")
        if (rgb) return I18n.t("color and brightness (not tested on this model)")
        return I18n.t("brightness only")
    }
    function noteText(n) {
        return n === "legion-usb" ? I18n.t("This Lenovo has a USB RGB keyboard controller (ITE). Its colors are not exposed by the kernel; tools like L5P-Keyboard-RGB or OpenRGB control it.")
             : n === "rog-usb" ? I18n.t("This ASUS has an Aura (N-KEY) USB keyboard. Its colors are controlled by asusctl, not through the kernel light.")
             : n === "hp-none" ? I18n.t("No HP keyboard light found. If this laptop has an RGB keyboard: the official kernel doesn't support HP keyboard lights yet, they need the extra hp-wmi-fan-and-backlight-control module (see the README). Per-key RGB keyboards aren't supported.")
             : n
    }
    function noPermissionText() { return I18n.t("No write permission: Keyboard tab > 'Grant permission' (or install.sh)") }
    function statusText() {
        if (!available) return I18n.t("No keyboard light found")
        const src = { single: I18n.t("chosen color"), multi: I18n.t("%1 colors").arg(colors.length),
                      theme: I18n.t("Caelestia theme, %1 colors").arg(themeCount) }
        const fx = !effectsAllowed ? I18n.t("static")
            : effect === "breathing"
            ? I18n.t("breathing (%1, %2 s, %3%–%4%)").arg(src[source]).arg(I18n.num(period, 1)).arg(Math.round(minLevel * 100)).arg(Math.round(maxLevel * 100))
            : effect === "cycle" ? I18n.t("color cycle (%1, %2 s per color)").arg(src[source]).arg(I18n.num(period, 1)) : I18n.t("static")
        return I18n.t("Keyboard light: %1").arg(name) + " (" + kindText() + ")"
            + "\n" + I18n.t("Color: %1").arg(rgb ? hex(color) : "-")
            + (hasBrightness ? "\n" + I18n.t("Brightness: %1%").arg(Math.round(brightness * 100 / maxBrightness)) : "")
            + "\n" + I18n.t("Effect: %1").arg(fx)
            + (writable ? "" : "\n" + noPermissionText())
    }

    Cmd { id: reader }
    Cmd { id: oneShot }
    Cmd { id: brightReader; timeout: 5000 }
    function loadSaved() {
        const k = Settings.kbdOf()
        // Older versions could save a colour cycle on the single chosen colour (it can't cycle): use the colour list
        source = k.effect === "cycle" && k.source === "single" ? "multi" : k.source
        colors = k.colors
        speed = k.speed
        themeCount = k.themeCount
        minLevel = k.min
        maxLevel = k.max
        if (k.color !== "") color = k.color
        effect = k.effect
        t0 = Date.now()
    }
    Component.onCompleted: refresh()
}
