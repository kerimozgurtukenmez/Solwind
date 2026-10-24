import QtQuick
import QtQuick.Layouts
import qs.components
import qs.services

Card {
    id: svc
    title: I18n.t("NBFC service")

    RowLayout {
        Layout.fillWidth: true
        spacing: 10
        Rectangle { implicitWidth: 10; implicitHeight: 10; radius: 5; color: Nbfc.running ? Colours.success : Colours.outline }
        StyledText {
            text: Nbfc.running ? (Nbfc.readOnly ? I18n.t("Running (read-only)") : I18n.t("Running")) : I18n.t("Stopped")
            color: Colours.fg
            font { pixelSize: 15; bold: true }
        }
        Item { Layout.fillWidth: true }
        StyledText { visible: Nbfc.running && isFinite(Nbfc.temp); text: "thermostat"; color: Colours.primary; font { family: Colours.iconFont; pixelSize: 20 } }
        StyledText { visible: Nbfc.running && isFinite(Nbfc.temp); text: Math.round(Nbfc.temp) + "°C"; color: Colours.fg; font { pixelSize: 15; bold: true } }
    }
    // A fresh NBFC has no config: say why it can't start and where to pick one
    StyledText {
        Layout.fillWidth: true
        visible: Nbfc.noConfig && !Nbfc.running
        text: I18n.t("No NBFC config is selected yet, so NBFC can't start. Pick one below in \"NBFC config\" (the recommend button finds configs that may fit this laptop).")
        wrapMode: Text.WordWrap
        color: Colours.warning
        font.pixelSize: 12
    }
    StyledText {
        Layout.fillWidth: true
        visible: Nbfc.message !== ""
        text: Nbfc.message
        wrapMode: Text.WordWrap
        color: Nbfc.messageError ? Colours.error : Colours.fgDim
        font.pixelSize: 12
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Btn {
            Layout.fillWidth: true; Layout.preferredWidth: 1
            filled: !Nbfc.running
            icon: Nbfc.running ? "stop" : "play_arrow"
            text: Nbfc.running ? I18n.t("Stop") : I18n.t("Start")
            onClicked: Nbfc.running ? Nbfc.stopService() : Nbfc.startService()
        }
        Btn {
            Layout.fillWidth: true; Layout.preferredWidth: 1
            icon: "restart_alt"; text: I18n.t("Restart")
            enabled: Nbfc.running
            opacity: enabled ? 1 : 0.4
            onClicked: Nbfc.restartService(Nbfc.readOnly)
        }
    }
    ToggleRow {
        icon: "rocket_launch"; label: I18n.t("Start on boot"); sub: I18n.t("The service starts by itself when the system boots")
        controlled: true
        checked: Nbfc.bootEnabled
        onToggled: v => Nbfc.setBoot(v)
    }
    ToggleRow {
        icon: "visibility"; label: I18n.t("Read-only mode"); sub: I18n.t("Only reads, never writes fan speeds. For safely trying a new config")
        controlled: true
        checked: Nbfc.readOnly
        onToggled: v => Nbfc.setReadOnly(v)
    }
    SliderRow {
        label: I18n.t("Safety limit (Fixed/Curve fans go to 100% above it)")
        value: (Nbfc.safety - 70) / 30
        readout: Math.round(Nbfc.safety) + "°C"
        onMoved: v => { Nbfc.safety = Math.round(70 + v * 30); Settings.setSafety(Nbfc.safety) }
    }
}
