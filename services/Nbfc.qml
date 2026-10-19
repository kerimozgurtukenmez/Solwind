pragma Singleton
import Quickshell
import Quickshell.Io
import QtQuick
import qs.utils

// Backend talking to nbfc-linux. Setting fan speeds needs no root; service/config actions go through pkexec.
Singleton {
    id: root

    // ---- state ----
    property bool running: false
    property bool readOnly: false
    property string configId: ""
    property var fans: []                 // [{ name, temp, auto, current, target }]
    property real temp: NaN
    property bool bootEnabled: false
    property var configs: []
    property var recommended: []
    property string message: ""
    property bool messageError: false
    property real safety: isFinite(Settings.safety()) ? Settings.safety() : 90   // at this temperature fans in Fixed/Curve mode go to 100%

    property var targets: ({})            // fan index -> st (only for Fixed/Curve fans)
    property var lastSent: ({})
    // "Did NBFC see it yet?" is judged by counting status reads, not by the clock (a clock jump can't break it)
    property int pollSeq: 0               // status reads started so far
    property int statusSeq: 0             // number of the status read being processed
    property var sentSeq: ({})            // fan index -> pollSeq when NBFC took the last speed (Infinity while on the way)
    property var releasing: ({})          // fan index -> { at, seq, tries }: handed back to NBFC auto, not confirmed yet
    property var queue: []
    property var adminQueue: []
    property bool logged: false

    // ---------- command queues ----------
    // nbfc commands run one after another. Admin actions (pkexec) have their own queue: the password dialog can stay
    // open for a long time and must not hold up fan speeds or the safety limit.
    function enqueue(key, cmd, cb) {
        const q = queue.filter(e => key === "" || e.key !== key)
        q.push({ key: key, cmd: cmd, cb: cb })
        queue = q
        pump()
    }
    function pump() {
        if (runner.busy || queue.length === 0) return
        const e = queue[0]
        queue = queue.slice(1)
        runner.go(e.cmd, (code, out, err) => {
            if (e.cb) e.cb(code, out, err)
            Qt.callLater(root.pump)
        })
    }
    function enqueueAdmin(cmd, cb) {
        adminQueue = adminQueue.concat([{ cmd: cmd, cb: cb }])
        pumpAdmin()
    }
    function pumpAdmin() {
        if (adminRunner.busy || adminQueue.length === 0) return
        const e = adminQueue[0]
        adminQueue = adminQueue.slice(1)
        adminRunner.go(e.cmd, (code, out, err) => {
            if (e.cb) e.cb(code, out, err)
            Qt.callLater(root.pumpAdmin)
        })
    }
    function firstLine(s) { return s.trim().split("\n")[0] }
    function lines(s) { return s.split("\n").map(l => l.trim()).filter(l => l !== "") }

    // While an admin action runs and for a few seconds after it, NBFC may stop and start: that is not reported as
    // "stopped unexpectedly" or "could not set the speed"
    property double adminQuietUntil: 0
    function adminQuiet() {
        const left = adminQuietUntil - Date.now()          // (a clock jump back doesn't stretch it)
        return adminRunner.busy || adminQueue.length > 0 || (left > 0 && left <= 5000)
    }

    // actions that need root
    function sudo(args, label, then) {
        message = label
        messageError = false
        enqueueAdmin(["pkexec"].concat(args), (code, out, err) => {
            root.adminQuietUntil = Date.now() + 5000
            if (code === 0) {
                root.message = ""
            } else {
                root.message = I18n.t("Failed: %1").arg(root.firstLine(err !== "" ? err : out) + " (" + I18n.t("code %1").arg(code) + ")")
                root.messageError = true
            }
            if (then) then(code)
            root.poll()
        })
    }

    // ---------- service ----------
    property bool userStopped: false         // if the user stopped the service, don't send the "stopped unexpectedly" notification
    property bool lostPending: false         // the service went away; reported once no admin action explains it
    signal serviceLost()                     // a running service stopped unexpectedly
    signal commandFailed(string msg)         // a fan speed could not be written
    signal boostEnded()                      // a timed max fan run ended

    function startService() {
        userStopped = false
        lostPending = false
        sudo(["nbfc", "start"].concat(readOnly ? ["-r"] : []), I18n.t("Waiting for admin permission…"))
    }
    function stopService() { handBackThen(["nbfc", "stop"], code => { if (code === 0) root.userStopped = true }) }
    function restartService(ro) { sudo(["nbfc", "restart"].concat(ro ? ["-r"] : []), I18n.t("Waiting for admin permission…")) }
    // NBFC saves the fans' target speeds when it stops and sets them again at its next start (/var/lib/nbfc/state.json),
    // also when Ccenter isn't running then, and a new config gets them by fan number. So before stopping or switching
    // the config, every fan goes back to NBFC's automatic control, and no speeds are written until the action has
    // finished. If it fails or is cancelled, Ccenter takes its own fans again on the next round; fans it doesn't drive
    // (no saved setting, or fixed from outside) stay on auto.
    property int holds: 0                     // admin actions that hand the fans back, still running
    readonly property bool holding: holds > 0
    function handBackThen(args, then) {
        holds++
        const act = () => root.sudo(args, I18n.t("Waiting for admin permission…"), code => {
            root.holds--
            if (then) then(code)
        })
        // Always: the last status read may not show a speed that is still on the way, and in read-only mode NBFC also
        // keeps and saves the targets (it only doesn't write them to the EC). Key "" is never replaced by a later
        // command, so `act` always runs, after the speed commands already waiting.
        if (running) enqueue("", ["nbfc", "set", "-a"], () => act())
        else act()
    }
    function setReadOnly(v) {
        const was = root.running
        readOnly = v
        if (was) restartService(v)
    }
    function setBoot(v) {
        sudo(["systemctl", v ? "enable" : "disable", "nbfc_service"], I18n.t("Waiting for admin permission…"), () => root.refreshBoot())
    }
    function refreshBoot() {
        enqueue("boot", ["systemctl", "is-enabled", "nbfc_service"], (code, out) => { root.bootEnabled = out.trim() === "enabled" })
    }

    // ---------- config ----------
    property var cfgFromCli: []
    property var cfgFromDir: []
    property string cfgNoteCli: ""
    property string configNote: ""

    // List: the config folders first (file name = config name), otherwise `nbfc config -l`. The selected config is always listed.
    function mergeConfigs() {
        const src = cfgFromDir.length > 0 ? cfgFromDir : cfgFromCli
        const seen = {}
        const all = []
        for (const c of src.concat(configId !== "" ? [configId] : [])) {
            if (!seen[c]) { seen[c] = true; all.push(c) }
        }
        all.sort((a, b) => a.localeCompare(b))
        configs = all
        const from = cfgFromDir.length > 0 ? " (" + cfgDirs + ")" : cfgFromCli.length > 0 ? " (nbfc config -l)" : ""
        const onlyCurrent = cfgFromDir.length === 0 && cfgFromCli.length === 0
        configNote = (all.length > 0 ? all.length + " config" + from : I18n.t("Could not get the list"))
                   + (onlyCurrent ? " · " + I18n.t("only the selected config is known, no config folder found") : "")
                   + (cfgNoteCli !== "" && cfgFromDir.length === 0 ? " · " + cfgNoteCli : "")
    }
    property string cfgDirs: ""
    function loadConfigs() {
        // stdout + stderr together; some versions print the list to stderr or in a different format
        enqueue("cfg:list", ["sh", "-c", "nbfc config -l 2>&1"], (code, out) => {
            const ls = root.lines(out).filter(l => !/^(usage|error|warning)/i.test(l))
            console.log("[ccenter] nbfc config -l (exit " + code + "): " + ls.length + " configs available; selected: " + (root.configId || "-"))
            root.cfgFromCli = code === 0 ? ls : []
            root.cfgNoteCli = code === 0 ? (ls.length === 0 ? I18n.t("nbfc config -l returned nothing") : "")
                                         : I18n.t("nbfc config -l failed (code %1): %2").arg(code).arg(root.firstLine(out))
            root.mergeConfigs()
        })
        // The folders NBFC itself loads configs from (src/nbfc.h): the mutable /var/lib one and DATADIR
        // (/usr/share, or /usr/local/share for a /usr/local build). Line format: folder|filename
        enqueue("cfg:dir", ["sh", "-c",
            'for d in /var/lib/nbfc/configs /usr/share/nbfc/configs /usr/local/share/nbfc/configs; do ' +
            '[ -d "$d" ] && find "$d" -maxdepth 1 -name "*.json" -printf "$d|%f\\n"; done'], (code, out) => {
            const names = [], dirs = []
            for (const l of root.lines(out)) {
                const i = l.indexOf("|")
                if (i < 0) continue
                const d = l.slice(0, i)
                if (dirs.indexOf(d) < 0) dirs.push(d)
                names.push(l.slice(i + 1).replace(/\.json$/, ""))
            }
            root.cfgFromDir = names
            root.cfgDirs = dirs.join(", ")
            root.mergeConfigs()
        })
    }
    // The selected config's threshold table per fan, as our curve format (the config file is only read).
    // [{ points: [{t, s}], hyst } | null]  - null: the config defines no threshold table
    property var configCurves: []
    function loadConfigCurves() {
        const id = configId
        if (id === "") { configCurves = []; return }
        // Same lookup order as NBFC (ModelConfig_FindAndLoad): the id as a path (NBFC runs in "/"), then /var/lib,
        // then DATADIR, each with and without ".json", so the preview shows the file NBFC really uses.
        enqueue("cfg:curves", ["sh", "-c",
            'cd / || exit 1; [ -f "$1" ] && exec cat "$1"; ' +
            'for d in /var/lib/nbfc/configs /usr/share/nbfc/configs /usr/local/share/nbfc/configs; do ' +
            'for f in "$d/$1" "$d/$1.json"; do [ -f "$f" ] && exec cat "$f"; done; done; exit 1', "sh", id], (code, out) => {
            if (id !== root.configId) return
            try {
                root.configCurves = code === 0 ? (JSON.parse(out).FanConfigurations || []).map(f => root.toCurve(f.TemperatureThresholds)) : []
            } catch (e) {
                root.configCurves = []
            }
        })
    }
    // NBFC: go to FanSpeed when the temperature reaches UpThreshold, back below DownThreshold = a stepped curve
    function toCurve(th) {
        if (!Array.isArray(th) || th.length === 0) return null
        const rows = th.map(r => ({ up: Number(r.UpThreshold), down: Number(r.DownThreshold), s: Number(r.FanSpeed) }))
                       .filter(r => isFinite(r.up) && isFinite(r.s)).sort((a, b) => a.up - b.up)
        const pts = []
        for (const r of rows) {
            let t = Math.max(30, Math.min(100, Math.round(r.up)))     // editor range 30..100
            if (pts.length && t <= pts[pts.length - 1].t) t = pts[pts.length - 1].t + 1
            if (t > 100) break
            pts.push({ t: t, s: Math.max(0, Math.min(100, Math.round(r.s))) })
        }
        if (pts.length < 2) return null
        const gaps = rows.filter(r => r.up > 0 && isFinite(r.down)).map(r => r.up - r.down)
        const hyst = gaps.length ? Math.max(0, Math.min(10, Math.round(gaps.reduce((a, b) => a + b, 0) / gaps.length))) : 3
        return { points: pts, hyst: hyst }
    }

    function recommend() {
        enqueue("cfg:rec", ["nbfc", "config", "-r"], (code, out) => {
            if (code !== 0) return
            const known = {}
            for (const c of root.configs) known[c] = true
            const found = []
            for (const l of root.lines(out)) {
                for (const c of [l, l.split(/\s{2,}|\t/)[0].trim()]) {
                    if (known[c] && found.indexOf(c) < 0) { found.push(c); break }
                }
            }
            root.recommended = found
            if (found.length === 0) { root.message = I18n.t("No recommendation found"); root.messageError = false }
        })
    }
    function applyConfig(name) {
        handBackThen(["sh", "-c", 'nbfc config -s "$1" && nbfc restart $2', "sh", name, readOnly ? "-r" : ""])
    }

    // ---------- reading the status ----------
    function isTrue(v) { return /^(true|yes|on|enabled|1)/i.test(v.trim()) }

    function parseStatus(txt) {
        const res = { readOnly: null, temp: NaN, fans: [] }
        let fan = null
        const fresh = name => {
            const f = { name: name, temp: NaN, auto: true, critical: false, current: 0, target: 0, curSeen: false }
            res.fans.push(f)
            return f
        }
        for (const line of txt.split("\n")) {
            const i = line.indexOf(":")
            if (i < 0) continue
            const k = line.slice(0, i).trim().toLowerCase()
            const v = line.slice(i + 1).trim()
            if (k.indexOf("fan") >= 0 && k.indexOf("name") >= 0) { fan = fresh(v); continue }
            if (k.indexOf("read") >= 0 && k.indexOf("only") >= 0) { res.readOnly = root.isTrue(v); continue }
            if (k.indexOf("temp") >= 0) {
                const n = parseFloat(v)
                if (fan) fan.temp = n; else res.temp = n
                continue
            }
            if (k.indexOf("current") >= 0 && k.indexOf("speed") >= 0) {
                if (!fan || fan.curSeen) fan = fresh("Fan " + (res.fans.length + 1))
                fan.current = parseFloat(v) || 0
                fan.curSeen = true
                continue
            }
            if (!fan) continue
            if (k.indexOf("target") >= 0 && k.indexOf("speed") >= 0) fan.target = parseFloat(v) || 0
            else if (k.indexOf("critical") >= 0) fan.critical = root.isTrue(v)      // "Critical Mode Enabled": NBFC runs it at 100%
            else if (k.indexOf("auto") >= 0) fan.auto = root.isTrue(v)
        }
        return res
    }

    function applyStatus(res) {
        if (res.readOnly !== null) readOnly = res.readOnly
        const ft = res.fans.map(f => f.temp).filter(t => isFinite(t))
        temp = isFinite(res.temp) ? res.temp : (ft.length ? Math.max.apply(null, ft) : NaN)
        fans = res.fans
    }

    function poll() {
        if (statusCmd.busy) return
        root.pollSeq++
        const seq = root.pollSeq
        statusCmd.go(["nbfc", "status", "-a"], (code, out, err) => {
            if (!root.logged) {
                root.logged = true
                console.log("[ccenter] nbfc status -a (exit " + code + "):\n" + out + err)
            }
            root.statusSeq = seq
            const was = root.running
            root.running = code === 0
            // A running service went away: not reported while an admin action (restart, config) explains it, but
            // afterwards if it didn't come back and the user didn't stop it
            if (was && !root.running) root.lostPending = true
            if (root.running) root.lostPending = false
            if (root.lostPending && !root.userStopped && !root.quitting && !root.adminQuiet()) {
                root.lostPending = false
                root.serviceLost()
            }
            if (code === 0) {
                // Only a service that came back clears "stopped by the user" (a read started before the stop
                // finished can still succeed afterwards)
                if (!was) root.userStopped = false
                root.applyStatus(root.parseStatus(out))
                root.checkBoost()
                root.control()
                root.record()
            } else {
                root.temp = NaN
            }
        })
    }

    // ---------- fan control ----------
    // With a fan in Fixed/Curve mode, sensors are read even while the window is closed (the safety limit watches the GPU too)
    readonly property bool needsSensors: Object.keys(targets).length > 0
    // Is Ccenter driving fans (Fixed/Curve/max fan)? Then poll often
    readonly property bool controlling: needsSensors || boosting
    property bool uiVisible: true             // is the window open (bound in shell.qml); slower polling while hidden

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
    // The safety limit always looks at the hottest one
    function hottest(i) { return sourceTemp(i, "max") }

    // st.curve = [{ t: °C, s: % }] (sorted by temperature), st.smooth = straight lines between points (else steps)
    function curveAt(st, T) {
        const p = st.curve
        if (!p || p.length === 0) return NaN
        if (T <= p[0].t) return p[0].s
        for (let i = 1; i < p.length; i++) {
            if (T < p[i].t) {
                if (!st.smooth) return p[i - 1].s
                return p[i - 1].s + (p[i].s - p[i - 1].s) * (T - p[i - 1].t) / (p[i].t - p[i - 1].t)
            }
        }
        return p[p.length - 1].s
    }

    // Current level of a fan in Curve mode (%): hysteresis and the gradual slow-down work on this
    property var level: ({})
    readonly property int rampDown: 5        // slows down by at most this much per control round (2 s)

    // Speed up immediately; to slow down the temperature must drop st.hyst °C below the curve, then step down gradually
    function curveLevel(i, st, T) {
        const up = curveAt(st, T)
        const prev = level[i]
        if (prev === undefined || !isFinite(prev) || up >= prev) return up
        const down = curveAt(st, T + st.hyst)
        const target = Math.min(prev, Math.max(up, down))
        return Math.max(target, prev - rampDown)
    }

    // idx = fan index, -1 = all fans. st = { mode: 0 auto | 1 fixed | 2 curve, fixed: 0..1, curve: [{t,s}], smooth }
    function applyTarget(idx, st) {
        const ids = idx < 0 ? fans.map((f, i) => i) : [idx]
        const t = Object.assign({}, targets)
        for (const i of ids) {
            lastSent[i] = undefined
            sentSeq[i] = 0
            level[i] = undefined                    // setting changed: the new curve applies immediately
            if (st.mode === 0) { delete t[i]; markReleasing(i) }
            else { t[i] = st; delete releasing[i] }
        }
        targets = t
        if (boosting) return                         // while max fan runs, each fan returns to its own setting when it ends
        if (st.mode === 0) {
            enqueue(idx < 0 ? "auto:all" : "auto:" + idx,
                    idx < 0 ? ["nbfc", "set", "-a"] : ["nbfc", "set", "-f", String(idx), "-a"], null)
        } else {
            control()
        }
    }

    // A fan Ccenter handed back to NBFC's automatic control: confirmed by a status read started after it was asked,
    // otherwise asked again (every 4 s, at most 5 times, then reported)
    function markReleasing(i) { releasing[i] = { at: Date.now(), seq: pollSeq, tries: 0 } }
    function checkRelease(i, now) {
        const r = releasing[i]
        if (!r) return
        if (fans[i].auto && statusSeq > r.seq) { delete releasing[i]; return }
        const age = now - r.at
        if (age >= 0 && age < 4000) return                // (a clock jump back counts as time up)
        if (r.tries >= 5) {
            if (adminQuiet()) return                      // an admin action may explain it: wait until it's over
            delete releasing[i]
            commandFailed(I18n.t("Fan %1 could not be handed back to NBFC's automatic control").arg(i + 1))
            return
        }
        r.at = now
        r.seq = pollSeq
        r.tries++
        enqueue("auto:" + i, ["nbfc", "set", "-f", String(i), "-a"], null)
    }

    // Runs after every status read: writes speeds to fans in Fixed/Curve mode
    function control() {
        if (!root.running || readOnly || quitting || holding) return
        const now = Date.now()
        for (let i = 0; i < fans.length; i++) {
            const f = fans[i]
            // NBFC went back to auto or holds another speed than the one we sent (changed outside Ccenter, an old one
            // restored): write it again. Only judged from a status read started after NBFC took our command; not in
            // critical mode, where NBFC itself runs the fan at 100%.
            if (lastSent[i] !== undefined && statusSeq > (sentSeq[i] ?? 0)
                    && (f.auto || (!f.critical && Math.abs(f.target - lastSent[i]) >= 1))) lastSent[i] = undefined
            if (boosting) {                          // max fan: all fans at 100%
                if (lastSent[i] !== 100) { lastSent[i] = 100; send(i, 100) }
                continue
            }
            const t = targets[i]
            if (!t) { checkRelease(i, now); continue }
            const T = sourceTemp(i, t.src)
            const H = hottest(i)
            let pct
            if (isFinite(H) && H >= safety) { pct = 100; level[i] = 100 }   // safety comes first
            else if (t.mode === 1) { pct = t.fixed * 100; level[i] = undefined }
            else if (isFinite(T)) { pct = curveLevel(i, t, T); level[i] = pct }
            else continue
            if (!isFinite(pct)) continue
            pct = Math.max(0, Math.min(100, Math.round(pct)))
            const last = lastSent[i]
            if (last === undefined || Math.abs(pct - last) >= 2 || (pct !== last && (pct === 0 || pct === 100))) {
                lastSent[i] = pct
                send(i, pct)
            }
        }
    }
    property double lastFailNote: 0
    function send(i, pct) {
        sentSeq[i] = Infinity
        enqueue("spd:" + i, ["nbfc", "set", "-f", String(i), "-s", String(pct)], (code, out, err) => {
            if (code === 0) {
                if (root.lastSent[i] === pct) root.sentSeq[i] = root.pollSeq   // (a newer speed may already be on the way)
                return
            }
            root.lastSent[i] = undefined             // retried on the next round
            root.sentSeq[i] = 0
            if (!root.adminQuiet())
                root.commandFailed(I18n.t("Could not set the speed of fan %1: %2").arg(i + 1).arg(root.firstLine(err !== "" ? err : out)))
        })
    }

    // ---------- max fan ----------
    // boostUntil: 0 off, -1 until turned off, >0 end time (ms)
    property double boostUntil: 0
    readonly property bool boosting: boostUntil !== 0
    function setBoost(minutes) {
        if (minutes > 0) boostUntil = Date.now() + minutes * 60000
        else if (minutes < 0) boostUntil = -1
        else if (boosting) {
            boostUntil = 0
            // each fan back to its own setting: fans without one go to NBFC auto, Fixed/Curve ones are recomputed
            for (let i = 0; i < fans.length; i++) {
                lastSent[i] = undefined
                level[i] = undefined
                if (!targets[i]) {
                    markReleasing(i)
                    enqueue("auto:" + i, ["nbfc", "set", "-f", String(i), "-a"], null)
                }
            }
        }
        control()
    }
    function checkBoost() {
        if (boostUntil > 0 && Date.now() >= boostUntil) { setBoost(0); boostEnded() }
    }

    // ---------- history (last 10 minutes, one sample per status read) ----------
    property var history: []                 // [{ t: ms, cpu: °C, gpu: °C|NaN, fan: % (average) }]
    readonly property int historyMs: 600000
    function record() {
        const now = Date.now()
        const fanAvg = fans.length ? fans.reduce((a, f) => a + f.current, 0) / fans.length : NaN
        const h = history.filter(x => now - x.t <= historyMs)
        h.push({ t: now, cpu: temp, gpu: Sensors.gpu, fan: fanAvg })
        history = h
    }

    // Quit completely: first hand the fans we control back to NBFC's automatic control
    property bool quitting: false
    function releaseAndQuit() {
        if (quitting) return
        quitting = true
        // Keyboard effect back to the static colour first (so it isn't left dark mid-fade), then release the fans
        Keyboard.restoreForQuit(() => root.releaseFansAndQuit())
    }
    function releaseFansAndQuit() {
        Settings.flush()                      // don't lose a pending settings change
        const owned = Object.keys(targets).length > 0 || boosting
        targets = ({})
        boostUntil = 0
        if (owned && running) enqueue("quit", ["nbfc", "set", "-a"], () => Qt.quit())
        else if (owned) warnSavedSpeedsThenQuit()
        else Qt.quit()
    }
    // NBFC isn't running, so the fans can't be handed back now. If it saved fixed speeds when it stopped, it sets them
    // again at its next start, with nothing watching the temperature but the config's critical limit: say so.
    function warnSavedSpeedsThenQuit() {
        enqueue("quit", ["cat", "/var/lib/nbfc/state.json"], (code, out) => {
            let fixed = false
            try { fixed = code === 0 && (JSON.parse(out).TargetFanSpeeds || []).some(v => Number(v) >= 0) } catch (e) {}
            if (fixed) {
                console.log("[ccenter] NBFC is stopped with fixed fan speeds saved in /var/lib/nbfc/state.json")
                Notify.send(I18n.t("Fans may stay at a fixed speed"),
                            I18n.t("NBFC is stopped and has saved fixed fan speeds; it sets them again when it starts. After starting it, run `nbfc set -a` or open Ccenter."),
                            "normal", "dialog-warning")
            }
            Qt.quit()
        })
    }

    // Another config: the old one's settings don't belong to these fans. Fans Ccenter drove go back to NBFC's automatic
    // control (followed up until NBFC reports it: a restart right after the switch can swallow the first command, and
    // the new config then gets the old speeds by fan number); FanState applies the new config's saved settings on the
    // next status read, which takes those fans over again.
    function forgetFans() {
        const owned = Object.keys(targets).concat(Object.keys(releasing))
        targets = ({})
        lastSent = ({})
        sentSeq = ({})
        level = ({})
        releasing = ({})
        for (const k of owned) markReleasing(Number(k))
        if (running && !boosting) for (const k of owned) enqueue("auto:" + k, ["nbfc", "set", "-f", k, "-a"], null)
    }

    // ---------- plumbing ----------
    Cmd { id: runner; timeout: 15000 }
    Cmd { id: statusCmd; timeout: 10000 }
    Cmd { id: adminRunner }                   // no time limit: it waits for the password dialog
    // 2 s while the window is open or Ccenter drives fans; 5 s when hidden and everything is on NBFC
    Timer { interval: root.uiVisible || root.controlling ? 2000 : 5000; running: true; repeat: true; triggeredOnStart: true; onTriggered: root.poll() }

    FileView {
        path: "/etc/nbfc/nbfc.json"
        watchChanges: true
        onFileChanged: reload()
        // A half-written file (empty while NBFC rewrites it) keeps the current id instead of switching configs
        onLoaded: {
            try { root.configId = JSON.parse(text()).SelectedConfigId || "" }
            catch (e) { console.log("[ccenter] /etc/nbfc/nbfc.json could not be read: " + e) }
        }
    }

    onConfigIdChanged: { forgetFans(); mergeConfigs(); loadConfigCurves() }
    Component.onCompleted: { loadConfigs(); refreshBoot() }
}
