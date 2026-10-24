import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.services

// One row of a connected group (adapted from Caelestia's settings app, modules/nexus/common/ConnectedRect.qml,
// GPL-3.0): the group's first and last visible rows get the large outer corners, the rows between them small ones.
// The content goes into a column with the row's padding.
Rectangle {
    id: gi
    default property alias content: box.data
    property int padH: Tokens.padding.largeIncreased
    property int padV: Tokens.padding.medium
    property alias gap: box.spacing
    property bool clickable: false            // the whole row reacts to hover/press and emits rowClicked
    signal rowClicked()
    readonly property bool isGroupItem: true
    readonly property var rows: parent ? Array.from(parent.visibleChildren).filter(c => c.isGroupItem === true) : []
    readonly property bool first: rows.length === 0 || rows[0] === gi
    readonly property bool last: rows.length === 0 || rows[rows.length - 1] === gi
    readonly property real outer: Tokens.rounding.extraLarge
    readonly property real inner: Tokens.rounding.extraSmall

    Layout.fillWidth: true
    implicitHeight: box.implicitHeight + padV * 2
    color: Colours.surfaceContainer
    topLeftRadius: first ? outer : inner
    topRightRadius: first ? outer : inner
    bottomLeftRadius: last ? outer : inner
    bottomRightRadius: last ? outer : inner
    Behavior on color { CAnim {} }

    StateLayer { visible: gi.clickable; active: gi.clickable && gi.enabled; onClicked: gi.rowClicked() }
    ColumnLayout {
        id: box
        anchors { fill: parent; leftMargin: gi.padH; rightMargin: gi.padH; topMargin: gi.padV; bottomMargin: gi.padV }
        spacing: Tokens.spacing.medium
    }
}
