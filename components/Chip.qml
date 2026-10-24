import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.services

// Small status pill: an optional dot or icon and a text
Rectangle {
    id: c
    property string text
    property string icon: ""
    property color dot: "transparent"         // a coloured dot before the text (status)
    property bool strong: false               // tinted (secondary container) instead of the neutral container

    implicitHeight: row.implicitHeight + Tokens.padding.small * 2
    implicitWidth: row.implicitWidth + Tokens.padding.large * 2
    radius: height / 2
    color: strong ? Colours.secondaryContainer : Colours.surfaceHigh
    Behavior on color { CAnim {} }
    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: Tokens.spacing.small
        Rectangle { visible: c.dot.a > 0; implicitWidth: 8; implicitHeight: 8; radius: 4; color: c.dot }
        MaterialIcon { visible: c.icon !== ""; text: c.icon; color: c.strong ? Colours.fgOnSecondaryContainer : Colours.fgDim; size: Tokens.font.icon.small; fill: 1 }
        StyledText { text: c.text; color: c.strong ? Colours.fgOnSecondaryContainer : Colours.fg; font: Tokens.font.label.large }
    }
}
