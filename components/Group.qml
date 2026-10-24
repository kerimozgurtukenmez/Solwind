import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.services

// A section: a small header and its rows (GroupItem and the *Row components), connected like in Caelestia's
// settings app (modules/nexus/common/SectionHeader.qml, GPL-3.0)
ColumnLayout {
    id: g
    property string title: ""
    default property alias rows: list.data

    Layout.fillWidth: true
    spacing: 0

    StyledText {
        visible: g.title !== ""
        Layout.fillWidth: true
        Layout.leftMargin: Tokens.padding.small
        Layout.bottomMargin: Tokens.spacing.small
        text: g.title
        color: Colours.fgDim
        font: Tokens.font.label.medium
        elide: Text.ElideRight
    }
    ColumnLayout {
        id: list
        Layout.fillWidth: true
        spacing: Tokens.spacing.extraSmall / 2
    }
}
