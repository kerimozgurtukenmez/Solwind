pragma Singleton
import Quickshell
import QtQuick
import Solwind

// NBFC's state and the fan actions, as the pages know them. solwindd reads NBFC, drives the fans (Fixed/Curve, max
// fan, the safety limit), runs the service / config actions with the admin password and keeps the graph's history;
// this only mirrors its /solwind/Fans and passes the user's actions on.
Singleton {
    id: root

    readonly property var f: Solwind.fans
    readonly property bool up: Solwind.connected === true

    readonly property bool running: up && f.Running === true
    readonly property bool readOnly: up && f.ReadOnly === true
    readonly property string configId: up ? (f.ConfigId ?? "") : ""
    readonly property string fileConfigId: up ? (f.FileConfigId ?? "") : ""     // selected in /etc/nbfc/nbfc.json
    readonly property bool noConfig: up && f.NoConfig === true
    // A config is selected but NBFC still runs the old one (used after NBFC restarts)
    readonly property bool configPending: up && f.ConfigPending === true
    readonly property var fans: up && f.Fans ? Array.from(f.Fans) : []   // [{ name, temp, auto, critical, current, target, rpm }]
    readonly property real temp: up && typeof f.Temp === "number" ? f.Temp : NaN
    readonly property bool bootEnabled: up && f.BootEnabled === true
    readonly property var configs: up && f.Configs ? Array.from(f.Configs) : []
    readonly property string message: up ? (f.Message ?? "") : ""
    readonly property bool messageError: up && f.MessageError === true
    readonly property bool controlling: up && f.Controlling === true          // Solwind drives fans (Fixed/Curve/max fan)

    // At this temperature fans in Fixed/Curve mode go to 100%. ServiceCard sets it while its slider moves; for half a
    // second after that the daemon's echoes (older values of the drag) don't pull it back, then its value shows again
    // (by then the same; RestoreNone: switching the Binding off keeps what the slider set).
    property real safety: 90
    property bool safetyHeld: false
    function holdSafety() { safetyHeld = true; safetyRelease.restart() }
    Timer { id: safetyRelease; interval: 500; onTriggered: root.safetyHeld = false }
    Binding {
        target: root; property: "safety"; when: !root.safetyHeld; restoreMode: Binding.RestoreNone
        value: root.up && typeof root.f.Safety === "number" ? root.f.Safety : 90
    }

    // The selected config's threshold table per fan: [{ points: [{t, s}], hyst } | null] (null: none in the config)
    readonly property var configCurves: up && f.ConfigCurves ? Array.from(f.ConfigCurves).map(c => c && c.points ? c : null) : []
    // Where the config list came from (the folders, or `nbfc config -l`), as the config card shows it
    readonly property string configNote: {
        const l = up && f.ConfigList ? f.ConfigList : ({})
        const fromDirs = l.fromDirs ?? 0, fromCli = l.fromCli ?? 0, code = l.cliCode ?? -1
        const cli = code < 0 ? "" : code === 0 ? (fromCli === 0 ? I18n.t("nbfc config -l returned nothing") : "")
                  : I18n.t("nbfc config -l failed (code %1): %2").arg(code).arg(l.cliLine ?? "")
        const from = fromDirs > 0 ? " (" + Array.from(l.dirs ?? []).join(", ") + ")" : fromCli > 0 ? " (nbfc config -l)" : ""
        return (configs.length > 0 ? I18n.t("%1 configs").arg(configs.length) + from : I18n.t("Could not get the list"))
             + (fromDirs === 0 && fromCli === 0 ? " · " + I18n.t("only the selected config is known, no config folder found") : "")
             + (cli !== "" && fromDirs === 0 ? " · " + cli : "")
    }
    // Configs `nbfc config -r` names for this laptop: set by recommend() only (as before), so a new window starts empty
    property var recommended: []
    function recommend() {
        Solwind.callLater("fans", "Recommend", "", [], r => {
            if (r.ok && r.values[0] === true) root.recommended = Array.from(r.values[1])
            else if (!r.ok) console.warn("[solwind] fans.Recommend: " + r.error)
        })
    }

    function startService() { Daemon.send("fans", "StartService") }
    function stopService() { Daemon.send("fans", "StopService") }
    function restartService(ro) { Daemon.send("fans", "RestartService") }    // (ro: the current mode, which the daemon uses)
    function setReadOnly(v) { Daemon.send("fans", "SetReadOnly", "b", [v === true]) }
    function setBoot(v) { Daemon.send("fans", "SetBoot", "b", [v === true]) }
    function applyConfig(name) { Daemon.send("fans", "ApplyConfig", "s", [String(name)]) }

    // ---------- max fan ----------
    readonly property double boostUntil: up && typeof f.BoostUntil === "number" ? f.BoostUntil : 0   // 0 off, -1 until turned off, else the end (ms)
    readonly property bool boosting: boostUntil !== 0
    // Why max fan can't be turned on now ("" = it can)
    function boostBlocked() { return up ? (f.BoostBlocked ?? "") : Daemon.downText }
    // Returns false if it wasn't done (turning it off always works while the daemon runs)
    function setBoost(minutes) { return Daemon.send("fans", "SetBoost", "x", [minutes]).ok }

    // ---------- temperatures (for the cards' live readings) ----------
    // The temperature NBFC reads for this fan (the sensor from the config; CPU on most laptops)
    function tempOf(i) {
        const t = fans[i] ? fans[i].temp : NaN
        return isFinite(t) ? t : temp
    }
    // st.src: "cpu" (NBFC's sensor, default) | "gpu" | "max". Falls back to CPU if the GPU can't be read (asleep/absent).
    function sourceTemp(i, src) {
        const c = tempOf(i), g = Sensors.gpu
        if (src === "gpu") return isFinite(g) ? g : c
        if (src === "max") return isFinite(g) ? (isFinite(c) ? Math.max(c, g) : g) : c
        return c
    }

    // ---------- the window (solwindd reads faster while one is open) ----------
    property bool uiVisible: true             // is the window open (bound in shell.qml)
    onUiVisibleChanged: Solwind.watch("window", uiVisible)
    Component.onCompleted: {
        Solwind.watch("window", uiVisible)
        if (up) loadHistory()
    }

    // ---------- history (last 10 minutes, one sample per status read; kept by the daemon) ----------
    property var history: []                 // [{ t: ms, cpu: °C, gpu: °C|NaN, fan: % (average) }]
    readonly property int historyMs: 600000
    function loadHistory() {
        const r = Daemon.call("fans", "History")
        history = r.ok ? Array.from(r.value) : []
    }
    Connections {
        target: Solwind
        function onConnectedChanged() {
            if (Solwind.connected === true) root.loadHistory()
            else { root.history = []; if (root.quitting) Qt.quit() }    // (releaseAndQuit: solwindd is gone)
        }
        function onSampled(s) {
            const h = root.history
            if (h.length && s.t <= h[h.length - 1].t) return    // (already in what History() gave)
            const keep = h.filter(x => s.t - x.t <= root.historyMs)
            keep.push(s)
            root.history = keep
        }
    }

    // Quit completely: solwindd hands the fans back to NBFC's automatic control and leaves the keyboard light on its
    // static colour, then quits; this process ends once the daemon is gone, so `solwind quit` (which waits for it)
    // returns only after the hand-back, as with the QML app. At most 25 s (the daemon's own limit is 20 s).
    property bool quitting: false
    function releaseAndQuit() {
        if (quitting) return
        quitting = true
        if (!up || !Daemon.send("daemon", "Quit").ok) { Qt.quit(); return }
        quitWait.start()
    }
    Timer { id: quitWait; interval: 25000; onTriggered: { console.warn("[solwind] solwindd didn't quit within 25 s"); Qt.quit() } }
}
