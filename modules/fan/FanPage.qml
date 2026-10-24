import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.services
import qs.components

// View only: fan settings, profiles and restoring live in services/FanState.qml.
// The temperatures stay pinned at the top; everything below them scrolls.
PageBase {
    id: page
    title: I18n.t("Fan control")
    sub: Nbfc.configId !== "" ? Nbfc.configId : ""

    pinnedContent: TempCard {}

    // NBFC isn't running: say so first, with the way to start it
    Group {
        visible: !Nbfc.running
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
        Layout.fillWidth: true
        spacing: Tokens.spacing.largeIncreased
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

        ProfileCard { visible: Nbfc.fans.length > 0 }

        GridLayout {
            id: fansGrid
            Layout.fillWidth: true
            // side by side only when each fan still gets room for its curve
            columns: Math.max(1, Math.min(Nbfc.fans.length, fansGrid.width > 1000 ? 2 : 1))
            columnSpacing: Tokens.spacing.large
            rowSpacing: Tokens.spacing.largeIncreased
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
            visible: Nbfc.fans.length > 0
            isGlobal: true
            name: "Global"
            subtitle: I18n.t("When on, the same settings are written to every fan")
        }
    }

    HistoryCard {}
    ServiceCard {}
    ConfigCard {}
}
