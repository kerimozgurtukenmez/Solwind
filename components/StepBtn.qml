import QtQuick
import Caelestia.Config
import qs.services

// A +/- button of a stepper (the look of Caelestia's StyledSpinBox, GPL-3.0). Repeats while held down.
// side: "" round on its own, "left" / "right" as the outer end of [- value +] (its inner corners are small).
Rectangle {
    id: b
    property string icon
    property string side: ""
    signal activated()

    implicitWidth: implicitHeight
    implicitHeight: Tokens.font.icon.medium.pointSize + Tokens.padding.small * 2
    readonly property real full: height / 2
    readonly property real small: ma.pressed ? Tokens.rounding.small : Tokens.rounding.extraSmall
    topLeftRadius: side === "right" ? small : full
    bottomLeftRadius: side === "right" ? small : full
    topRightRadius: side === "left" ? small : full
    bottomRightRadius: side === "left" ? small : full
    color: Colours.surfaceHighest
    Behavior on topLeftRadius { Anim { type: Anim.DefaultEffects } }
    Behavior on topRightRadius { Anim { type: Anim.DefaultEffects } }
    Behavior on bottomLeftRadius { Anim { type: Anim.DefaultEffects } }
    Behavior on bottomRightRadius { Anim { type: Anim.DefaultEffects } }

    Rectangle {
        anchors.fill: parent
        topLeftRadius: b.topLeftRadius; topRightRadius: b.topRightRadius
        bottomLeftRadius: b.bottomLeftRadius; bottomRightRadius: b.bottomRightRadius
        color: Colours.fg
        opacity: ma.pressed ? 0.12 : ma.containsMouse ? 0.08 : 0
        Behavior on opacity { Anim { type: Anim.DefaultEffects } }
    }
    MaterialIcon {
        anchors.centerIn: parent
        anchors.horizontalCenterOffset: ma.pressed ? 0 : (b.side === "left" ? -2 : b.side === "right" ? 2 : 0)
        text: b.icon
        color: Colours.fgDim
        size: Tokens.font.icon.medium
        Behavior on anchors.horizontalCenterOffset { Anim { type: Anim.FastSpatial } }
    }
    // Held down: repeats, faster and faster (400 ms, then 50 ms less each time, down to 50 ms)
    Timer {
        id: rep
        interval: 400
        repeat: true
        onTriggered: { b.activated(); interval = Math.max(50, interval - 50) }
    }
    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onPressed: { b.activated(); rep.interval = 400; rep.start() }
        onReleased: rep.stop()
        onCanceled: rep.stop()
    }
}
