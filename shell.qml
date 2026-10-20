//@ pragma AppId ccenter
import Quickshell
import Quickshell.Io
import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.components
import qs.modules.fan
import qs.modules.keyboard
import qs.services

ShellRoot {
    id: root
    // The window state lives outside the window: closing the window destroys it completely
    // (UI, graphics context and render caches are freed; fan control keeps running in FanState)
    // Started by the service (CCENTER_BACKGROUND=1) it runs in the background without opening the window
    property bool shown: Quickshell.env("CCENTER_BACKGROUND") !== "1"
    property int tab: 0

    LazyLoader {
        active: root.shown
        // FloatingWindow = a normal xdg window (Hyprland tiles it). PanelWindow is layer-shell and doesn't tile.
        FloatingWindow {
            title: "Ccenter-Caelestia"
            visible: true
            // Closing the window doesn't quit the app; it keeps running in the background (reopen with `ccenter`)
            onClosed: root.shown = false
            implicitWidth: 600
            implicitHeight: 780
            color: Colours.surface
            // Caelestia's tokens are per screen (rounding, spacing etc.)
            contentItem.Tokens.screen: screen?.name ?? ""

            ColumnLayout {
                anchors { fill: parent; margins: Tokens.padding.largeIncreased }
                spacing: Tokens.spacing.large

                RowLayout {
                    Layout.fillWidth: true
                    StyledText { text: "Ccenter"; color: Colours.fg; font { pixelSize: 26; bold: true } }
                    StyledText { text: "-Caelestia"; color: Colours.primary; font { pixelSize: 26; bold: true } }
                    Item { Layout.fillWidth: true }
                    // Language switch (saved in settings; default follows the system language)
                    Segment {
                        Layout.fillWidth: false
                        implicitWidth: 120
                        model: I18n.languages.map(l => l.id.toUpperCase())
                        controlled: true
                        current: I18n.languages.findIndex(l => l.id === I18n.lang)
                        onPicked: i => I18n.setLang(I18n.languages[i].id)
                    }
                }

                RowLayout {
                    spacing: Tokens.spacing.medium
                    NavButton {
                        icon: "mode_fan"; label: I18n.t("Fan control"); sub: I18n.t("NBFC · speed and curves")
                        active: root.tab === 0; onClicked: root.tab = 0
                    }
                    NavButton {
                        icon: "keyboard"; label: I18n.t("Keyboard"); sub: I18n.t("RGB · color and brightness")
                        active: root.tab === 1; onClicked: root.tab = 1
                    }
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: root.tab
                    FanPage {}
                    KeyboardPage {}
                }
            }
        }
    }
    onShownChanged: if (!shown) gcTimer.restart()

    Timer { id: gcTimer; interval: 1000; onTriggered: gc() }

    // Sensors are read while the fan tab is open, while Ccenter drives fans, or while notifications are on (critical temperature)
    Binding { target: Sensors; property: "active"; value: (root.shown && root.tab === 0) || Nbfc.needsSensors || Notify.enabled }
    // Always-running services (also while the window is closed): event watcher and fan state
    readonly property var alerts: Alerts
    readonly property var fanState: FanState
    readonly property var keyboard: Keyboard     // keyboard effects keep running while the window is closed
    // Polling pace: fast while the window is open or Ccenter drives fans, otherwise relaxed (performance)
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

    // Terminal control: ccenter <command> (= qs ipc -p <app folder> call cc <command> [argument]).
    // A failure is answered with "ERROR<tab>message" (exit code 1) or, for a wrong argument, "USAGE<tab>message"
    // (exit code 2); bin/ccenter prints the message to stderr.
    function fail(msg) { return "ERROR\t" + msg }
    function badArg(msg) { return "USAGE\t" + msg }
    IpcHandler {
        target: "cc"
        function toggle(): void { root.shown = !root.shown }
        function open(): void { root.shown = true }
        function hide(): void { root.shown = false }
        // Quit completely: fans driven by Ccenter go back to NBFC's automatic control
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
