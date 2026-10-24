import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.services

// A row with a switch; clicking anywhere on the row flips it (Caelestia's settings app, common/ToggleRow.qml)
GroupItem {
    id: tr
    property string icon: ""
    property string label
    property string sub
    property alias checked: tg.checked
    property bool controlled: false
    signal toggled(bool v)

    clickable: true
    onRowClicked: tg.click()
    opacity: enabled ? 1 : 0.5
    Behavior on opacity { Anim { type: Anim.DefaultEffects } }

    RowLayout {
        Layout.fillWidth: true
        spacing: Tokens.spacing.medium
        MaterialIcon { visible: tr.icon !== ""; text: tr.icon; color: Colours.fgDim; size: Tokens.font.icon.medium }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            StyledText { Layout.fillWidth: true; text: tr.label; color: Colours.fg; font: Tokens.font.body.medium; elide: Text.ElideRight }
            StyledText { visible: tr.sub !== ""; Layout.fillWidth: true; text: tr.sub; wrapMode: Text.WordWrap; color: Colours.outlineStrong; font: Tokens.font.label.small }
        }
        Toggle { id: tg; controlled: tr.controlled; onToggled: v => tr.toggled(v) }
    }
}
