import QtQuick
import QtQuick.Shapes
import Caelestia.Config
import qs.services

// Material 3 switch as Caelestia's (adapted from Caelestia's components/controls/StyledSwitch.qml, GPL-3.0): a pill
// track, a round thumb with a check / cross drawn in it (a dash while pressed).
Item {
    id: t
    property bool checked: false
    property bool controlled: false   // true: a click doesn't flip `checked` by itself; bind the state from outside
    signal toggled(bool v)
    readonly property bool pressed: ma.pressed
    readonly property bool hovered: ma.containsMouse

    implicitHeight: Tokens.font.body.medium.pointSize + Tokens.padding.small * 2
    implicitWidth: implicitHeight * 1.7

    Rectangle {
        id: track
        anchors.fill: parent
        radius: height / 2
        color: !t.enabled ? (t.checked ? Qt.alpha(Colours.fg, 0.12) : Qt.alpha(Colours.surfaceHighest, 0.38))
             : t.checked ? Colours.primary : Colours.surfaceHighest
        Behavior on color { CAnim {} }

        Rectangle {
            id: thumb
            readonly property real size: parent.height - Tokens.padding.extraSmall
            width: t.pressed ? size * 1.2 : size
            height: size
            radius: height / 2
            anchors.verticalCenter: parent.verticalCenter
            x: t.checked ? parent.width - width - Tokens.padding.extraSmall / 2 : Tokens.padding.extraSmall / 2
            color: !t.enabled ? (t.checked ? Colours.surface : Qt.alpha(Colours.fg, 0.12))
                 : t.checked ? Colours.fgOnPrimary : Colours.outlineStrong
            Behavior on x { Anim { type: Anim.FastSpatial } }
            Behavior on width { Anim { type: Anim.FastSpatial } }
            Behavior on color { CAnim {} }

            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                color: t.checked ? Colours.primary : Colours.fg
                opacity: t.pressed ? 0.1 : t.hovered ? 0.08 : 0
                Behavior on opacity { Anim { type: Anim.DefaultEffects } }
            }

            Shape {
                id: icon
                property point start1: t.pressed ? Qt.point(width * 0.2, height / 2)
                                     : t.checked ? Qt.point(width * 0.15, height / 2) : Qt.point(width * 0.15, height * 0.15)
                property point end1: t.pressed ? (t.checked ? Qt.point(width * 0.4, height / 2) : Qt.point(width * 0.8, height / 2))
                                   : t.checked ? Qt.point(width * 0.4, height * 0.7) : Qt.point(width * 0.85, height * 0.85)
                property point start2: t.pressed ? (t.checked ? Qt.point(width * 0.4, height / 2) : Qt.point(width * 0.2, height / 2))
                                     : t.checked ? Qt.point(width * 0.4, height * 0.7) : Qt.point(width * 0.15, height * 0.85)
                property point end2: t.pressed ? Qt.point(width * 0.8, height / 2)
                                   : t.checked ? Qt.point(width * 0.85, height * 0.2) : Qt.point(width * 0.85, height * 0.15)
                anchors.centerIn: parent
                width: height
                height: track.height - Tokens.padding.medium
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    strokeWidth: Tokens.font.body.large.pointSize * 0.15
                    strokeColor: !t.enabled ? (t.checked ? Colours.outlineStrong : Colours.surfaceContainer)
                               : t.checked ? Colours.primary : Colours.surfaceHighest
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    startX: icon.start1.x; startY: icon.start1.y
                    PathLine { x: icon.end1.x; y: icon.end1.y }
                    PathMove { x: icon.start2.x; y: icon.start2.y }
                    PathLine { x: icon.end2.x; y: icon.end2.y }
                    Behavior on strokeColor { CAnim {} }
                }
                Behavior on start1 { PropertyAnimation { duration: Tokens.anim.durations.expressiveFastSpatial; easing: Tokens.anim.expressiveFastSpatial } }
                Behavior on end1 { PropertyAnimation { duration: Tokens.anim.durations.expressiveFastSpatial; easing: Tokens.anim.expressiveFastSpatial } }
                Behavior on start2 { PropertyAnimation { duration: Tokens.anim.durations.expressiveFastSpatial; easing: Tokens.anim.expressiveFastSpatial } }
                Behavior on end2 { PropertyAnimation { duration: Tokens.anim.durations.expressiveFastSpatial; easing: Tokens.anim.expressiveFastSpatial } }
            }
        }
    }
    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: t.click()
    }
    // Also used by a row: clicking anywhere on it flips the switch
    function click() {
        const v = !checked
        if (!controlled) checked = v
        toggled(v)
    }
}
