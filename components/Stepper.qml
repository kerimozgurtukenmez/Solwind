import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.services

// Label on the left, [- value +] on the right (the look of Caelestia's StyledSpinBox, GPL-3.0)
RowLayout {
    id: s
    property string label
    property string sub: ""
    property int value: 0
    property int from: 0
    property int to: 100
    property string unit: ""
    signal moved(int v)

    spacing: Tokens.spacing.medium
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 0
        StyledText { Layout.fillWidth: true; text: s.label; color: Colours.fg; font: Tokens.font.body.medium; elide: Text.ElideRight }
        StyledText { visible: s.sub !== ""; Layout.fillWidth: true; text: s.sub; wrapMode: Text.WordWrap; color: Colours.outlineStrong; font: Tokens.font.label.small }
    }
    RowLayout {
        spacing: Tokens.spacing.small
        StepBtn { side: "left"; icon: "remove"; onActivated: s.moved(Math.max(s.from, s.value - 1)) }
        Rectangle {
            implicitWidth: 65
            implicitHeight: minus.implicitHeight
            radius: Tokens.rounding.extraSmall
            color: Colours.surfaceHighest
            StyledText {
                anchors.centerIn: parent
                text: s.value + s.unit
                color: Colours.fg
                font: Tokens.font.body.medium
            }
        }
        StepBtn { id: minus; side: "right"; icon: "add"; onActivated: s.moved(Math.min(s.to, s.value + 1)) }
    }
}
