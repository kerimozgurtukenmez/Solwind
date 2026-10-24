import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import Caelestia.Config
import qs.components
import qs.services

// App settings: language, running in the background, notifications, problem report, about
PageBase {
    title: I18n.t("Settings")

    Group {
        title: I18n.t("Language")
        GroupItem {
            RowLayout {
                Layout.fillWidth: true
                spacing: Tokens.spacing.medium
                MaterialIcon { text: "translate"; color: Colours.fgDim; size: Tokens.font.icon.medium }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    StyledText { Layout.fillWidth: true; text: I18n.t("Interface language"); color: Colours.fg; font: Tokens.font.body.medium }
                    StyledText { Layout.fillWidth: true; text: I18n.t("Also used by the terminal commands, the tray and notifications"); wrapMode: Text.WordWrap; color: Colours.outlineStrong; font: Tokens.font.label.small }
                }
                Segment {
                    Layout.fillWidth: false
                    Layout.preferredWidth: 160
                    model: I18n.languages.map(l => l.name)
                    controlled: true
                    current: I18n.languages.findIndex(l => l.id === I18n.lang)
                    onPicked: i => I18n.setLang(I18n.languages[i].id)
                }
            }
        }
    }

    // Background running: the app keeps running when the window closes; start on login (systemd user service)
    Group {
        title: I18n.t("Background")
        ToggleRow {
            icon: "login"
            label: I18n.t("Start in the background on login")
            sub: Autostart.ours ? I18n.t("Starts without a window; your fan settings apply right away")
               : Autostart.installed ? I18n.t("The background service runs another Solwind copy (%1)").arg(Autostart.serviceDir || "?")
               : I18n.t("Needs an installation first: sudo make install")
            controlled: true
            checked: Autostart.enabled
            enabled: Autostart.ours && !Autostart.busy
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
        ButtonRow {
            icon: "power_settings_new"
            label: I18n.t("Quit (fans go back to NBFC)")
            sub: I18n.t("Closing the window keeps the app running in the background. To open it again use the app menu or the terminal: solwind")
            trailing: ""
            onClicked: Nbfc.releaseAndQuit()
        }
        InfoRow {
            visible: Autostart.message !== ""
            icon: "error"
            label: Autostart.message
            valueColor: Colours.error
        }
    }

    // Notifications: critical temperature, service stopped, fan command failed, max fan ended, profile changed by keybind
    Group {
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
            onToggled: v => Settings.setNotify("profile", v)
        }
        ButtonRow {
            icon: "send"
            label: I18n.t("Send a test notification")
            trailing: ""
            enabled: Notify.enabled
            onClicked: Notify.send("Solwind", I18n.t("Notifications work."), "normal", "dialog-information")
        }
    }

    ReportCard {}

    Group {
        title: I18n.t("About")
        InfoRow { icon: "info"; label: "Solwind"; value: version.text().trim() }
        InfoRow { icon: "folder"; label: I18n.t("App folder"); value: Quickshell.shellPath("").replace(/\/$/, "") }
    }
    FileView { id: version; path: Quickshell.shellPath("VERSION"); blockLoading: true }
}
