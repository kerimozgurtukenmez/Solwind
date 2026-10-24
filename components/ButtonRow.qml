import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.services

// A clickable row: icon, text, and a trailing icon (a chevron by default) (Caelestia's settings app, RowButton/NavRow)
GroupItem {
    id: br
    property string icon: ""
    property string label
    property string sub: ""
    property string trailing: "chevron_right"
    property color accent: Colours.fgDim
    signal clicked()

    clickable: true
    onRowClicked: br.clicked()
    opacity: enabled ? 1 : 0.5
    Behavior on opacity { Anim { type: Anim.DefaultEffects } }

    RowLayout {
        Layout.fillWidth: true
        spacing: Tokens.spacing.medium
        MaterialIcon { visible: br.icon !== ""; text: br.icon; color: br.accent; size: Tokens.font.icon.medium; fill: 1 }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            StyledText { Layout.fillWidth: true; text: br.label; color: Colours.fg; font: Tokens.font.body.medium; elide: Text.ElideRight }
            StyledText { visible: br.sub !== ""; Layout.fillWidth: true; text: br.sub; wrapMode: Text.Wrap; color: Colours.outlineStrong; font: Tokens.font.label.small }
        }
        MaterialIcon { visible: br.trailing !== ""; text: br.trailing; color: Colours.fgDim; size: Tokens.font.icon.medium }
    }
}
