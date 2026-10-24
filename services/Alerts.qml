pragma Singleton
import Quickshell
import QtQuick

// Watches for events and sends desktop notifications. The same event never fires twice in a row.
Singleton {
    id: root

    // Critical temperature: notify once when the safety limit is reached; re-arm after cooling down by 5°C
    property bool armed: true
    // CPU: NBFC's sensor, or the kernel's while NBFC isn't running (then NBFC reports nothing)
    readonly property real cpu: isFinite(Nbfc.temp) ? Nbfc.temp : Sensors.cpu
    readonly property real hot: {
        const c = cpu, g = Sensors.gpu
        return isFinite(g) ? (isFinite(c) ? Math.max(c, g) : g) : c
    }
    onHotChanged: {
        if (!isFinite(hot)) return
        if (armed && hot >= Nbfc.safety) {
            armed = false
            // Worded a moment later: in the same status read the saved fan settings may still be on their way
            // (FanState restores them right after it), and the text says whether Solwind drives the fans. The
            // temperature and where it came from are the ones that crossed the limit.
            critical.temp = hot
            critical.who = isFinite(Sensors.gpu) && !(Sensors.gpu < cpu) ? "GPU" : "CPU"
            critical.restart()
        } else if (!armed && hot < Nbfc.safety - 5) {
            armed = true
        }
    }
    Timer {
        id: critical
        interval: 300
        property real temp: NaN
        property string who: ""
        onTriggered: {
            const owned = Object.keys(Nbfc.targets).length > 0
            // Say what really happens: nothing is written while NBFC is stopped or read-only
            const what = !Nbfc.running ? I18n.t("Safety limit (%1°C) exceeded; NBFC isn't running, so Solwind can't change the fan speed.")
                       : Nbfc.readOnly ? I18n.t("Safety limit (%1°C) exceeded; NBFC is in read-only mode, so fan speeds can't be set.")
                       : owned ? I18n.t("Safety limit (%1°C) exceeded; fans driven by Solwind were set to 100%.")
                       : I18n.t("Safety limit (%1°C) exceeded; the fans are under NBFC control.")
            Notify.send(I18n.t("Critical temperature: %1°C (%2)").arg(Math.round(temp)).arg(who), what.arg(Nbfc.safety),
                        "critical", "dialog-warning")
        }
    }

    property double lastFail: 0
    property double lastRelease: 0
    Connections {
        target: Nbfc
        function onServiceLost() {
            Notify.send(I18n.t("NBFC service stopped"), I18n.t("Fan control is not possible. Restart it from Solwind or with `sudo nbfc start`."),
                        "critical", "dialog-error")
        }
        function onCommandFailed(msg) {
            const now = Date.now()
            if (now >= root.lastFail && now - root.lastFail < 300000) return     // at most once every 5 minutes
            root.lastFail = now
            Notify.send(I18n.t("Could not set the fan speed"), msg, "critical", "dialog-error")
        }
        // Its own limit: a speed error just before must not hide that a fan was left at a fixed speed
        function onReleaseFailed(msg) {
            const now = Date.now()
            if (now >= root.lastRelease && now - root.lastRelease < 300000) return
            root.lastRelease = now
            Notify.send(I18n.t("Could not hand a fan back to NBFC"), msg, "critical", "dialog-error")
        }
        function onBoostEnded() {
            Notify.send(I18n.t("Max fan ended"), I18n.t("The fans went back to their previous settings."), "low", "dialog-information")
        }
    }
}
