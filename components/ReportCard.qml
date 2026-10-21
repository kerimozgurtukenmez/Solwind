import QtQuick
import QtQuick.Layouts
import Quickshell
import qs.services
import qs.utils

// "Something wrong?" card: creates the diagnostic report (scripts/report.sh, same as `solwind report`) and offers to
// open it. The report is a text file in the home folder; nothing is sent anywhere.
Card {
    id: card
    title: I18n.t("Problem?")

    property string file: ""                  // last report created
    property bool busy: false
    readonly property string issues: "https://github.com/kerimozgurtukenmez/Solwind/issues"

    StyledText {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: I18n.t("Create a report with your system details and Solwind's recent logs, and attach it to a GitHub issue. It is saved as a text file in your home folder; nothing is sent anywhere. Personal names and paths are removed.")
        color: Colours.fgDim
        font.pixelSize: 11
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Btn {
            icon: "bug_report"
            text: card.busy ? I18n.t("Creating…") : I18n.t("Create report")
            filled: true
            enabled: !card.busy
            onClicked: {
                card.busy = true
                cmd.go(["sh", Quickshell.shellPath("scripts/report.sh")], (code, out) => {
                    card.busy = false
                    const m = out.match(/(\/\S*solwind-report-[0-9-]+\.txt)/)
                    card.file = m ? m[1] : ""
                })
            }
        }
        Btn {
            visible: card.file !== ""
            icon: "description"
            text: I18n.t("Open report")
            onClicked: Qt.openUrlExternally("file://" + card.file)
        }
        Btn {
            icon: "open_in_new"
            text: I18n.t("Report a problem")
            onClicked: Qt.openUrlExternally(card.issues)
        }
    }
    StyledText {
        visible: card.file !== ""
        Layout.fillWidth: true
        wrapMode: Text.WrapAnywhere
        text: I18n.t("Saved: %1").arg(card.file)
        color: Colours.success
        font.pixelSize: 11
    }

    Cmd { id: cmd }
}
