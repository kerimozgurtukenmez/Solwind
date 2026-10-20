import QtQuick
import QtQuick.Layouts
import qs.components
import qs.services

// Fan profiles: built-in + the user's own (kept separately per NBFC config)
Card {
    id: card
    title: I18n.t("Profiles")

    readonly property string cfgId: Nbfc.configId
    readonly property var custom: Settings.profilesOf(cfgId)
    readonly property string active: FanState.activeId()      // id of the active profile ("" = none)
    readonly property var all: FanState.builtins.map(p => Object.assign({ builtin: true }, p))
                                   .concat(custom.map(p => Object.assign({ builtin: false, icon: "tune" }, p)))
    readonly property string newName: nameInput.text.trim()
    readonly property bool nameTaken: FanState.reservedName(newName)
    property string confirmDelete: ""        // second click needed to delete

    StyledText {
        Layout.fillWidth: true
        text: card.active !== "" ? I18n.t("Active: %1").arg(FanState.activeName()) : I18n.t("Custom settings (no profile selected)")
        color: Colours.fgDim
        font.pixelSize: 11
    }

    Flow {
        Layout.fillWidth: true
        spacing: 8

        Repeater {
            model: card.all
            Rectangle {
                id: chip
                required property var modelData
                readonly property bool on: modelData.name === card.active
                readonly property bool deleting: card.confirmDelete === modelData.name
                implicitHeight: 36
                implicitWidth: row.implicitWidth + 28
                radius: height / 2
                color: deleting ? Colours.error : (on ? Colours.primary : Colours.surfaceHigh)
                Behavior on color { CAnim {} }

                RowLayout {
                    id: row
                    anchors.centerIn: parent
                    spacing: 6
                    StyledText {
                        text: chip.modelData.icon
                        color: chip.on || chip.deleting ? Colours.fgOnPrimary : Colours.primary
                        font { family: Colours.iconFont; pixelSize: 18 }
                    }
                    StyledText {
                        text: chip.deleting ? I18n.t("Delete?") : FanState.displayName(chip.modelData)
                        color: chip.on || chip.deleting ? Colours.fgOnPrimary : Colours.fg
                        font { pixelSize: 13; bold: true }
                    }
                    // Delete button on the user's own profiles (its click area is further down, on top)
                    StyledText {
                        id: closeIcon
                        visible: !chip.modelData.builtin
                        text: "close"
                        color: chip.on || chip.deleting ? Colours.fgOnPrimary : Colours.fgDim
                        font { family: Colours.iconFont; pixelSize: 16 }
                    }
                }
                // Click areas come after the content so they stay on top (z: -1 used to swallow clicks)
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        console.log("[solwind] profile button: " + chip.modelData.name)
                        if (chip.deleting) { card.confirmDelete = ""; return }   // cancel the delete
                        card.confirmDelete = ""
                        console.log("[solwind] " + FanState.applyByName(chip.modelData.name, false))
                    }
                }
                MouseArea {
                    visible: closeIcon.visible
                    x: row.x + closeIcon.x - 6
                    y: 0
                    width: closeIcon.width + 12
                    height: chip.height
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (chip.deleting) {
                            // Reset first: removing the profile destroys this chip, and nothing after that may run here
                            const name = chip.modelData.name
                            card.confirmDelete = ""
                            Settings.removeProfile(card.cfgId, name)
                        } else {
                            card.confirmDelete = chip.modelData.name
                        }
                    }
                }
            }
        }
    }

    // Max fan: all fans at 100%; when the time is up each fan returns to its own setting
    property double now: Date.now()
    Timer { running: Nbfc.boostUntil > 0; interval: 1000; repeat: true; triggeredOnStart: true; onTriggered: card.now = Date.now() }
    // Remaining max-fan time "m:ss". (Not called left(): every Item already has a "left" anchor property.)
    function remaining() {
        const s = Math.max(0, Math.round((Nbfc.boostUntil - now) / 1000))
        return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0")
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        StyledText {
            text: "air"
            color: Nbfc.boosting ? Colours.error : Colours.primary
            font { family: Colours.iconFont; pixelSize: 22 }
        }
        StyledText {
            Layout.fillWidth: true
            text: Nbfc.boosting ? I18n.t("Max fan on") + (Nbfc.boostUntil > 0 ? " · " + I18n.t("%1 left").arg(card.remaining()) : "") : I18n.t("Max fan")
            color: Nbfc.boosting ? Colours.error : Colours.fg
            font { pixelSize: 13; bold: Nbfc.boosting }
        }
        Repeater {
            model: Nbfc.boosting ? [] : [{ t: I18n.t("5 min"), m: 5 }, { t: I18n.t("15 min"), m: 15 }, { t: I18n.t("Until off"), m: -1 }]
            Btn {
                required property var modelData
                text: modelData.t
                enabled: Nbfc.boostBlocked() === ""             // nothing could be written now
                opacity: enabled ? 1 : 0.4
                onClicked: if (enabled) Nbfc.setBoost(modelData.m)
            }
        }
        Btn {
            visible: Nbfc.boosting
            icon: "close"
            text: I18n.t("Turn off")
            filled: true
            onClicked: Nbfc.setBoost(0)
        }
    }

    // Save the current settings as a profile
    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 38
            radius: 19
            color: Colours.surfaceHigh
            StyledText {
                visible: nameInput.text === ""
                x: 16
                anchors.verticalCenter: parent.verticalCenter
                text: I18n.t("Save the current settings: profile name…")
                color: Colours.fgDim
                font.pixelSize: 13
            }
            TextInput {
                id: nameInput
                anchors { fill: parent; leftMargin: 16; rightMargin: 16 }
                verticalAlignment: TextInput.AlignVCenter
                color: Colours.fg
                selectByMouse: true
                clip: true
                maximumLength: 32
                font { family: Colours.fontFamily; pixelSize: 13 }
                onAccepted: save.clicked()
            }
        }
        Btn {
            id: save
            icon: "save"
            text: I18n.t("Save")
            filled: true
            enabled: card.newName !== "" && !card.nameTaken
            opacity: enabled ? 1 : 0.4
            onClicked: {
                if (!enabled) return
                Settings.saveProfile(card.cfgId, FanState.snapshot(card.newName))
                Settings.setActive(card.cfgId, card.newName)
                nameInput.text = ""
            }
        }
    }
    StyledText {
        visible: card.nameTaken
        text: I18n.t("This name belongs to a built-in profile, pick another one.")
        color: Colours.error
        font.pixelSize: 11
    }
}
