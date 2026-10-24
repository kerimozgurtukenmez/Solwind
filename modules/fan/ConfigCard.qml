import QtQuick
import QtQuick.Layouts
import qs.components
import qs.services

Card {
    id: cfg
    title: "NBFC config"
    property string picked: ""
    property bool open: false
    readonly property string sel: picked !== "" ? picked : Nbfc.configId
    readonly property string q: search.text.toLowerCase()
    readonly property var shown: {
        const f = Nbfc.configs.filter(c => c.toLowerCase().indexOf(q) >= 0)
        const rec = Nbfc.recommended
        return f.filter(c => rec.indexOf(c) >= 0).concat(f.filter(c => rec.indexOf(c) < 0))
    }

    Connections {
        target: Nbfc
        function onRecommendedChanged() {
            if (Nbfc.recommended.length > 0) cfg.picked = Nbfc.recommended[0]
        }
        function onConfigIdChanged() { cfg.picked = "" }
    }

    // Header row: click -> open/close the list
    Item {
        Layout.fillWidth: true
        implicitHeight: Math.max(48, headRow.implicitHeight)     // (a third line when a config is waiting for a restart)
        RowLayout {
            id: headRow
            anchors.fill: parent
            spacing: 12
            StyledText { text: "tune"; color: Colours.primary; font { family: Colours.iconFont; pixelSize: 24 } }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                StyledText {
                    Layout.fillWidth: true
                    text: Nbfc.configId !== "" ? Nbfc.configId : I18n.t("No config selected")
                    elide: Text.ElideRight
                    color: Colours.fg
                    font { pixelSize: 15; bold: true }
                }
                StyledText {
                    text: Nbfc.fans.length > 0 ? I18n.t("%1 fans found").arg(Nbfc.fans.length) : I18n.t("No fan information")
                    color: Colours.fgDim
                    font.pixelSize: 11
                }
                // `nbfc config -s` without a restart: NBFC still runs the old one
                StyledText {
                    Layout.fillWidth: true
                    visible: Nbfc.configPending
                    text: I18n.t("Selected: %1, used after NBFC restarts").arg(Nbfc.fileConfigId)
                    elide: Text.ElideRight
                    color: Colours.warning
                    font.pixelSize: 11
                }
            }
            StyledText {
                text: "expand_more"
                color: Colours.fgDim
                rotation: cfg.open ? 180 : 0
                Behavior on rotation { Anim {} }
                font { family: Colours.iconFont; pixelSize: 24 }
            }
        }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: cfg.open = !cfg.open }
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: cfg.open
        spacing: 8

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 40
            radius: 20
            color: Colours.surfaceHigh
            StyledText {
                x: 14
                anchors.verticalCenter: parent.verticalCenter
                text: "search"
                color: Colours.fgDim
                font { family: Colours.iconFont; pixelSize: 20 }
            }
            StyledText {
                visible: search.text === ""
                x: 44
                anchors.verticalCenter: parent.verticalCenter
                text: I18n.t("Search configs…")
                color: Colours.fgDim
                font.pixelSize: 13
            }
            TextInput {
                id: search
                anchors { fill: parent; leftMargin: 44; rightMargin: 14 }
                verticalAlignment: TextInput.AlignVCenter
                color: Colours.fg
                selectByMouse: true
                clip: true
                font.pixelSize: 13
            }
        }

        StyledText {
            Layout.fillWidth: true
            text: Nbfc.configNote
            wrapMode: Text.WordWrap
            color: Colours.fgDim
            font.pixelSize: 11
        }

        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: 228
            clip: true
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds
            model: cfg.shown
            delegate: Rectangle {
                readonly property bool chosen: cfg.sel === modelData
                width: ListView.view.width
                height: 38
                radius: 12
                color: chosen ? Colours.primary : (ma.containsMouse ? Colours.surfaceHigh : "transparent")
                StyledText {
                    anchors { verticalCenter: parent.verticalCenter; left: parent.left; leftMargin: 14; right: tag.left; rightMargin: 8 }
                    text: modelData
                    elide: Text.ElideRight
                    color: chosen ? Colours.fgOnPrimary : Colours.fg
                    font.pixelSize: 13
                }
                Row {
                    id: tag
                    anchors { verticalCenter: parent.verticalCenter; right: parent.right; rightMargin: 12 }
                    spacing: 8
                    StyledText {
                        visible: Nbfc.recommended.indexOf(modelData) >= 0
                        text: I18n.t("recommended")
                        color: chosen ? Colours.fgOnPrimary : Colours.fgDim
                        font.pixelSize: 11
                    }
                    StyledText {
                        visible: modelData === Nbfc.configId
                        text: "check"
                        color: chosen ? Colours.fgOnPrimary : Colours.primary
                        font { family: Colours.iconFont; pixelSize: 18 }
                    }
                }
                MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: cfg.picked = modelData }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 8
            Btn {
                Layout.fillWidth: true; Layout.preferredWidth: 1
                icon: "auto_awesome"; text: I18n.t("Find recommended")
                onClicked: Nbfc.recommend()
            }
            Btn {
                Layout.fillWidth: true; Layout.preferredWidth: 1
                filled: true; icon: "check"; text: I18n.t("Apply")
                enabled: cfg.sel !== "" && cfg.sel !== Nbfc.configId
                opacity: enabled ? 1 : 0.4
                onClicked: Nbfc.applyConfig(cfg.sel)
            }
        }
        StyledText {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Colours.fgDim
            font.pixelSize: 11
            text: I18n.t("Applying restarts the service and asks for admin permission. An incompatible config can drive the fans wrongly; try it in read-only mode first.")
        }
    }
}
