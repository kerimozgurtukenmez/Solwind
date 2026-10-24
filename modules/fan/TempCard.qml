import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.components
import qs.services

Card {
    title: I18n.t("Hardware and temperatures")

    GridLayout {
        Layout.fillWidth: true
        columns: Math.max(1, Sensors.readings.length)
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
