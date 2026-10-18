pragma Singleton
import Quickshell
import Quickshell.Io
import QtQuick
import qs.utils

// CPU / GPU / iGPU temperatures (hwmon + nvidia-smi for NVIDIA). Read only while needed (see shell.qml).
Singleton {
    id: root
    property bool active: true
    // fast: window open or Ccenter driving fans -> every 3 s, GPU every round.
    // Otherwise every 5 s and the GPU (nvidia-smi, expensive) every 4th round (~20 s).
    property bool fast: true
    property int tick: 0
    property real lastNv: NaN
    property string lastNvNote: ""
    // nvidia-smi failures (e.g. after a driver update without a reboot every call fails and the kernel
    // logs "API mismatch"): from the 2nd failure in a row wait 30 s, 1 min, 2 min ... up to 10 min between
    // tries (one failure, e.g. while the card wakes up, is just retried); reset on success.
    property int nvFails: 0
    property double nvRetryAt: 0
    property var readings: []     // [{ label, model, value (°C or NaN), note }]
    property var hw: ({ cpu: "", cores: "", gpu: "", igpu: "" })
    // For fan control: discrete GPU temperature (NaN when asleep / absent)
    readonly property real gpu: { const r = readings.find(x => x.label === "GPU"); return r ? r.value : NaN }
    readonly property bool hasGpu: readings.some(x => x.label === "GPU")
    // Fan RPM: trusted once the driver has reported a value above 0 (some drivers always return 0)
    property var rpms: []                    // in fan order (fan1 -> 0)
    property bool rpmWorks: false
    function rpm(i) { return rpmWorks && i < rpms.length ? rpms[i] : NaN }

    // "13th Gen Intel(R) Core(TM) i7-13700H" -> "Intel Core i7-13700H"
    function cleanCpu(n) {
        return n.replace(/\((R|TM|tm|r)\)/g, "").replace(/^\d+(st|nd|rd|th) Gen\s+/i, "")
                .replace(/\s+(CPU|Processor)\b.*$/i, "").replace(/\s+@.*$/, "").replace(/\s+/g, " ").trim()
    }
    // "Raptor Lake-P [Iris Xe Graphics]" -> "Iris Xe Graphics"; "NVIDIA GeForce RTX 3050 6GB Laptop GPU" -> "GeForce RTX 3050 6GB Laptop GPU"
    function cleanGpu(n) {
        const b = n.match(/\[([^\]]+)\]/)
        return (b ? b[1] : n).replace(/^NVIDIA\s+/, "").trim()
    }
    function parseHw(txt) {
        const h = { cpu: "", cores: "", gpu: "", igpu: "" }
        const gpus = []
        for (const line of txt.split("\n")) {
            const p = line.split("|")
            if (p[0] === "cpu" && p.length >= 3) { h.cpu = cleanCpu(p[1]); h.cores = p[2] }
            else if (p[0] === "gpu" && p.length >= 4) gpus.push({ vendor: p[1], name: cleanGpu(p.slice(3).join("|")) })
        }
        const dgpu = gpus.find(g => g.vendor === "0x10de")
        const igpu = gpus.find(g => g.vendor === "0x8086") || (dgpu ? gpus.find(g => g.vendor === "0x1002") : null)
        const other = gpus.find(g => g !== dgpu && g !== igpu)
        h.gpu = dgpu ? dgpu.name : (other ? other.name : "")
        h.igpu = igpu ? igpu.name : ""
        hw = h
    }

    function parse(txt) {
        let pkg = NaN, coreMax = NaN, amdCpu = NaN, acpi = NaN
        let nv = NaN, nvNote = "", hasNv = false, nvFailed = false, amdGpu = NaN, igpu = NaN
        let fanDrv = "", fanRpm = []
        for (const line of txt.split("\n")) {
            const p = line.split("|")
            if (p[0] === "fan" && p.length === 4) {
                if (fanDrv === "") fanDrv = p[1]          // first fan driver (hp, thinkpad, dell_smm, ...)
                if (p[1] === fanDrv) fanRpm[parseInt(p[2]) - 1] = parseInt(p[3])
                continue
            }
            if (p.length !== 3) continue
            const n = p[0], l = p[1].toLowerCase(), raw = p[2]
            if (n === "nvidia" || n === "nouveau") {
                hasNv = true
                // not read this round: last value (while backing off: the error, not an older "asleep")
                if (raw === "skip") { nv = lastNv; nvNote = nvFails > 0 ? I18n.t("Read error") : lastNvNote }
                else if (raw === "asleep") nvNote = I18n.t("Asleep")
                else if (raw === "fail") { nvFailed = true; nvNote = I18n.t("Read error") }
                else nv = Math.max(isFinite(nv) ? nv : -1, parseInt(raw) / 1000)
                continue
            }
            const v = parseInt(raw) / 1000
            if (!isFinite(v)) continue
            if (n === "coretemp") {
                if (l.indexOf("package") >= 0) pkg = v
                else coreMax = isFinite(coreMax) ? Math.max(coreMax, v) : v
            } else if (n === "k10temp" || n === "zenpower") {
                if (!isFinite(amdCpu) || l.indexOf("tctl") >= 0 || l.indexOf("tdie") >= 0) amdCpu = v
            } else if (n === "acpitz") {
                if (!isFinite(acpi)) acpi = v
            } else if (n === "i915" || n === "xe") {
                igpu = v
            } else if (n === "amdgpu") {
                if (!isFinite(amdGpu) || l.indexOf("edge") >= 0) amdGpu = v
            }
        }
        const cpu = isFinite(pkg) ? pkg : (isFinite(amdCpu) ? amdCpu : (isFinite(coreMax) ? coreMax : acpi))
        const out = [{ label: "CPU", model: hw.cpu, extra: hw.cores !== "" ? I18n.t("%1 threads").arg(hw.cores) : "", value: cpu, note: "" }]
        let gpu = nv
        if (isFinite(amdGpu)) { if (hasNv) igpu = amdGpu; else gpu = amdGpu }
        if (hasNv || isFinite(gpu) || hw.gpu !== "") out.push({ label: "GPU", model: hw.gpu, extra: "", value: gpu, note: nvNote })
        if (isFinite(igpu) || hw.igpu !== "")
            out.push({ label: "iGPU", model: hw.igpu, extra: "", value: igpu, note: isFinite(igpu) ? "" : I18n.t("No sensor") })
        if (hasNv) { lastNv = nv; lastNvNote = nvNote }   // used on rounds where the GPU is skipped
        if (nvFailed && !isFinite(nv)) {
            nvFails++
            const wait = nvFails < 2 ? 0 : Math.min(600000, 30000 * Math.pow(2, nvFails - 2))
            nvRetryAt = Date.now() + wait
            if (nvFails === 2 || wait === 600000 && nvFails % 6 === 0)
                console.log("[ccenter] nvidia-smi gave no temperature (failures: " + nvFails + "), next try in "
                            + Math.round(wait / 1000) + " s. After an NVIDIA driver update a reboot usually fixes this.")
        } else if (isFinite(nv) && nvFails > 0) {
            console.log("[ccenter] nvidia-smi works again")
            nvFails = 0
            nvRetryAt = 0
        }
        readings = out
        rpms = Array.from(fanRpm, v => isFinite(v) ? v : NaN)
        if (rpms.some(v => v > 0)) rpmWorks = true
    }

    function poll() {
        if (cmd.busy) return
        const now = Date.now()
        if (nvRetryAt - now > 600000) nvRetryAt = now + 600000      // the clock went back: never wait more than the cap
        const gpu = (fast || tick % 4 === 0) && now >= nvRetryAt
        tick++
        cmd.go(["sh", Quickshell.shellPath("scripts/sensors.sh")].concat(gpu ? ["gpu"] : []), (code, out) => root.parse(out))
    }

    Cmd { id: cmd }
    Cmd { id: hwCmd }
    Component.onCompleted: hwCmd.go(["sh", Quickshell.shellPath("scripts/hwinfo.sh")], (code, out) => root.parseHw(out))
    Timer { interval: root.fast ? 3000 : 5000; running: root.active; repeat: true; triggeredOnStart: true; onTriggered: root.poll() }
}
