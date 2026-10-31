import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.components
import qs.services

// A row of tiles, or with `vertical` a column of them (one per chip) that fills the height it is given
Card {
    id: card
    property bool vertical: false
    title: vertical ? "" : I18n.t("Hardware and temperatures")
    pad: vertical ? 0 : Tokens.padding.largeIncreased
    fillHeight: vertical

    GridLayout {
        Layout.fillWidth: true
        Layout.fillHeight: card.vertical
        columns: card.vertical ? 1 : Math.max(1, Sensors.readings.length)
        rowSpacing: Tokens.spacing.small
        columnSpacing: Tokens.spacing.small
        Repeater {
            model: Sensors.readings.length
            Rectangle {
                readonly property var r: Sensors.readings[index]
                readonly property bool known: r && isFinite(r.value)
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                implicitHeight: info.implicitHeight + Tokens.padding.large * 2
                radius: Tokens.rounding.large
                color: Colours.surfaceHigh

                ColumnLayout {
                    id: info
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: Tokens.padding.large }
                    spacing: Tokens.spacing.extraSmall / 2
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Tokens.spacing.small
                        MaterialIcon {
                            text: r && r.label === "CPU" ? "memory" : "developer_board"
                            color: Colours.primary
                            size: Tokens.font.icon.small
                        }
                        StyledText { text: r ? r.label : ""; color: Colours.fgDim; font: Tokens.font.label.medium }
                    }
                    StyledText {
                        Layout.fillWidth: true
                        text: known ? Math.round(r.value) + "°C" : (r && r.note !== "" ? r.note : "—")
                        color: known && r.value >= 85 ? Colours.error : (known && r.value >= 70 ? Colours.warning : Colours.fg)
                        font: known ? Tokens.font.title.large : Tokens.font.title.small
                    }
                    StyledText {
                        Layout.fillWidth: true
                        visible: r && r.model !== ""
                        text: r ? r.model : ""
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        color: Colours.fg
                        font: Tokens.font.label.small
                    }
                    StyledText {
                        visible: r && r.extra !== ""
                        text: r ? r.extra : ""
                        color: Colours.fgDim
                        font: Tokens.font.label.small
                    }
                    StyledText {
                        visible: r && isFinite(r.usage)
                        text: r ? I18n.t("Usage: %1").arg(I18n.t("%1%").arg(Math.round(r.usage))) : ""
                        color: Colours.fgDim
                        font: Tokens.font.label.small
                    }
                }
            }
        }
    }
    StyledText {
        visible: Sensors.readings.length === 0
        text: I18n.t("Could not read the sensors")
        color: Colours.fgDim
        font: Tokens.font.body.small
    }
}
