//@ pragma AppId solwind
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
    // (UI, graphics context and render caches are freed; fan control keeps running in FanState)
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
                    currentIndex: root.tab
                    FanPage {}
                    KeyboardPage {}
                    SettingsPage {}
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

    // Sensors are read while the fan tab is open, while Solwind drives fans, or while notifications are on (critical temperature)
    Binding { target: Sensors; property: "active"; value: (root.shown && root.tab === 0) || Nbfc.needsSensors || Notify.enabled }
    // Always-running services (also while the window is closed): event watcher and fan state
    readonly property var alerts: Alerts
    readonly property var fanState: FanState
    readonly property var keyboard: Keyboard     // keyboard effects keep running while the window is closed
    // Polling pace: fast while the window is open or Solwind drives fans, otherwise relaxed (performance)
    Binding { target: Nbfc; property: "uiVisible"; value: root.shown }
    Binding { target: Sensors; property: "fast"; value: root.shown || Nbfc.controlling }

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
    // (exit code 2); bin/solwind prints the message to stderr.
    function fail(msg) { return "ERROR\t" + msg }
    function badArg(msg) { return "USAGE\t" + msg }
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
            if (!Nbfc.setBoost(minutes)) return root.fail(Nbfc.boostBlocked())
            return minutes === 0 ? I18n.t("Max fan off")
                 : minutes > 0 ? I18n.t("Max fan on (%1 min)").arg(minutes) : I18n.t("Max fan on (until turned off)")
        }
        function boostStatus(): string {
            if (!Nbfc.boosting) return I18n.t("Max fan off")
            if (Nbfc.boostUntil < 0) return I18n.t("Max fan on (until turned off)")
            return I18n.t("Max fan on (%1 min)").arg(Math.max(1, Math.ceil((Nbfc.boostUntil - Date.now()) / 60000)))
        }
        function profiles(): string { return FanState.profileList() }
        // UI language: tr | en
        function lang(id: string): string {
            if (I18n.languages.findIndex(l => l.id === id) < 0) return root.badArg("tr | en")
            I18n.setLang(id)
            return I18n.t("Language: %1").arg(I18n.languages.find(l => l.id === id).name)
        }
        function currentLang(): string { return I18n.t("Language: %1").arg(I18n.languages.find(l => l.id === I18n.lang)?.name ?? I18n.lang) }
        // Keyboard light: colour "#rrggbb", brightness in percent (0-100)
        function kbd(): string { return Keyboard.statusText() }
        // Detection report (laptop, driver, light, permission, support level): handy for bug reports
        function kbdInfo(): string { return Keyboard.infoText() }
        function kbdColor(hex: string): string {
            if (!Keyboard.available) return root.fail(I18n.t("No keyboard light found"))
            if (!Keyboard.rgb) return root.fail(I18n.t("This keyboard light is not RGB"))
            if (!/^#?[0-9a-fA-F]{6}$/.test(hex)) return root.badArg(I18n.t("The color must look like #rrggbb (e.g. #ff0000)"))
            if (!Keyboard.writable) { Keyboard.refresh(); return root.fail(Keyboard.noPermissionText()) }   // (it may have been granted meanwhile)
            Keyboard.setColor(hex.startsWith("#") ? hex : "#" + hex)
            return I18n.t("Color: %1").arg(Keyboard.hex(Keyboard.color))
        }
        function kbdEffect(name: string): string {
            if (["static", "breathing", "cycle"].indexOf(name) < 0) return root.badArg(I18n.t("Effect: %1").arg("static | breathing | cycle"))
            if (!Keyboard.effectsAllowed) return root.fail(I18n.t("Effects are not available for this keyboard"))
            if (!Keyboard.writable) { Keyboard.refresh(); return root.fail(Keyboard.noPermissionText()) }   // (it may have been granted meanwhile)
            Keyboard.setEffect(name)
            return I18n.t("Effect: %1").arg(name)
        }
        function kbdSource(name: string): string {
            if (["single", "multi", "theme"].indexOf(name) < 0) return root.badArg(I18n.t("Color source: single | multi | theme"))
            if (!Keyboard.setSource(name)) return root.fail(I18n.t("A color cycle needs several colors: multi | theme"))
            return I18n.t("Color source: %1").arg(name)
        }
        function kbdSpeed(percent: int): string {
            if (!Keyboard.effectsAllowed) return root.fail(I18n.t("Effects are not available for this keyboard"))
            Keyboard.setSpeed(Math.max(0, Math.min(100, percent)) / 100)
            return I18n.t("Speed: %1% (%2 s per breath / color)").arg(Math.round(Keyboard.speed * 100)).arg(I18n.num(Keyboard.period, 1))
        }
        // Number of colours for the Caelestia source (3-6)
        function kbdThemeCount(n: int): string {
            if (!Keyboard.effectsAllowed) return root.fail(I18n.t("Effects are not available for this keyboard"))
            Keyboard.setThemeCount(n)
            return I18n.t("Caelestia colors: %1 (%2)").arg(Keyboard.themeCount).arg(Keyboard.themeColors.map(c => Keyboard.hex(c)).join(" "))
        }
        // Lowest and highest breathing level (percent)
        function kbdRange(low: int, high: int): string {
            if (!Keyboard.effectsAllowed) return root.fail(I18n.t("Effects are not available for this keyboard"))
            Keyboard.setRange(low / 100, high / 100)
            return I18n.t("Breathing range: %1% – %2%").arg(Math.round(Keyboard.minLevel * 100)).arg(Math.round(Keyboard.maxLevel * 100))
        }
        function kbdBrightness(percent: int): string {
            if (!Keyboard.available) return root.fail(I18n.t("No keyboard light found"))
            if (!Keyboard.hasBrightness) return root.fail(I18n.t("This keyboard light has no brightness setting (color only)"))
            if (!Keyboard.writable) { Keyboard.refresh(); return root.fail(Keyboard.noPermissionText()) }   // (it may have been granted meanwhile)
            Keyboard.setBrightness(Math.max(0, Math.min(100, percent)) * Keyboard.maxBrightness / 100)
            return I18n.t("Brightness: %1%").arg(Math.max(0, Math.min(100, percent)))
        }
        function status(): string { return FanState.statusText() }
    }
}
