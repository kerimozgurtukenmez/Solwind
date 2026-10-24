import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.services

// A row that shows a value (Caelestia's settings app, common/InfoRow.qml)
GroupItem {
    id: ir
    property string icon: ""
    property string label
    property string sub: ""
    property string value: ""
    property color valueColor: Colours.fgDim

    RowLayout {
        Layout.fillWidth: true
        spacing: Tokens.spacing.medium
        MaterialIcon { visible: ir.icon !== ""; text: ir.icon; color: Colours.fgDim; size: Tokens.font.icon.medium }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            StyledText { Layout.fillWidth: true; text: ir.label; color: Colours.fg; font: Tokens.font.body.medium; wrapMode: Text.WordWrap }
            StyledText { visible: ir.sub !== ""; Layout.fillWidth: true; text: ir.sub; wrapMode: Text.Wrap; color: Colours.outlineStrong; font: Tokens.font.label.small }
        }
        StyledText {
            Layout.maximumWidth: ir.width / 2
            text: ir.value
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideRight
            color: ir.valueColor
            font: Tokens.font.body.medium
        }
    }
}
