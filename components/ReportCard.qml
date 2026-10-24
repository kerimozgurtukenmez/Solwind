import QtQuick
import QtQuick.Layouts
import Quickshell
import qs.services
import qs.utils

// "Problem?" section: creates the diagnostic report (scripts/report.sh, same as `solwind report`) and offers to open
// it. The report is a text file in the home folder; nothing is sent anywhere.
Group {
    id: g
    title: I18n.t("Problem?")

    property string file: ""                  // last report created
    property bool busy: false
    readonly property string issues: "https://github.com/kerimozgurtukenmez/Solwind/issues"

    ButtonRow {
        icon: "bug_report"
        label: g.busy ? I18n.t("Creating…") : I18n.t("Create report")
        sub: I18n.t("Create a report with your system details and Solwind's recent logs, and attach it to a GitHub issue. It is saved as a text file in your home folder; nothing is sent anywhere. Personal names and paths are removed.")
        trailing: ""
        enabled: !g.busy
        onClicked: {
            g.busy = true
            cmd.go(["sh", Quickshell.shellPath("scripts/report.sh")], (code, out) => {
                g.busy = false
                const m = out.match(/(\/\S*solwind-report-[0-9-]+\.txt)/)
                g.file = m ? m[1] : ""
            })
        }
    }
    ButtonRow {
        visible: g.file !== ""
        icon: "description"
        label: I18n.t("Open report")
        sub: I18n.t("Saved: %1").arg(g.file)
        trailing: "open_in_new"
        onClicked: Qt.openUrlExternally("file://" + g.file)
    }
    ButtonRow {
        icon: "open_in_new"
        label: I18n.t("Report a problem")
        sub: g.issues
        trailing: "open_in_new"
        onClicked: Qt.openUrlExternally(g.issues)
    }

    Cmd { id: cmd }
}
