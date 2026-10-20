pragma Singleton
import Quickshell
import QtQuick

// Watches for events and sends desktop notifications. The same event never fires twice in a row.
Singleton {
    id: root

    // Critical temperature: notify once when the safety limit is reached; re-arm after cooling down by 5°C
    property bool armed: true
    readonly property real hot: {
        const c = Nbfc.temp, g = Sensors.gpu
        return isFinite(g) ? (isFinite(c) ? Math.max(c, g) : g) : c
    }
    onHotChanged: {
        if (!isFinite(hot)) return
        if (armed && hot >= Nbfc.safety) {
            armed = false
            const who = isFinite(Sensors.gpu) && Sensors.gpu >= Nbfc.temp ? "GPU" : "CPU"
            const owned = Object.keys(Nbfc.targets).length > 0
            Notify.send(I18n.t("Critical temperature: %1°C (%2)").arg(Math.round(hot)).arg(who),
                        owned ? I18n.t("Safety limit (%1°C) exceeded; fans driven by Solwind were set to 100%.").arg(Nbfc.safety)
                              : I18n.t("Safety limit (%1°C) exceeded; the fans are under NBFC control.").arg(Nbfc.safety),
                        "critical", "dialog-warning")
        } else if (!armed && hot < Nbfc.safety - 5) {
            armed = true
        }
    }

    property double lastFail: 0
    Connections {
        target: Nbfc
        function onServiceLost() {
            Notify.send(I18n.t("NBFC service stopped"), I18n.t("Fan control is not possible. Restart it from Solwind or with `sudo nbfc start`."),
                        "critical", "dialog-error")
        }
        function onCommandFailed(msg) {
            const now = Date.now()
            if (now - root.lastFail < 300000) return          // at most once every 5 minutes
            root.lastFail = now
            Notify.send(I18n.t("Could not set the fan speed"), msg, "critical", "dialog-error")
        }
        function onBoostEnded() {
            Notify.send(I18n.t("Max fan ended"), I18n.t("The fans went back to their previous settings."), "low", "dialog-information")
        }
    }
}
