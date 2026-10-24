import QtQuick
import Caelestia.Config
import qs.services

// Material 3 slider as Caelestia's (adapted from Caelestia's components/controls/StyledSlider.qml, GPL-3.0): the
// filled part, a thin handle with a gap on both sides, the rest of the track, and a dot at its end.
Item {
    id: s
    property real value: 0.5          // 0..1
    property color accent: Colours.primary
    signal moved(real v)              // only while the user drags

    implicitWidth: 200
    implicitHeight: Tokens.padding.medium * 2
    readonly property real gap: Tokens.spacing.extraSmall
    readonly property real handleW: 4
    readonly property real pos: Math.max(0, Math.min(1, value)) * (width - handleW)
    readonly property real r: Math.min(Tokens.rounding.small, height / 2)

    // Filled part (rounded at the start, almost square next to the handle)
    Rectangle {
        visible: width > 0
        x: 0
        width: Math.max(0, s.pos - s.gap)
        height: s.height
        topLeftRadius: s.r; bottomLeftRadius: s.r
        topRightRadius: Tokens.rounding.extraSmall / 2; bottomRightRadius: Tokens.rounding.extraSmall / 2
        color: s.enabled ? s.accent : Qt.alpha(Colours.fg, 0.38)
        Behavior on width { enabled: !ma.pressed; Anim {} }
    }
    // Rest of the track
    Rectangle {
        visible: width > 0
        x: s.pos + s.handleW + s.gap
        width: Math.max(0, s.width - x)
        height: s.height
        topRightRadius: s.r; bottomRightRadius: s.r
        topLeftRadius: Tokens.rounding.extraSmall / 2; bottomLeftRadius: Tokens.rounding.extraSmall / 2
        color: s.enabled ? Colours.secondaryContainer : Qt.alpha(Colours.fg, 0.1)
        Behavior on x { enabled: !ma.pressed; Anim {} }
        Rectangle {
            width: 4; height: 4; radius: 2
            anchors { right: parent.right; rightMargin: Math.max(2, (s.height - 4) / 2); verticalCenter: parent.verticalCenter }
            color: s.enabled ? s.accent : Qt.alpha(Colours.fg, 0.38)
            visible: parent.width > 12
        }
    }
    // Handle
    Rectangle {
        x: s.pos
        width: s.handleW
        height: s.height * (ma.pressed ? 1.5 : 1.3)
        anchors.verticalCenter: parent.verticalCenter
        radius: width / 2
        color: s.enabled ? s.accent : Qt.alpha(Colours.fg, 0.38)
        Behavior on x { enabled: !ma.pressed; Anim {} }
        Behavior on height { Anim { type: Anim.FastSpatial } }
    }
    MouseArea {
        id: ma
        anchors.fill: parent
        anchors.topMargin: -6
        anchors.bottomMargin: -6
        preventStealing: true
        cursorShape: Qt.PointingHandCursor
        function upd(m) {
            s.value = Math.max(0, Math.min(1, (m.x - s.handleW / 2) / (s.width - s.handleW)))
            s.moved(s.value)
        }
        onPressed: m => upd(m)
        onPositionChanged: m => upd(m)
    }
}
