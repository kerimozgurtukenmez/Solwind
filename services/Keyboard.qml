pragma Singleton
import Quickshell
import QtQuick
import Solwind

// The keyboard light, as the Keyboard page knows it. solwindd finds the light (only reading), writes colour and
// brightness, runs the effects (also with no window open), recovers after write errors and grants / removes the write
// permission (admin password); this mirrors its /solwind/Keyboard and passes the page's changes on, unrounded.
Singleton {
    id: root

    readonly property var k: Solwind.keyboard
    readonly property bool up: Solwind.connected === true

    // ---------- what was found ----------
    readonly property string backend: up ? (k.Backend ?? "") : ""   // "" (none) | led-rgb | hp-zoned | hp-rgbzones | asus-tuf | led-mono
    readonly property bool available: up && k.Available === true
    readonly property bool ready: up && k.Ready === true              // first read finished?
    readonly property string name: up ? (k.Name ?? "") : ""           // e.g. hp::kbd_backlight (or "hp::kbd_zoned_backlight ×4")
    readonly property string driver: up ? (k.Driver ?? "") : ""       // kernel driver of the light, e.g. hp-wmi
    readonly property var dmi: up && k.Dmi ? k.Dmi : ({ vendor: "", product: "", family: "", board: "" })
    readonly property string kernel: up ? (k.Kernel ?? "") : ""
    readonly property var notes: up && k.Notes ? Array.from(k.Notes) : []   // legion-usb, rog-usb, hp-none
    readonly property bool rgb: up && k.Rgb === true
    readonly property bool hasBrightness: up && k.HasBrightness === true
    readonly property bool effectsAllowed: up && k.EffectsAllowed === true   // software effects (only where tested)
    readonly property bool tested: up && k.Tested === true
    readonly property bool writable: up && k.Writable === true
    readonly property string ruleAt: up ? (k.RuleAt ?? "") : ""      // the permission rule: etc | lib | ""
    readonly property bool ruleLegacy: up && k.RuleLegacy === true   // an older, less strict rule is installed
    readonly property bool ruleOld: up && k.RuleOld === true         // the rule has the old name (Ccenter's)
    readonly property bool permBusy: up && k.PermBusy === true
    readonly property string error: up ? (k.Error ?? "") : ""

    // ---------- what the page sets shows at once ----------
    // A drag sends a value per step, and each comes back from the daemon a moment later: the slider (or the wheel's
    // marker) would be pulled back to an older one. So for half a second after the page set a value, that value is
    // shown, made as the daemon makes it (Keyboard.qml's rules); then the daemon's again, by then the same.
    property var held: ({})                   // name -> the value the page set
    function hold(name, v) { const h = Object.assign({}, held); h[name] = v; held = h; release.restart() }
    Timer { id: release; interval: 500; onTriggered: root.held = ({}) }

    // ---------- the state ----------
    readonly property int maxBrightness: up && k.MaxBrightness > 0 ? k.MaxBrightness : 255
    readonly property int brightness: held.brightness ?? (up ? (k.Brightness ?? 0) : 0)
    // The chosen colour (at full intensity), unrounded (the wheel's marker follows it)
    readonly property color color: held.color ?? (up && k.ColorF && k.ColorF.length === 3 ? Qt.rgba(k.ColorF[0], k.ColorF[1], k.ColorF[2], 1)
                                                  : up && k.Color ? k.Color : "white")
    readonly property string effect: up ? (k.Effect ?? "static") : "static"   // static | breathing | cycle
    readonly property string source: up ? (k.Source ?? "single") : "single"   // single | multi | theme (Caelestia)
    readonly property var colors: up && k.Colors ? Array.from(k.Colors) : []  // "#rrggbb", 1..6
    readonly property real speed: held.speed ?? (up && typeof k.Speed === "number" ? k.Speed : 0.5)   // 0 slow .. 1 fast
    readonly property real period: held.speed === undefined && up && typeof k.Period === "number" ? k.Period : 8 - 7 * speed   // one breath / one colour (s)
    readonly property real minLevel: held.minLevel ?? (up && typeof k.MinLevel === "number" ? k.MinLevel : 0)   // lowest breathing level (0..1)
    readonly property real maxLevel: held.maxLevel ?? (up && typeof k.MaxLevel === "number" ? k.MaxLevel : 1)
    readonly property int themeCount: up ? (k.ThemeCount ?? 3) : 3   // number of Caelestia colours (3..6)
    readonly property var themeColors: up && k.ThemeColors ? Array.from(k.ThemeColors) : []   // as the effect uses them
    readonly property bool effectActive: up && k.EffectActive === true
    readonly property bool paused: up && k.Paused === true           // the effect waits while the permission changes
    readonly property int cycleIndex: up ? (k.CycleIndex ?? 0) : 0   // the colour the effect shows now

    // ---------- changes ----------
    function refresh() { Daemon.send("keyboard", "Refresh") }
    // In the light's own steps (the daemon ignores it without brightness or permission, as Keyboard.qml did)
    function setBrightness(b) {
        if (hasBrightness && writable && isFinite(b)) hold("brightness", Math.max(0, Math.min(maxBrightness, Math.round(b))))
        Daemon.send("keyboard", "SetBrightnessRaw", "d", [Number(b)])
    }
    // A colour, or a "#rrggbb" string (the presets). The wheel's colour goes over as it is (its 16-bit value: the
    // daemon then does exactly what Keyboard.qml did with it; checked on every hue/saturation of the wheel)
    function setColor(c) {
        if (rgb && writable) { const x = Qt.lighter(c, 1); hold("color", Qt.rgba(x.r, x.g, x.b, 1)) }
        if (typeof c === "string") Daemon.send("keyboard", "SetColor", "s", [c])
        else Daemon.send("keyboard", "SetColorF", "ddd", [c.r, c.g, c.b])
    }
    function setEffect(e) { Daemon.send("keyboard", "SetEffect", "s", [e]) }
    // Returns false for "single" while a colour cycle is chosen (a cycle needs more than one colour)
    function setSource(s) { return Daemon.call("keyboard", "SetSource", "s", [s]).ok }
    function setThemeCount(n) { Daemon.send("keyboard", "SetThemeCount", "i", [Math.round(n)]) }
    function setColors(list) { Daemon.send("keyboard", "SetColors", "as", [Array.from(list)]) }
    function setSpeed(v) {
        if (isFinite(v)) hold("speed", Math.max(0, Math.min(1, v)))
        Daemon.send("keyboard", "SetSpeedF", "d", [Number(v)])
    }
    // Lowest/highest level; if one passes the other, it pushes it along
    function setRange(lo, hi) {
        if (isFinite(lo) && isFinite(hi)) {
            let l = Math.max(0, Math.min(1, lo)), h = Math.max(0, Math.min(1, hi))
            if (l > h) { if (l !== minLevel) h = l; else l = h }
            hold("minLevel", l)
            hold("maxLevel", h)
        }
        Daemon.send("keyboard", "SetRangeF", "dd", [Number(lo), Number(hi)])
    }
    function grantPermission() { Daemon.send("keyboard", "GrantPermission") }
    function revokePermission() { Daemon.send("keyboard", "RevokePermission") }

    // ---------- texts ----------
    function hex(c) {
        const h = x => ("0" + Math.round(x * 255).toString(16)).slice(-2)
        return ("#" + h(c.r) + h(c.g) + h(c.b)).toUpperCase()
    }
    function kindText() { return up ? (k.KindText ?? "") : "" }
    function supportText() { return up ? (k.SupportText ?? "") : "" }
    function noteText(n) { const i = notes.indexOf(n); return i >= 0 && up && k.NoteTexts ? (k.NoteTexts[i] ?? n) : n }
    function noPermissionText() { return up ? (k.NoPermissionText ?? "") : "" }
    // Detection report and status (solwind kbd info / solwind kbd)
    function infoText() { return Daemon.text("keyboard", "InfoText") }
    function statusText() { return Daemon.text("keyboard", "StatusText") }
}
