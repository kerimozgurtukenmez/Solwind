import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.services
import qs.components

// View only: solwindd keeps the fan settings and profiles and restores them; services/FanState.qml and Nbfc.qml pass
// them on.
// Wide window: the chart on top, the fans below it, NBFC and the profiles side by side at the bottom, the CPU / GPU /
// iGPU tiles in a column on the right. Narrow window: one column, the temperatures pinned at the top.
PageBase {
    id: page
    adaptive: true
    title: wide ? "" : I18n.t("Fan control")
    sub: !wide && Nbfc.configId !== "" ? Nbfc.configId : ""
    showPinned: !wide

    pinnedContent: TempCard {}
    side: TempCard {
        vertical: true
        Layout.preferredWidth: 220
        Layout.fillWidth: false
        Layout.fillHeight: true
    }

    HistoryCard { stepPx: page.wide ? 16 : 22 }

    // No fan plugin installed: the temperatures only (PLUGIN-PLAN.md)
    Group {
        visible: Nbfc.noPlugin
        InfoRow {
            icon: "extension_off"
            label: I18n.t("No fan plugin is installed")
            sub: I18n.t("Solwind shows the temperatures, but can't control the fans without one. Install the one for your fans from the Plugins page, then restart Solwind.")
        }
    }

    // A fan plugin without a service (HP) that can't write now says why
    Group {
        visible: !Nbfc.noPlugin && !Nbfc.hasService && (!Nbfc.running || Nbfc.readOnly)
        InfoRow {
            icon: "error"
            label: I18n.t("Fan speeds can't be changed now")
            sub: Nbfc.boostBlocked()
        }
    }

    // NBFC isn't running: say so first, with the way to start it
    Group {
        visible: Nbfc.hasService && !Nbfc.running
        ButtonRow {
            icon: "error"
            accent: Colours.error
            label: I18n.t("The NBFC service is not running")
            sub: Nbfc.noConfig ? I18n.t("No NBFC config is selected yet, so NBFC can't start. Pick one below in \"NBFC config\" (the recommend button finds configs that may fit this laptop).")
                               : I18n.t("Fan control is not possible. Restart it from Solwind or with `sudo nbfc start`.")
            trailing: Nbfc.noConfig ? "" : "play_arrow"
            enabled: !Nbfc.noConfig
            onClicked: Nbfc.startService()
        }
    }

    // Fan settings are locked while the service is stopped or in read-only mode
    ColumnLayout {
        visible: !Nbfc.noPlugin
        Layout.fillWidth: true
        spacing: Tokens.spacing.large
        enabled: Nbfc.running && !Nbfc.readOnly
        opacity: enabled ? 1 : 0.4
        Behavior on opacity { Anim { type: Anim.DefaultEffects } }

        StyledText {
            visible: Nbfc.fans.length === 0
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: I18n.t("No fan information. The service may not be running or no config is selected.")
            color: Colours.fgDim
            font: Tokens.font.body.small
        }

        Group {
            visible: Nbfc.fans.length > 0
            ToggleRow {
                icon: "mode_fan"
                label: "Global"
                sub: I18n.t("When on, the same settings are written to every fan")
                controlled: true
                checked: FanState.globalOn
                onToggled: v => FanState.setGlobalOn(v)
            }
        }

        // Global on: one card for all the fans. Off: one card per fan.
        GridLayout {
            id: fansGrid
            visible: !FanState.globalOn
            Layout.fillWidth: true
            // side by side only when each fan still gets room for its curve
            columns: Math.max(1, Math.min(Nbfc.fans.length, fansGrid.width >= 560 ? 2 : 1))
            columnSpacing: Tokens.spacing.large
            rowSpacing: Tokens.spacing.large
            Repeater {
                model: Nbfc.fans.length
                FanCard {
                    Layout.alignment: Qt.AlignTop
                    fanIndex: index
                    name: I18n.t("Fan %1").arg(index + 1)
                    subtitle: info ? info.name : ""
                    info: Nbfc.fans[index]
                    active: !FanState.globalOn
                }
            }
        }
        FanCard {
            visible: Nbfc.fans.length > 0 && FanState.globalOn
            isGlobal: true
            name: "Global"
            subtitle: I18n.t("When on, the same settings are written to every fan")
        }
    }

    // NBFC's settings only exist with its plugin (Nbfc.hasService)
    GridLayout {
        Layout.fillWidth: true
        columns: page.wide && Nbfc.hasService ? 2 : 1
        columnSpacing: Tokens.spacing.large
        rowSpacing: Tokens.spacing.largeIncreased
        ColumnLayout {
            visible: Nbfc.hasService
            Layout.alignment: Qt.AlignTop
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            spacing: Tokens.spacing.largeIncreased
            ServiceCard {}
            ConfigCard {}
        }
        ProfileCard {
            visible: Nbfc.fans.length > 0
            Layout.alignment: Qt.AlignTop
            enabled: Nbfc.running && !Nbfc.readOnly
            opacity: enabled ? 1 : 0.4
        }
    }
}
