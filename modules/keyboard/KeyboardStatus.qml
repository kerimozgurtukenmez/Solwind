import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.components
import qs.services

// What was detected, step by step, and the keyboard light's permission
Card {
    id: status
    property string effectName: ""            // the selected effect, for the summary line
    title: I18n.t("Keyboard light")
    RowLayout {
        Layout.fillWidth: true
        spacing: 12
        Rectangle {
            implicitWidth: 44; implicitHeight: 44; radius: 14
            color: Keyboard.rgb ? Keyboard.color : Colours.surfaceHigh
            opacity: Keyboard.maxBrightness > 0 ? 0.35 + 0.65 * Keyboard.brightness / Keyboard.maxBrightness : 1
            border { width: 2; color: Colours.outline }
            MaterialIcon {
                anchors.centerIn: parent
                visible: !Keyboard.rgb
                text: "keyboard"
                color: Colours.primary
                size: Tokens.font.icon.medium
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            StyledText {
                text: !Keyboard.ready ? I18n.t("Reading…") : Keyboard.available ? Keyboard.name
                    : Keyboard.notes.indexOf("no-plugin") >= 0 ? I18n.t("No plugin for this keyboard light") : I18n.t("No keyboard light found")
                color: Colours.fg
                font: Tokens.font.title.small
            }
            StyledText {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: !Keyboard.available ? (Keyboard.notes.indexOf("no-plugin") >= 0 ? I18n.t("A keyboard light was found, but no installed plugin supports it.")
                                                                                       : I18n.t("This computer has no keyboard light the kernel recognizes."))
                    : Keyboard.kindText() + (Keyboard.effectsAllowed ? " · " + I18n.t("effect: %1").arg(status.effectName) : "")
                color: Colours.fgDim
                font: Tokens.font.label.small
            }
        }
    }
    // Detection, step by step: laptop -> driver -> light -> permission -> support level
    ColumnLayout {
        visible: Keyboard.ready
        Layout.fillWidth: true
        spacing: 4
        Repeater {
            model: [
                { ok: Keyboard.dmi.vendor !== "", label: I18n.t("Laptop"),
                  value: [Keyboard.dmi.vendor, Keyboard.dmi.product].filter(x => x).join(" · ") || "?" },
                { ok: Keyboard.driver !== "", label: I18n.t("Driver"), value: Keyboard.driver || I18n.t("none found") },
                { ok: Keyboard.available, label: I18n.t("Light"),
                  value: Keyboard.available ? Keyboard.name : I18n.t("none found") },
                { ok: Keyboard.available && Keyboard.writable, label: I18n.t("Permission"),
                  value: !Keyboard.available ? "—" : Keyboard.writable ? I18n.t("yes") : I18n.t("needed") },
                { ok: Keyboard.available, label: I18n.t("Support"), value: Keyboard.supportText() }
            ]
            RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 8
                MaterialIcon {
                    text: modelData.ok ? "check_circle" : "cancel"
                    color: modelData.ok ? Colours.success : Colours.fgDim
                    size: Tokens.font.icon.small
                }
                StyledText { text: modelData.label; color: Colours.fgDim; font: Tokens.font.label.small; Layout.preferredWidth: 90 }
                StyledText {
                    Layout.fillWidth: true
                    text: modelData.value
                    wrapMode: Text.Wrap
                    color: Colours.fg
                    font: Tokens.font.label.small
                }
            }
        }
        // Findings (e.g. an RGB keyboard the kernel doesn't expose) and what to use instead
        Repeater {
            model: Keyboard.notes
            StyledText {
                required property string modelData
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: Keyboard.noteText(modelData)
                color: Colours.warning
                font: Tokens.font.label.small
            }
        }
    }
    // No permission: explain what will be done, grant if the user wants
    ColumnLayout {
        visible: Keyboard.available && !Keyboard.writable && Keyboard.ready
        Layout.fillWidth: true
        spacing: 8
        StyledText {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: I18n.t("Permission is needed to change the keyboard light.")
            color: Colours.warning
            font: Tokens.font.label.large
        }
        StyledText {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: (Keyboard.hasBrightness
                   ? I18n.t("What is done: /etc/udev/rules.d/90-solwind.rules is added; only the color and brightness files of the %1 light become writable (what you do by hand with 'sudo tee').").arg(Keyboard.name)
                   : I18n.t("What is done: /etc/udev/rules.d/90-solwind.rules is added; only the color files of the %1 light become writable (what you do by hand with 'sudo tee').").arg(Keyboard.name)) + "\n"
                + (Keyboard.hasBrightness ? I18n.t("What changes: only your user can change the keyboard light's color and brightness; it gives no access to the fans, files or the system, and nothing to other users.")
                                          : I18n.t("What changes: only your user can change the keyboard light's color; it gives no access to the fans, files or the system, and nothing to other users.")) + "\n"
                + (Keyboard.ruleOld ? I18n.t("The old rule of Ccenter (Solwind's old name), 90-ccenter.rules, is removed.") + "\n" : "")
                + I18n.t("Undo: 'Remove permission' here. Your password is asked in the system's own window; Solwind never sees it.")
            color: Colours.fgDim
            font: Tokens.font.label.small
        }
        Btn {
            icon: "key"
            text: Keyboard.permBusy ? I18n.t("Waiting for the password…") : I18n.t("Grant permission")
            filled: true
            enabled: !Keyboard.permBusy
            onClicked: Keyboard.grantPermission()
        }
    }
    // The old rule (earlier versions) let every user write, or has the old name (Ccenter): offer the current one
    GridLayout {
        visible: Keyboard.writable && (Keyboard.ruleLegacy || Keyboard.ruleOld)
        Layout.fillWidth: true
        columns: status.width < 380 ? 1 : 2
        columnSpacing: 8
        rowSpacing: 8
        StyledText {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: Keyboard.ruleLegacy ? I18n.t("The installed permission is from an older, less strict version. The new version allows only your user.")
                                      : I18n.t("The installed permission was added by Ccenter, Solwind's old name (/etc/udev/rules.d/90-ccenter.rules). Updating it moves it to 90-solwind.rules.")
            color: Colours.warning
            font: Tokens.font.label.small
        }
        Btn {
            icon: "shield"
            text: Keyboard.permBusy ? I18n.t("Waiting for the password…") : I18n.t("Update permission")
            filled: true
            enabled: !Keyboard.permBusy
            onClicked: Keyboard.grantPermission()
        }
    }
    // If the permission comes from the rule the app added, it can be removed here
    GridLayout {
        visible: Keyboard.writable && Keyboard.ruleAt === "etc" && !Keyboard.ruleLegacy && !Keyboard.ruleOld
        Layout.fillWidth: true
        columns: status.width < 380 ? 1 : 2
        columnSpacing: 8
        rowSpacing: 8
        StyledText {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: I18n.t("Keyboard light permission is on (/etc/udev/rules.d/90-solwind.rules).")
            color: Colours.fgDim
            font: Tokens.font.label.small
        }
        Btn {
            icon: "key_off"
            text: Keyboard.permBusy ? I18n.t("Waiting for the password…") : I18n.t("Remove permission")
            enabled: !Keyboard.permBusy
            onClicked: Keyboard.revokePermission()
        }
    }
    StyledText {
        visible: Keyboard.error !== ""
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: Keyboard.error
        color: Colours.error
        font: Tokens.font.body.small
    }
}
