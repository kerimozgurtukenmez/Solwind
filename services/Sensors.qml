pragma Singleton
import Quickshell
import QtQuick
import Solwind

// CPU / GPU / iGPU temperatures and fan speeds, read by solwindd (hwmon, nvidia-smi for NVIDIA; while the fan page is
// open, Solwind drives fans or notifications are on). This mirrors its /solwind/Sensors.
Singleton {
    id: root

    readonly property var s: Solwind.sensors
    readonly property bool up: Solwind.connected === true

    // The fan page is open (bound in shell.qml): the daemon reads the sensors meanwhile
    property bool active: false
    onActiveChanged: Solwind.watch("fans", active)
    Component.onCompleted: Solwind.watch("fans", active)

    // [{ label, model, extra, value (°C or NaN), note }] for the temperature tiles
    readonly property var readings: up && s.Readings ? Array.from(s.Readings).map(r => ({
        label: r.label, model: r.model, extra: r.threads !== "" ? I18n.t("%1 threads").arg(r.threads) : "", value: r.value,
        note: r.note === "asleep" ? I18n.t("Asleep") : r.note === "error" ? I18n.t("Read error") : r.note === "nosensor" ? I18n.t("No sensor") : ""
    })) : []
    // For fan control: discrete GPU temperature (NaN when asleep / absent)
    readonly property real gpu: up && typeof s.Gpu === "number" ? s.Gpu : NaN
    readonly property real cpu: up && typeof s.Cpu === "number" ? s.Cpu : NaN
    readonly property bool hasGpu: up && s.HasGpu === true
    // Fan i's speed (RPM), NaN unless the driver's fans can be paired with NBFC's (the daemon decides)
    function rpm(i) { const x = Nbfc.fans[i]; return x && typeof x.rpm === "number" ? x.rpm : NaN }
}
