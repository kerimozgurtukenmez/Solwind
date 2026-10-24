pragma Singleton
import Quickshell
import Quickshell.Io
import QtQuick
import Caelestia.Config
import Caelestia.Images

// Colours come from Caelestia; nothing is ever written to Caelestia's files (read-only):
//  - ~/.local/state/caelestia/scheme.json  -> colour scheme
//  - Tokens.transparency (shell.json)       -> transparency; a translucent window gets Hyprland's blur
//  - wallpaper/path.txt                     -> wallpaper luminance (to tint the layers like Caelestia does)
Singleton {
    id: root

    // Raw colours from the scheme (defaults until it is loaded)
    QtObject {
        id: raw
        property color surface: "#141218"
        property color surfaceContainer: "#211f26"
        property color surfaceContainerHigh: "#2b2930"
        property color surfaceContainerHighest: "#36343b"
        property color surfaceContainerLow: "#1d1b20"
        property color surfaceContainerLowest: "#0f0d13"
    }
    property color primary: "#d0bcff"
    property color fgOnPrimary: "#381e72"
    property color fg: "#e6e0e9"
    property color fgDim: "#cac4d0"
    property color outline: "#49454f"
    property color error: "#f2b8b5"
    property color success: "#b5ccba"
    property color warning: "#ffcc80"
    property color tertiary: "#efb8c8"
    property color secondary: "#ccc2dc"
    property color sky: "#89dceb"
    // Material 3 roles Caelestia's settings app uses (selected navigation, tonal buttons, slider track, switch)
    property color secondaryContainer: "#4a4458"
    property color fgOnSecondaryContainer: "#e8def8"
    property color fgOnSecondary: "#332d41"
    property color outlineStrong: "#938f99"     // M3 "outline" (`outline` here is M3 "outlineVariant")
    // The theme's accent colours (tinted to the wallpaper); colour pool for keyboard effects
    property var accents: []
    readonly property var accentKeys: ["mauve", "pink", "red", "maroon", "peach", "yellow", "green", "teal",
                                       "sky", "sapphire", "blue", "lavender", "flamingo", "rosewater"]
    property bool light: false
    readonly property string fontFamily: Tokens.font.body.small.family

    // Backgrounds go through the transparency layers: window (0) < card (1) < inside a card (2)
    readonly property color surface: layer(raw.surface, 0)
    readonly property color surfaceContainer: layer(raw.surfaceContainer, 1)
    readonly property color surfaceHigh: layer(raw.surfaceContainerHigh, 2)
    readonly property color surfaceHighest: layer(raw.surfaceContainerHighest, 2)   // switch track (off), inputs
    readonly property color surfaceLow: layer(raw.surfaceContainerLow, 1)           // the content panel behind the rows
    readonly property color surfaceLowest: layer(raw.surfaceContainerLowest, 1)

    // ---------- Caelestia transparency (same maths as Caelestia's services/Colours.qml, adapted; GPL-3.0) ----------
    readonly property bool trEnabled: Tokens.transparency.enabled
    readonly property real trBase: Math.max(0, Math.min(1, Tokens.transparency.base - (light ? 0.1 : 0)))
    readonly property real trLayers: Math.max(0, Math.min(1, Tokens.transparency.layers))
    readonly property real wallLuminance: wallLuminanceSafe

    function luminance(c) {
        if (c.r == 0 && c.g == 0 && c.b == 0) return 0
        return Math.sqrt(0.299 * (c.r ** 2) + 0.587 * (c.g ** 2) + 0.114 * (c.b ** 2))
    }
    function alter(c, a, layer) {
        const lum = luminance(c)
        const offset = (!light || layer == 1 ? 1 : -layer / 2) * (light ? 0.2 : 0.3) * (1 - trBase)
                     * (1 + wallLuminance * (light ? (layer == 1 ? 3 : 1) : 2.5))
        const scale = lum > 0 ? (lum + offset) / lum : 1
        return Qt.rgba(Math.max(0, Math.min(1, c.r * scale)), Math.max(0, Math.min(1, c.g * scale)),
                       Math.max(0, Math.min(1, c.b * scale)), a)
    }
    function layer(c, layer) {
        if (!trEnabled) return c
        return layer === 0 ? Qt.alpha(c, trBase) : alter(c, trLayers, layer ?? 1)
    }

    // property name -> key in scheme.json (missing keys are skipped and keep the default)
    readonly property var keys: ({
        primary: "primary", fgOnPrimary: "onPrimary", fg: "onSurface", fgDim: "onSurfaceVariant",
        outline: "outlineVariant", error: "error", success: "success", warning: "yellow", tertiary: "tertiary", secondary: "secondary", sky: "sky",
        secondaryContainer: "secondaryContainer", fgOnSecondaryContainer: "onSecondaryContainer", fgOnSecondary: "onSecondary",
        outlineStrong: "outline"
    })
    readonly property var rawKeys: ({
        surface: "surface", surfaceContainer: "surfaceContainer", surfaceContainerHigh: "surfaceContainerHigh",
        surfaceContainerHighest: "surfaceContainerHighest", surfaceContainerLow: "surfaceContainerLow",
        surfaceContainerLowest: "surfaceContainerLowest"
    })

    readonly property string stateDir: Quickshell.env("HOME") + "/.local/state/caelestia"

    FileView {
        path: root.stateDir + "/scheme.json"
        watchChanges: true
        onFileChanged: reload()
        onLoaded: {
            const s = JSON.parse(text())
            const c = s.colours
            root.light = s.mode === "light"
            for (const k in root.keys)
                if (c[root.keys[k]]) root[k] = "#" + c[root.keys[k]]
            for (const k in root.rawKeys)
                if (c[root.rawKeys[k]]) raw[k] = "#" + c[root.rawKeys[k]]
            root.accents = root.accentKeys.filter(k => c[k]).map(k => "#" + c[k])
        }
    }

    FileView {
        id: wall
        path: root.stateDir + "/wallpaper/path.txt"
        watchChanges: true
        onFileChanged: reload()
        onLoaded: root.wallpaper = text().trim()
    }
    property string wallpaper: ""

    // A video wallpaper can't be analysed; Caelestia keeps the luminance at 0 in that case as well
    readonly property bool wallIsImage: root.wallpaper !== "" && !/\.(mp4|mkv|webm|avi|mov)$/i.test(root.wallpaper)
    readonly property real wallLuminanceSafe: wallIsImage ? analyser.luminance : 0

    ImageAnalyser {
        id: analyser
        source: root.wallIsImage ? root.wallpaper : ""
    }
}
