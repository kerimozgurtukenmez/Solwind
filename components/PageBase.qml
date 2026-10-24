import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.services

// A page (the layout of Caelestia's settings app, modules/nexus/common/PageBase.qml, GPL-3.0): a large title, then
// the content, centred and capped in width, scrolling below the title. Sections go into `content`.
ColumnLayout {
    id: p
    property string title
    property string sub: ""
    default property alias content: col.data
    property alias header: extra.data          // extra items on the right of the title (chips, buttons)
    property alias pinnedContent: pinned.data            // items between the title and the scrolling content (they don't scroll)
    property alias flick: fl

    spacing: Tokens.spacing.large

    RowLayout {
        Layout.fillWidth: true
        Layout.maximumWidth: Tokens.sizes.nexus.maxContentWidth
        Layout.alignment: Qt.AlignHCenter
        spacing: Tokens.spacing.largeIncreased
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            StyledText { Layout.fillWidth: true; text: p.title; color: Colours.fg; font: Tokens.font.title.large; elide: Text.ElideRight }
            StyledText { visible: p.sub !== ""; Layout.fillWidth: true; text: p.sub; color: Colours.fgDim; font: Tokens.font.body.small; elide: Text.ElideRight }
        }
        RowLayout { id: extra; spacing: Tokens.spacing.small }
    }

    ColumnLayout {
        id: pinned
        visible: children.length > 0
        Layout.fillWidth: true
        Layout.maximumWidth: Tokens.sizes.nexus.maxContentWidth
        Layout.alignment: Qt.AlignHCenter
        spacing: Tokens.spacing.largeIncreased
    }
    Flickable {
        id: fl
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        contentWidth: width
        contentHeight: col.implicitHeight + Tokens.padding.extraLarge
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: col
            width: Math.min(fl.width, Tokens.sizes.nexus.maxContentWidth)
            x: (fl.width - width) / 2
            spacing: Tokens.spacing.largeIncreased
        }
    }
}
