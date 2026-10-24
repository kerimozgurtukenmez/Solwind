import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.services

// Pill button in Caelestia's styles (components/controls/TextButton.qml, GPL-3.0): filled (primary), tonal
// (secondary container, the default) or text (no background)
Rectangle {
    id: b
    property string text
    property string icon: ""
    property bool filled: false
    property bool textOnly: false
    readonly property color fgc: filled ? Colours.fgOnPrimary : textOnly ? Colours.primary : Colours.fgOnSecondaryContainer
    signal clicked()

    implicitHeight: Math.max(row.implicitHeight, 20) + Tokens.padding.small * 2
    implicitWidth: row.implicitWidth + Tokens.padding.large * 2
    radius: ma.pressed ? Tokens.rounding.small : height / 2
    color: !enabled ? Qt.alpha(Colours.fg, 0.1) : filled ? Colours.primary : textOnly ? "transparent" : Colours.secondaryContainer
    Behavior on radius { Anim { type: Anim.DefaultEffects } }
    Behavior on color { CAnim {} }

    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        color: b.fgc
        opacity: ma.pressed ? 0.12 : ma.containsMouse ? 0.08 : 0
        Behavior on opacity { Anim { type: Anim.DefaultEffects } }
    }
    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: Tokens.spacing.small
        MaterialIcon { visible: b.icon !== ""; text: b.icon; color: b.enabled ? b.fgc : Qt.alpha(Colours.fg, 0.38); size: Tokens.font.icon.small; fill: 1 }
        StyledText { visible: b.text !== ""; text: b.text; color: b.enabled ? b.fgc : Qt.alpha(Colours.fg, 0.38); font: Tokens.font.label.large }
    }
    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: b.clicked()
    }
}
