import QtQuick
import QtQuick.Layouts
import qs.components
import qs.services

// Background running: the app keeps running when the window closes; start on login (systemd user service)
Card {
    title: I18n.t("Background")

    ToggleRow {
        icon: "login"
        label: I18n.t("Start in the background on login")
        sub: Autostart.ours ? I18n.t("Starts without a window; your fan settings apply right away")
           : Autostart.installed ? I18n.t("The background service runs another Ccenter copy (%1)").arg(Autostart.serviceDir || "?")
           : I18n.t("Needs an installation first: ./install.sh")
        controlled: true
        checked: Autostart.enabled
        enabled: Autostart.ours && !Autostart.busy
        opacity: enabled ? 1 : 0.5
        onToggled: v => Autostart.setEnabled(v)
    }
    ToggleRow {
        icon: "dock_to_left"
        label: I18n.t("Show in the system tray")
        sub: I18n.t("Icon in the bar: click to show/hide, hover for profiles, max fan, quit")
        controlled: true
        checked: Tray.enabled
        onToggled: v => Settings.setUi("tray", v)
    }
    StyledText {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: I18n.t("Closing the window keeps the app running in the background. To open it again use the app menu or the terminal: ccenter")
        color: Colours.fgDim
        font.pixelSize: 11
    }
    StyledText {
        visible: Autostart.message !== ""
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: Autostart.message
        color: Colours.error
        font.pixelSize: 11
    }
    Btn {
        Layout.fillWidth: true
        icon: "power_settings_new"
        text: I18n.t("Quit (fans go back to NBFC)")
        onClicked: Nbfc.releaseAndQuit()
    }
}
