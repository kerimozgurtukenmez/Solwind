import QtQuick
import qs.services

// Hover / press feedback over its parent, with the parent's corner radii (adapted from Caelestia's
// components/StateLayer.qml, GPL-3.0; without the press ripple). Clicks go to `clicked`.
MouseArea {
    id: sl
    property color tint: Colours.fg
    property bool active: true                 // false: no feedback, no clicks (disabled)
    readonly property bool lit: active && containsMouse

    anchors.fill: parent
    hoverEnabled: active
    enabled: active
    cursorShape: active ? Qt.PointingHandCursor : Qt.ArrowCursor

    Rectangle {
        anchors.fill: parent
        topLeftRadius: sl.parent.topLeftRadius ?? sl.parent.radius ?? 0
        topRightRadius: sl.parent.topRightRadius ?? sl.parent.radius ?? 0
        bottomLeftRadius: sl.parent.bottomLeftRadius ?? sl.parent.radius ?? 0
        bottomRightRadius: sl.parent.bottomRightRadius ?? sl.parent.radius ?? 0
        color: sl.tint
        opacity: sl.pressed ? 0.12 : sl.lit ? 0.08 : 0
        Behavior on opacity { Anim { type: Anim.DefaultEffects } }
    }
}
