import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.services

// A titled block: the section header above, the content in one rounded row (the look of Caelestia's settings app)
ColumnLayout {
    id: card
    default property alias content: row.content
    property string title: ""
    property int pad: Tokens.padding.largeIncreased
    property int gap: Tokens.spacing.medium
    property bool fillHeight: false           // stretch the block to the height it is given (side-by-side cards)

    Layout.fillWidth: true
    Layout.preferredWidth: 1
    spacing: 0

    StyledText {
        visible: card.title !== ""
        Layout.fillWidth: true
        Layout.leftMargin: Tokens.padding.small
        Layout.bottomMargin: Tokens.spacing.small
        text: card.title
        color: Colours.fgDim
        font: Tokens.font.label.medium
        elide: Text.ElideRight
    }
    GroupItem {
        id: row
        padH: card.pad
        padV: card.pad
        gap: card.gap
        Layout.fillHeight: card.fillHeight
    }
}
