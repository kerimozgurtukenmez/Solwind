import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.services

// Connected button group (Material 3): the chosen one is filled and fully round, the others tonal with small inner corners
RowLayout {
    id: seg
    property var model: []
    property int current: 0
    property bool controlled: false   // true: a click doesn't change `current` by itself; bind `current` from outside
    signal picked(int i)              // only when the user clicks

    Layout.fillWidth: true
    spacing: Tokens.spacing.extraSmall / 2

    Repeater {
        model: seg.model
        Rectangle {
            id: item
            required property var modelData
            required property int index
            readonly property bool on: seg.current === index
            readonly property real full: height / 2
            readonly property real inner: ma.pressed ? Tokens.rounding.extraSmall : Tokens.rounding.small
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            implicitHeight: label.implicitHeight + Tokens.padding.small * 2
            topLeftRadius: on || index === 0 ? full : inner
            bottomLeftRadius: on || index === 0 ? full : inner
            topRightRadius: on || index === seg.model.length - 1 ? full : inner
            bottomRightRadius: on || index === seg.model.length - 1 ? full : inner
            color: on ? Colours.primary : Colours.secondaryContainer
            Behavior on color { CAnim {} }
            Behavior on topLeftRadius { Anim { type: Anim.DefaultEffects } }
            Behavior on topRightRadius { Anim { type: Anim.DefaultEffects } }
            Behavior on bottomLeftRadius { Anim { type: Anim.DefaultEffects } }
            Behavior on bottomRightRadius { Anim { type: Anim.DefaultEffects } }

            Rectangle {
                anchors.fill: parent
                topLeftRadius: item.topLeftRadius; topRightRadius: item.topRightRadius
                bottomLeftRadius: item.bottomLeftRadius; bottomRightRadius: item.bottomRightRadius
                color: item.on ? Colours.fgOnPrimary : Colours.fgOnSecondaryContainer
                opacity: ma.pressed ? 0.12 : ma.containsMouse ? 0.08 : 0
                Behavior on opacity { Anim { type: Anim.DefaultEffects } }
            }
            StyledText {
                id: label
                anchors.centerIn: parent
                width: Math.min(implicitWidth, parent.width - 8)
                elide: Text.ElideRight
                text: item.modelData
                color: item.on ? Colours.fgOnPrimary : Colours.fgOnSecondaryContainer
                font: Tokens.font.label.large
            }
            MouseArea {
                id: ma
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: { if (!seg.controlled) seg.current = item.index; seg.picked(item.index) }
            }
        }
    }
}
