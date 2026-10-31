import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import Caelestia.Config
import qs.components
import qs.services

// What Solwind is, its version, where it comes from and its license
PageBase {
    id: page
    title: I18n.t("About")
    sub: I18n.t("Fan control and keyboard lighting for laptops")

    function open(url) { Quickshell.execDetached(["xdg-open", url]) }

    Group {
        InfoRow { icon: "info"; label: "Solwind"; value: version.text().trim() }
        InfoRow {
            icon: "gavel"
            label: I18n.t("License")
            sub: I18n.t("Free software, without any warranty")
            value: "GPL-3.0"
        }
        InfoRow {
            icon: "smart_toy"
            label: I18n.t("Built with AI assistance")
            sub: I18n.t("I reviewed parts of the code myself and tested every feature on my own laptop before publishing it.")
        }
    }
    Group {
        title: I18n.t("Source")
        ButtonRow {
            icon: "code"
            label: "Solwind"
            sub: "github.com/kerimozgurtukenmez/Solwind"
            trailing: "open_in_new"
            onClicked: page.open("https://github.com/kerimozgurtukenmez/Solwind")
        }
        ButtonRow {
            icon: "extension"
            label: "Solwind-plugins"
            sub: "github.com/kerimozgurtukenmez/Solwind-plugins"
            trailing: "open_in_new"
            onClicked: page.open("https://github.com/kerimozgurtukenmez/Solwind-plugins")
        }
        ButtonRow {
            icon: "palette"
            label: "Caelestia"
            sub: I18n.t("The look of this window comes from its settings app")
            trailing: "open_in_new"
            onClicked: page.open("https://github.com/caelestia-dots")
        }
    }
    FileView { id: version; path: Quickshell.shellPath("VERSION"); blockLoading: true }
}
