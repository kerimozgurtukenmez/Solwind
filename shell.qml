//@ pragma AppId solwind
// solwindd-client: the fans and the keyboard light are driven by solwindd (D-Bus); this window only shows and changes them
import Quickshell
import Quickshell.Io
import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.components
import qs.modules.fan
import qs.modules.keyboard
import qs.modules.settings
import qs.services

ShellRoot {
    id: root
    // The window state lives outside the window: closing the window destroys it completely
    // (UI, graphics context and render caches are freed; fan control keeps running in solwindd)
    // Started by the service (SOLWIND_BACKGROUND=1) it runs in the background without opening the window
    property bool shown: Quickshell.env("SOLWIND_BACKGROUND") !== "1"
    property int tab: 0

    LazyLoader {
        active: root.shown
        // FloatingWindow = a normal xdg window (Hyprland tiles it). PanelWindow is layer-shell and doesn't tile.
        FloatingWindow {
            id: win
            title: "Solwind"
            visible: true
            // Closing the window doesn't quit the app; it keeps running in the background (reopen with `solwind`)
            onClosed: root.shown = false
            implicitWidth: 900
            implicitHeight: 780
            color: Colours.surface
            // Caelestia's tokens are per screen (rounding, spacing etc.)
            contentItem.Tokens.screen: screen?.name ?? ""

            // The layout of Caelestia's settings app (modules/nexus/Nexus.qml, GPL-3.0): navigation on the left,
            // the page on a panel next to it. A narrow window shows the navigation as icons only.
            readonly property bool compact: width < 720
            readonly property var pages: [
                { icon: "mode_fan", label: I18n.t("Fan control"), sub: I18n.t("NBFC · speed and curves") },
                { icon: "keyboard", label: I18n.t("Keyboard"), sub: I18n.t("RGB · color and brightness") },
                { icon: "settings", label: I18n.t("Settings"), sub: I18n.t("Language and notifications") }
            ]

            ColumnLayout {
                id: nav
                anchors { top: parent.top; bottom: parent.bottom; left: parent.left; margins: Tokens.padding.large }
                width: win.compact ? 76 : Math.min(Tokens.sizes.nexus.maxNavWidth, 340, Math.round(win.width / 3))
                spacing: Tokens.spacing.large

                // App name: one word in two colours (no gap between them)
                Row {
                    visible: !win.compact
                    Layout.leftMargin: Tokens.padding.small
                    Layout.topMargin: Tokens.padding.small
                    StyledText { text: "Sol"; color: Colours.fg; font: Tokens.font.title.large }
                    StyledText { text: "wind"; color: Colours.primary; font: Tokens.font.title.large }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Tokens.spacing.extraSmall
                    Repeater {
                        model: win.pages
                        NavItem {
                            required property var modelData
                            required property int index
                            icon: modelData.icon; label: modelData.label; sub: modelData.sub
                            compact: win.compact
                            selected: root.tab === index
                            first: index === 0
                            last: index === win.pages.length - 1
                            onClicked: root.tab = index
                        }
                    }
                }
                Item { Layout.fillHeight: true }
                // A glance at the state, whatever page is open
                Flow {
                    visible: !win.compact
                    Layout.fillWidth: true
                    spacing: Tokens.spacing.small
                    Chip {
                        visible: !Daemon.connected
                        dot: Colours.error
                        text: I18n.t("No background service")
                    }
                    Chip {
                        visible: Daemon.connected
                        dot: Nbfc.running ? Colours.success : Colours.outlineStrong
                        text: Nbfc.running ? (Nbfc.readOnly ? I18n.t("Running (read-only)") : I18n.t("Running")) : I18n.t("Stopped")
                    }
                    Chip { visible: isFinite(Nbfc.temp); icon: "thermostat"; text: "CPU " + Math.round(Nbfc.temp) + "°C" }
                    Chip { visible: Nbfc.boosting; strong: true; icon: "air"; text: I18n.t("Max fan") }
                }
            }

            Rectangle {
                id: panel
                anchors { top: parent.top; bottom: parent.bottom; right: parent.right; left: nav.right
                          margins: Tokens.padding.medium; leftMargin: Tokens.padding.large }
                radius: Tokens.rounding.large
                color: Colours.surfaceLow

                StackLayout {
                    anchors { fill: parent; margins: Tokens.padding.extraLarge; bottomMargin: 0 }
                    // Without solwindd there is nothing to show or change (only the settings page's quit / report)
                    visible: Daemon.connected || root.tab === 2
                    currentIndex: root.tab
                    FanPage {}
                    KeyboardPage {}
                    SettingsPage {}
                }
                ColumnLayout {
                    visible: !Daemon.connected && root.tab !== 2
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 2 * Tokens.padding.extraLarge, 520)
                    spacing: Tokens.spacing.medium
                    MaterialIcon {
                        Layout.alignment: Qt.AlignHCenter
                        text: "cloud_off"
                        size: Tokens.font.icon.extraLarge
                        color: Colours.fgDim
                    }
                    StyledText {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        text: Daemon.downText
                        color: Colours.fg
                        font: Tokens.font.title.small
                    }
                    StyledText {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        text: I18n.t("It reads the sensors and drives the fans and the keyboard light; this window only shows and changes its settings. It shows up here as soon as it runs.")
                        color: Colours.fgDim
                        font: Tokens.font.body.medium
                    }
                }
            }
        }
    }
    // Closing the window keeps the app running: free the window's memory, and say so in the log (shown in the terminal
    // it was started from)
    onShownChanged: {
        if (shown) return
        gcTimer.restart()
        console.info("[solwind] Window closed; Solwind keeps running in the background. "
                     + "To quit: the tray menu, `solwind quit`, or Ctrl+C in the terminal that started it.")
    }

    Timer { id: gcTimer; interval: 1000; onTriggered: gc() }

    // What this UI shows, so solwindd reads as often as it needs (it also reads the sensors while it drives fans or
    // notifications are on, and fast while it drives fans): the fan page, and a window at all
    Binding { target: Sensors; property: "active"; value: root.shown && root.tab === 0 }
    Binding { target: Nbfc; property: "uiVisible"; value: root.shown }

    // Tray (Caelestia bar): left click shows/hides the window, the menu has profiles and max fan
    Binding { target: Tray; property: "windowVisible"; value: root.shown }
    Binding { target: Tray; property: "profiles"; value: FanState.allProfiles().map(p => FanState.displayName(p)) }
    Binding { target: Tray; property: "active"; value: FanState.activeName() }
    Binding {
        target: Tray; property: "labels"
        value: ({ show: I18n.t("Show window"), hide: I18n.t("Hide window"), boost: I18n.t("Max fan (15 min)"),
                  boostOff: I18n.t("Turn max fan off"), quit: I18n.t("Quit") })
    }
    Binding { target: Tray; property: "boost"; value: Nbfc.boosting }
    Binding {
        target: Tray; property: "tooltip"
        value: (FanState.activeName() || I18n.t("Custom settings"))
               + (isFinite(Nbfc.temp) ? " · CPU " + Math.round(Nbfc.temp) + "°C" : "")
               + (isFinite(Sensors.gpu) ? " · GPU " + Math.round(Sensors.gpu) + "°C" : "")
               + (Nbfc.boosting ? " · " + I18n.t("Max fan") : "")
    }
    Connections {
        target: Tray
        function onToggleRequested() { root.shown = !root.shown }
        function onProfileRequested(name) { FanState.applyByName(name, false) }
    }

    // Terminal control: solwind <command> (= qs ipc -p <app folder> call cc <command> [argument]).
    // A failure is answered with "ERROR<tab>message" (exit code 1) or, for a wrong argument, "USAGE<tab>message"
    // (exit code 2); bin/solwind prints the message to stderr. The work is done by solwindd: its answer (or its refusal,
    // solwind.Error.Usage = a wrong argument) is passed on.
    function fail(msg) { return "ERROR\t" + msg }
    function badArg(msg) { return "USAGE\t" + msg }
    function relay(r) { return r.ok ? String(r.value ?? "") : r.errorName === "solwind.Error.Usage" ? badArg(r.error) : fail(r.error) }
    function daemonText(obj, method) { return relay(Daemon.call(obj, method)) }
    IpcHandler {
        target: "cc"
        function toggle(): void { root.shown = !root.shown }
        // Open the window on a page (0 fan control, 1 keyboard, 2 settings); used by the UI preview
        function tab(i: int): void { root.tab = Math.max(0, Math.min(2, i)); root.shown = true }
        function open(): void { root.shown = true }
        function hide(): void { root.shown = false }
        // Quit completely: fans driven by Solwind go back to NBFC's automatic control
        function quit(): void { Nbfc.releaseAndQuit() }
        // Applies a profile (same code as the button in the UI). Case-insensitive.
        function profile(name: string): string {
            const r = FanState.applyByNameResult(name, true)
            return r.ok ? r.text : r.usage ? root.badArg(r.text) : root.fail(r.text)
        }
        function currentProfile(): string { return FanState.activeName() || I18n.t("custom settings") }
        // Max fan: minutes (0 = off, -1 = until turned off)
        function boost(minutes: int): string {
            const r = Daemon.call("fans", "SetBoost", "x", [minutes])
            if (!r.ok) return root.relay(r)
            return minutes === 0 ? I18n.t("Max fan off")
                 : minutes > 0 ? I18n.t("Max fan on (%1 min)").arg(minutes) : I18n.t("Max fan on (until turned off)")
        }
        function boostStatus(): string {
            if (!Nbfc.boosting) return I18n.t("Max fan off")
            if (Nbfc.boostUntil < 0) return I18n.t("Max fan on (until turned off)")
            return I18n.t("Max fan on (%1 min)").arg(Math.max(1, Math.ceil((Nbfc.boostUntil - Date.now()) / 60000)))
        }
        function profiles(): string { return root.daemonText("fans", "ProfileList") }
        // UI language: tr | en (the answer in the new language: the daemon's comes back a moment later)
        function lang(id: string): string {
            if (I18n.languages.findIndex(l => l.id === id) < 0) return root.badArg("tr | en")
            const r = Daemon.call("daemon", "SetLanguage", "s", [id])
            if (!r.ok) return root.relay(r)
            return I18n.tIn(id, "Language: %1").arg(I18n.languages.find(l => l.id === id).name)
        }
        function currentLang(): string { return I18n.t("Language: %1").arg(I18n.languages.find(l => l.id === I18n.lang)?.name ?? I18n.lang) }
        // Keyboard light: colour "#rrggbb", brightness in percent (0-100)
        function kbd(): string { return root.daemonText("keyboard", "StatusText") }
        // Detection report (laptop, driver, light, permission, support level): handy for bug reports
        function kbdInfo(): string { return root.daemonText("keyboard", "InfoText") }
        function kbdColor(hex: string): string { return root.relay(Daemon.call("keyboard", "SetColor", "s", [hex])) }
        function kbdEffect(name: string): string { return root.relay(Daemon.call("keyboard", "SetEffect", "s", [name])) }
        function kbdSource(name: string): string { return root.relay(Daemon.call("keyboard", "SetSource", "s", [name])) }
        function kbdSpeed(percent: int): string { return root.relay(Daemon.call("keyboard", "SetSpeed", "i", [percent])) }
        // Number of colours for the Caelestia source (3-6)
        function kbdThemeCount(n: int): string { return root.relay(Daemon.call("keyboard", "SetThemeCount", "i", [n])) }
        // Lowest and highest breathing level (percent)
        function kbdRange(low: int, high: int): string { return root.relay(Daemon.call("keyboard", "SetRange", "ii", [low, high])) }
        function kbdBrightness(percent: int): string { return root.relay(Daemon.call("keyboard", "SetBrightness", "i", [percent])) }
        function status(): string { return root.daemonText("fans", "StatusText") }
    }
}
