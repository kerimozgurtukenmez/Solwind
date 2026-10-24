import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.components
import qs.services

// The NBFC service: state with start/stop/restart, start on boot, read-only mode, the safety limit
Group {
    id: svc
    title: I18n.t("NBFC service")

    GroupItem {
        RowLayout {
            Layout.fillWidth: true
            spacing: Tokens.spacing.medium
            Rectangle {
                implicitWidth: 10; implicitHeight: 10; radius: 5
                color: Nbfc.running ? Colours.success : Colours.outlineStrong
                Behavior on color { CAnim {} }
            }
            StyledText {
                Layout.fillWidth: true
                text: Nbfc.running ? (Nbfc.readOnly ? I18n.t("Running (read-only)") : I18n.t("Running")) : I18n.t("Stopped")
                color: Colours.fg
                font: Tokens.font.title.small
                elide: Text.ElideRight
            }
            Btn {
                filled: !Nbfc.running
                icon: Nbfc.running ? "stop" : "play_arrow"
                text: Nbfc.running ? I18n.t("Stop") : I18n.t("Start")
                onClicked: Nbfc.running ? Nbfc.stopService() : Nbfc.startService()
            }
            Btn {
                icon: "restart_alt"; text: I18n.t("Restart")
                enabled: Nbfc.running
                onClicked: Nbfc.restartService(Nbfc.readOnly)
            }
        }
        StyledText {
            Layout.fillWidth: true
            visible: Nbfc.message !== ""
            text: Nbfc.message
            wrapMode: Text.WordWrap
            color: Nbfc.messageError ? Colours.error : Colours.fgDim
            font: Tokens.font.label.medium
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
        icon: "local_fire_department"
        label: I18n.t("Safety limit (Fixed/Curve fans go to 100% above it)")
        value: (Nbfc.safety - 70) / 30
        readout: Math.round(Nbfc.safety) + "°C"
        onMoved: v => { Nbfc.safety = Math.round(70 + v * 30); Settings.setSafety(Nbfc.safety) }
    }
}
