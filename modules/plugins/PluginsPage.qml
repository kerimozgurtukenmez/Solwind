import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.components
import qs.services

// Plugins: Solwind's catalog, and installing / updating / removing them (services/Plugins.qml runs solwind-plugin, as
// the user). They are loaded when solwindd starts.
PageBase {
    id: page
    title: I18n.t("Plugins")
    sub: I18n.t("What drives your fans and keyboard light")

    // Each time the page is opened: `solwind plugin` may have changed something meanwhile
    onVisibleChanged: if (visible) Plugins.refresh()
    Component.onCompleted: Plugins.refresh()

    readonly property string query: search.text.trim().toLowerCase()
    function matches(...texts) { return query === "" || texts.some(t => t.toLowerCase().includes(query)) }

    Group {
        InfoRow {
            icon: "extension"
            label: I18n.t("Only the plugins you install are downloaded")
            sub: I18n.t("Each comes from Solwind's catalog at a fixed version (a commit of the Solwind-plugins repository, checked by git), is built on this computer and goes into your home folder. No password is needed.")
        }
    }

    // Something changed: solwindd loads plugins when it starts
    Group {
        visible: Plugins.changed
        ButtonRow {
            visible: Plugins.canRestart
            icon: "restart_alt"
            accent: Colours.primary
            label: I18n.t("Restart Solwind to load them")
            sub: I18n.t("The background service starts again; this window closes and opens again")
            trailing: "play_arrow"
            onClicked: Plugins.restart()
        }
        InfoRow {
            visible: !Plugins.canRestart
            icon: "restart_alt"
            label: I18n.t("Restart Solwind to load them")
            sub: I18n.t("In a terminal: solwind quit, then solwind")
        }
    }

    Group {
        visible: Plugins.message !== "" || Plugins.loadError !== ""
        InfoRow {
            icon: Plugins.messageError || Plugins.loadError !== "" ? "error" : "check_circle"
            label: Plugins.loadError !== "" ? I18n.t("The plugin list couldn't be read")
                 : Plugins.messageError ? I18n.t("That didn't work") : I18n.t("Done")
            sub: Plugins.loadError !== "" ? Plugins.loadError : Plugins.message
        }
    }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 44
        radius: height / 2
        color: Colours.surfaceContainer
        RowLayout {
            anchors { fill: parent; leftMargin: Tokens.padding.large; rightMargin: Tokens.padding.large }
            spacing: Tokens.spacing.small
            MaterialIcon { text: "search"; color: Colours.fgDim; size: Tokens.font.icon.medium }
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                StyledText {
                    visible: search.text === ""
                    anchors.verticalCenter: parent.verticalCenter
                    text: I18n.t("Search plugins…")
                    color: Colours.fgDim
                    font: Tokens.font.body.small
                }
                TextInput {
                    id: search
                    anchors.fill: parent
                    verticalAlignment: TextInput.AlignVCenter
                    color: Colours.fg
                    selectByMouse: true
                    clip: true
                    font: Tokens.font.body.medium
                }
            }
        }
    }

    Group {
        title: I18n.t("Catalog")
        GroupItem {
            visible: !Plugins.loaded
            StyledText { text: I18n.t("Reading…"); color: Colours.fgDim; font: Tokens.font.body.medium }
        }
        Repeater {
            model: Plugins.plugins
            GroupItem {
                id: row
                required property var modelData
                visible: page.matches(modelData.id, modelData.description)
                readonly property string st: modelData.state
                readonly property bool mine: Plugins.busyId === modelData.id || (Plugins.busyId === "*" && st !== "none")
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Tokens.spacing.medium
                    MaterialIcon {
                        text: row.modelData.kind === "fan" ? "mode_fan" : "keyboard"
                        color: row.st === "none" ? Colours.fgDim : Colours.primary
                        size: Tokens.font.icon.medium
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        StyledText { Layout.fillWidth: true; text: row.modelData.id; color: Colours.fg; font: Tokens.font.body.medium; elide: Text.ElideRight }
                        StyledText { Layout.fillWidth: true; text: row.modelData.description; wrapMode: Text.Wrap; color: Colours.outlineStrong; font: Tokens.font.label.small }
                        StyledText {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            font: Tokens.font.label.small
                            color: row.modelData.fits === "yes" ? Colours.success : Colours.fgDim
                            text: [
                                row.mine ? I18n.t("Working… (downloading and building can take a minute)")
                                    : row.st === "none" ? I18n.t("not installed")
                                    : row.st === "current" ? I18n.t("installed, up to date")
                                    : row.st === "update" ? I18n.t("installed, an update is in the catalog")
                                    : row.st === "sdk" ? I18n.t("installed, built for another Solwind")
                                    : row.st === "hand" ? I18n.t("installed by hand")
                                    : I18n.t("installed from a local folder"),
                                row.modelData.fits === "yes" ? I18n.t("fits this computer")
                                    : row.modelData.fits === "no" ? I18n.t("doesn't fit this computer") : "",
                                row.modelData.tested ? I18n.t("tested on: %1").arg(row.modelData.tested) : I18n.t("not tested yet")
                            ].filter(x => x !== "").join(" · ")
                        }
                    }
                    ColumnLayout {
                        visible: row.st !== "none"
                        spacing: 0
                        Toggle {
                            Layout.alignment: Qt.AlignHCenter
                            enabled: !Plugins.busy
                            controlled: true
                            checked: row.modelData.enabled
                            onToggled: v => Plugins.setEnabled(row.modelData.id, v)
                        }
                        StyledText {
                            Layout.alignment: Qt.AlignHCenter
                            text: row.modelData.enabled ? I18n.t("Active") : I18n.t("Turned off")
                            color: Colours.fgDim
                            font: Tokens.font.label.small
                        }
                    }
                    Btn {
                        visible: row.st !== "current" && row.st !== "local"
                        enabled: !Plugins.busy
                        filled: row.st !== "none" || row.modelData.fits === "yes"
                        icon: row.st === "none" ? "download" : "update"
                        text: row.st === "none" ? I18n.t("Install") : I18n.t("Update")
                        onClicked: Plugins.install(row.modelData.id)
                    }
                    Btn {
                        visible: row.st !== "none"
                        enabled: !Plugins.busy
                        textOnly: true
                        text: I18n.t("Remove")
                        onClicked: Plugins.remove(row.modelData.id)
                    }
                }
            }
        }
        Repeater {
            model: Plugins.others
            InfoRow {
                required property string modelData
                visible: page.matches(modelData)
                icon: "help"
                label: modelData
                sub: I18n.t("installed, not in the catalog (solwind plugin remove %1)").arg(modelData)
            }
        }
    }
}
