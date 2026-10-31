import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.services

// A page (the layout of Caelestia's settings app, modules/nexus/common/PageBase.qml, GPL-3.0): a large title, then
// the content, centred and capped in width, scrolling below the title. Sections go into `content`.
// An `adaptive` page uses all the width of a wide window: `wide` says so, and `side` is a column beside the scrolling
// content that doesn't scroll. In a narrow window `side` is hidden and the page is the usual single column.
ColumnLayout {
    id: p
    property string title
    property string sub: ""
    default property alias content: col.data
    property alias header: extra.data          // extra items on the right of the title (chips, buttons)
    property alias pinnedContent: pinned.data            // items between the title and the scrolling content (they don't scroll)
    property alias side: sideBox.data
    property alias flick: fl
    property bool adaptive: false
    property bool showPinned: true
    readonly property bool wide: adaptive && width >= 760
    readonly property real cap: adaptive ? Number.POSITIVE_INFINITY : Tokens.sizes.nexus.maxContentWidth

    spacing: Tokens.spacing.large

    RowLayout {
        visible: p.title !== ""
        Layout.fillWidth: true
        Layout.maximumWidth: p.cap
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
        visible: p.showPinned && children.length > 0
        Layout.fillWidth: true
        Layout.maximumWidth: p.cap
        Layout.alignment: Qt.AlignHCenter
        spacing: Tokens.spacing.largeIncreased
    }
    RowLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: Tokens.spacing.large

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
                width: Math.min(fl.width, p.cap)
                x: (fl.width - width) / 2
                spacing: Tokens.spacing.largeIncreased
            }
        }
        RowLayout {
            id: sideBox
            visible: p.wide
            Layout.fillWidth: false
            Layout.fillHeight: true
            Layout.bottomMargin: Tokens.padding.extraLarge
            spacing: Tokens.spacing.large
        }
    }
}
