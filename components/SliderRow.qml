import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.services

// A row with a label, a value and a slider below them (Caelestia's settings app, common/SliderRow.qml)
GroupItem {
    id: row
    property string icon: ""
    property string label
    property string sub: ""
    property string readout
    property alias value: sl.value
    property bool flat: false          // already inside a card's row: no second background or padding
    signal moved(real v)

    color: flat ? "transparent" : Colours.surfaceContainer
    padH: flat ? 0 : Tokens.padding.largeIncreased
    padV: flat ? Tokens.padding.small : Tokens.padding.large
    opacity: enabled ? 1 : 0.5
    RowLayout {
        Layout.fillWidth: true
        spacing: Tokens.spacing.medium
        MaterialIcon { visible: row.icon !== ""; text: row.icon; color: Colours.fgDim; size: Tokens.font.icon.medium }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            StyledText { Layout.fillWidth: true; text: row.label; color: Colours.fg; font: Tokens.font.body.medium; wrapMode: Text.WordWrap }
            StyledText { visible: row.sub !== ""; Layout.fillWidth: true; text: row.sub; wrapMode: Text.WordWrap; color: Colours.outlineStrong; font: Tokens.font.label.small }
        }
        StyledText { text: row.readout; color: Colours.fgDim; font: Tokens.font.body.medium }
    }
    Slide { id: sl; Layout.fillWidth: true; onMoved: v => row.moved(v) }
}
