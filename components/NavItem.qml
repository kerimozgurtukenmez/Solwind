import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.services

// An entry of the navigation on the left (adapted from Caelestia's settings app, modules/nexus/navpane/NavLocations.qml,
// GPL-3.0): icon in a circle, title and description; the selected one is tinted and fully round
Rectangle {
    id: n
    property string icon
    property string label
    property string sub: ""
    property bool selected: false
    property bool first: false
    property bool last: false
    property bool compact: false              // icon only (narrow window)
    signal clicked()

    readonly property real r: sl.pressed ? Tokens.rounding.medium
                            : selected ? Tokens.rounding.extraLargeIncreased : 0
    Layout.fillWidth: true
    implicitHeight: Math.ceil((row.implicitHeight + Tokens.padding.large * 2) / 2) * 2
    color: selected ? Colours.secondaryContainer : Colours.surfaceHigh
    topLeftRadius: r || (first ? Tokens.rounding.extraLarge : Tokens.rounding.extraSmall)
    topRightRadius: r || (first ? Tokens.rounding.extraLarge : Tokens.rounding.extraSmall)
    bottomLeftRadius: r || (last ? Tokens.rounding.extraLarge : Tokens.rounding.extraSmall)
    bottomRightRadius: r || (last ? Tokens.rounding.extraLarge : Tokens.rounding.extraSmall)
    Behavior on color { CAnim {} }
    Behavior on topLeftRadius { Anim { type: Anim.DefaultEffects } }
    Behavior on topRightRadius { Anim { type: Anim.DefaultEffects } }
    Behavior on bottomLeftRadius { Anim { type: Anim.DefaultEffects } }
    Behavior on bottomRightRadius { Anim { type: Anim.DefaultEffects } }

    StateLayer { id: sl; onClicked: n.clicked() }
    RowLayout {
        id: row
        anchors { fill: parent; margins: Tokens.padding.large }
        spacing: Tokens.spacing.medium
        Rectangle {
            Layout.fillHeight: true
            Layout.topMargin: -1
            Layout.bottomMargin: -1
            Layout.alignment: n.compact ? Qt.AlignHCenter : Qt.AlignLeft
            implicitWidth: height
            implicitHeight: ic.implicitHeight + Tokens.padding.small * 2
            radius: width / 2
            color: n.selected ? Colours.primary : Colours.secondaryContainer
            Behavior on color { CAnim {} }
            MaterialIcon {
                id: ic
                anchors.centerIn: parent
                anchors.verticalCenterOffset: 1
                text: n.icon
                color: n.selected ? Colours.fgOnPrimary : Colours.fgOnSecondaryContainer
                size: Tokens.font.icon.medium
                fill: 1
            }
        }
        ColumnLayout {
            visible: !n.compact
            Layout.fillWidth: true
            spacing: 0
            StyledText { Layout.fillWidth: true; text: n.label; color: Colours.fg; font: Tokens.font.body.medium; elide: Text.ElideRight }
            StyledText { visible: n.sub !== ""; Layout.fillWidth: true; text: n.sub; color: Colours.fgDim; font: Tokens.font.label.small; elide: Text.ElideRight }
        }
    }
}
