import QtQuick
import QtQuick.Layouts
import qs.components
import qs.services

// Notifications: critical temperature, service stopped, fan command failed, max fan ended, profile changed by keybind
Card {
    title: I18n.t("Notifications")

    ToggleRow {
        icon: "notifications"
        label: I18n.t("Notifications")
        sub: I18n.t("Critical temperature, NBFC service stopped, fan speed could not be set, max fan ended")
        controlled: true
        checked: Notify.enabled
        onToggled: v => Settings.setNotify("enabled", v)
    }
    ToggleRow {
        icon: "tune"
        label: I18n.t("Notify on profile change")
        sub: I18n.t("When the profile is changed from the terminal or a keybind")
        controlled: true
        checked: Notify.profiles
        enabled: Notify.enabled
        opacity: enabled ? 1 : 0.4
        onToggled: v => Settings.setNotify("profile", v)
    }
    Btn {
        Layout.fillWidth: true
        icon: "send"
        text: I18n.t("Send a test notification")
        enabled: Notify.enabled
        opacity: enabled ? 1 : 0.4
        onClicked: Notify.send("Solwind", I18n.t("Notifications work."), "normal", "dialog-information")
    }
}
