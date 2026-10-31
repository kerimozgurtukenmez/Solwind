import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.components
import qs.services

// Keyboard light. solwindd finds and drives the light; services/Keyboard.qml passes its state and the changes on;
// this is only the view.
// The page adapts to what was detected. Wide window: the settings of the selected effect with the brightness below
// (a slider, or level buttons when the light has only a few levels), the effect list next to them (effects only on
// tested drivers), and the detection summary in a panel on the right that folds away. Narrow window: all of it in one
// column, the summary on top.
PageBase {
    id: page
    adaptive: true
    title: wide ? "" : I18n.t("Keyboard")
    sub: !wide && Keyboard.available ? Keyboard.name : ""

    readonly property bool canWrite: Keyboard.available && Keyboard.writable
    readonly property bool breathing: Keyboard.effect === "breathing"
    readonly property bool animated: Keyboard.effectsAllowed && Keyboard.effect !== "static"     // breathing or colour cycle
    // Colour source options (Caelestia has its own switch). A colour cycle needs more than one colour: no choice there.
    readonly property var sources: [{ id: "single", t: I18n.t("Chosen color") }, { id: "multi", t: I18n.t("Several") }]
    // Effect list (new effects are added here)
    readonly property var effects: [
        { id: "static", name: I18n.t("Static"), icon: "lightbulb", sub: I18n.t("One color, always on") },
        { id: "breathing", name: I18n.t("Breathing"), icon: "airwave", sub: I18n.t("Slowly fades in and out") },
        { id: "cycle", name: I18n.t("Color cycle"), icon: "gradient", sub: I18n.t("Flows between colors") }
    ]
    readonly property var current: effects.find(e => e.id === Keyboard.effect) ?? effects[0]
    // Preset colours; the first is the Caelestia theme's primary colour (names are not shown, no translation needed)
    readonly property var presets: [
        { name: "Caelestia", c: Colours.primary },
        { name: "White", c: "#ffffff" }, { name: "Red", c: "#ff0000" }, { name: "Orange", c: "#ff6000" },
        { name: "Yellow", c: "#ffd000" }, { name: "Green", c: "#00ff00" }, { name: "Cyan", c: "#00ffff" },
        { name: "Blue", c: "#0040ff" }, { name: "Purple", c: "#8000ff" }, { name: "Pink", c: "#ff00a0" }
    ]

    // With "several colours" selected, the colour wheel edits the selected colour of the list
    property int editIndex: 0
    readonly property bool editingList: animated && Keyboard.source === "multi"
    readonly property bool themeLocked: animated && Keyboard.source === "theme"
    readonly property int editAt: Math.min(editIndex, Keyboard.colors.length - 1)
    readonly property color wheelColor: editingList ? Keyboard.colors[editAt] : Keyboard.color
    function pick(c) {
        if (!editingList) { Keyboard.setColor(c); return }
        const a = Keyboard.colors.slice()
        a[editAt] = Keyboard.hex(Qt.lighter(c, 1))
        Keyboard.setColors(a)
    }
    function pct(v) { return I18n.t("%1%").arg(Math.round(v * 100)) }

    // The Caelestia switch: off goes back to a colour of your own
    function setTheme(on) { Keyboard.setSource(on ? "theme" : breathing ? "single" : "multi") }

    // The summary panel: open if there is room, unless folded or opened by hand. What needs you stays in sight.
    property var infoChoice: null
    readonly property bool infoNeeded: Keyboard.ready && (!page.canWrite || Keyboard.error !== "")
    readonly property bool infoRoom: width >= 840
    readonly property bool infoOpen: infoNeeded || (infoChoice ?? infoRoom)
    // No room for it next to the settings while it has to stay open: it goes above them
    readonly property bool infoInSide: wide && (infoRoom || !infoNeeded)

    side: [
        ColumnLayout {
            visible: Keyboard.rgb && Keyboard.effectsAllowed
            enabled: page.canWrite
            opacity: enabled ? 1 : 0.4
            Layout.fillWidth: false
            Layout.preferredWidth: 240
            Layout.fillHeight: true
            spacing: Tokens.spacing.large
            EffectsPanel { page: page; pushCaelestiaDown: true }
        },
        ColumnLayout {
            visible: page.infoInSide
            Layout.fillWidth: false
            Layout.preferredWidth: page.infoOpen ? 270 : implicitWidth
            Layout.fillHeight: true
            spacing: Tokens.spacing.small
            IconBtn {
                visible: !page.infoNeeded
                Layout.alignment: page.infoOpen ? Qt.AlignRight : Qt.AlignHCenter
                icon: page.infoOpen ? "arrow_forward" : "arrow_back"
                onClicked: page.infoChoice = !page.infoOpen
            }
            Flickable {
                visible: page.infoOpen
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: width
                contentHeight: info.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                KeyboardStatus { id: info; width: parent.width; effectName: page.current.name }
            }
            Item { visible: !page.infoOpen; Layout.fillHeight: true }
        }
    ]

    // Read the current state when the window opens (may have changed via Fn keys)
    Component.onCompleted: Keyboard.refresh()

    ColumnLayout {
        id: col
        Layout.fillWidth: true
        spacing: Tokens.spacing.largeIncreased

        // ---------- status ----------
        KeyboardStatus { visible: !page.infoInSide; effectName: page.current.name }

        // ---------- effect: the list, then the settings of the selected one ----------
        ColumnLayout {
            visible: Keyboard.rgb
            Layout.fillWidth: true
            spacing: Tokens.spacing.largeIncreased
            enabled: page.canWrite
            opacity: enabled ? 1 : 0.4

            EffectsPanel { visible: !page.wide && Keyboard.effectsAllowed; page: page }

            // Settings of the selected effect
            Card {
                Layout.alignment: Qt.AlignTop
                title: Keyboard.effectsAllowed ? I18n.t("Settings · %1").arg(page.current.name) : I18n.t("Color")

                // Breathing / colour cycle settings
                ColumnLayout {
                    visible: page.animated
                    Layout.fillWidth: true
                    spacing: 10

                    StyledText { text: I18n.t("Colors"); color: Colours.fg; font: Tokens.font.body.small }
                    Segment {
                        visible: page.breathing && Keyboard.source !== "theme"
                        model: page.sources.map(x => x.t)
                        controlled: true
                        current: page.sources.findIndex(x => x.id === Keyboard.source)
                        onPicked: i => Keyboard.setSource(page.sources[i].id)
                    }

                    // Colour list (several colours) or theme colours; the one currently shown has a ring
                    RowLayout {
                        visible: Keyboard.source !== "single"
                        Layout.fillWidth: true
                        spacing: 8
                        Repeater {
                            model: Keyboard.source === "theme" ? Keyboard.themeColors : Keyboard.colors
                            Rectangle {
                                required property var modelData
                                required property int index
                                readonly property bool chosen: page.editingList && index === page.editAt
                                readonly property bool live: index === Keyboard.cycleIndex
                                width: 34; height: 34; radius: 17
                                color: modelData
                                border { width: chosen ? 3 : (live ? 2 : 1); color: chosen ? Colours.fg : (live ? Colours.primary : Colours.outline) }
                                MouseArea {
                                    anchors.fill: parent
                                    enabled: page.editingList
                                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                    onClicked: page.editIndex = parent.index
                                }
                            }
                        }
                        // Several colours: add/remove a colour (max 6). Caelestia: number of colours 3-6
                        StepBtn {
                            // Caelestia: hide when the theme has no further distinct colour to add (no accent colours)
                            visible: page.editingList ? Keyboard.colors.length < 6
                                   : (page.themeLocked && Keyboard.themeCount < 6 && Keyboard.themeColors.length >= Keyboard.themeCount)
                            icon: "add"
                            onActivated: {
                                if (page.themeLocked) { Keyboard.setThemeCount(Keyboard.themeCount + 1); return }
                                const a = Keyboard.colors.slice()
                                a.push(Keyboard.hex(page.wheelColor))
                                Keyboard.setColors(a)
                                page.editIndex = a.length - 1
                            }
                        }
                        StepBtn {
                            visible: page.editingList ? Keyboard.colors.length > (page.breathing ? 1 : 2) : (page.themeLocked && Keyboard.themeCount > 3)
                            icon: "remove"
                            onActivated: {
                                if (page.themeLocked) { Keyboard.setThemeCount(Keyboard.themeCount - 1); return }
                                const a = Keyboard.colors.slice()
                                a.splice(page.editAt, 1)
                                Keyboard.setColors(a)
                                page.editIndex = Math.max(0, Math.min(page.editIndex, a.length - 1))
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }
                    StyledText {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: Keyboard.source === "theme"
                            ? I18n.t("Your Caelestia theme's colors (the primary color + the theme colors whose hue differs most; saturation is raised so they look vivid on the LEDs). + / − changes the number of colors (3-6). Updates by itself when the theme changes.")
                            : Keyboard.source === "multi"
                            ? (page.breathing ? I18n.t("Each breath uses the next color; the color changes when the light is darkest.") : I18n.t("The colors flow into each other in turn, without blinking."))
                              + " " + I18n.t("Click a color and change it with the wheel below. + adds one, − removes the selected one (max 6).")
                            : I18n.t("Your chosen color fades in and out. Pick the color from the wheel below.")
                        color: Colours.fgDim
                        font: Tokens.font.label.small
                    }

                    SliderRow {
                        id: speedRow
                        flat: true
                        label: I18n.t("Speed")
                        value: Keyboard.speed
                        readout: I18n.t(page.breathing ? "%1 s per breath" : "%1 s per color").arg(I18n.num(Keyboard.period, 1))
                        onMoved: v => Keyboard.setSpeed(v)
                    }
                    SliderRow {
                        id: lowRow
                        flat: true
                        visible: page.breathing
                        label: I18n.t("Lowest")
                        value: Keyboard.minLevel
                        readout: Keyboard.minLevel === 0 ? I18n.t("fully off") : page.pct(Keyboard.minLevel)
                        onMoved: v => Keyboard.setRange(v, Keyboard.maxLevel)
                    }
                    SliderRow {
                        id: highRow
                        flat: true
                        visible: page.breathing
                        label: I18n.t("Highest")
                        value: Keyboard.maxLevel
                        readout: page.pct(Keyboard.maxLevel)
                        onMoved: v => Keyboard.setRange(Keyboard.minLevel, v)
                    }
                    // Sliders lose their binding once dragged; resync when the value changes elsewhere (the other slider pushed it, terminal)
                    Connections {
                        target: Keyboard
                        function onMinLevelChanged() { lowRow.value = Keyboard.minLevel }
                        function onMaxLevelChanged() { highRow.value = Keyboard.maxLevel }
                        function onSpeedChanged() { speedRow.value = Keyboard.speed }
                    }
                    StyledText {
                        visible: page.breathing
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: I18n.t("Levels are relative to the overall brightness below. Unless the lowest is 0%, the light never goes fully off.")
                        color: Colours.fgDim
                        font: Tokens.font.label.small
                    }

                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Colours.outline; opacity: 0.5 }
                }

                // Description for the static effect
                StyledText {
                    visible: !page.animated
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: I18n.t("The keyboard stays lit in your chosen color.")
                          + (Keyboard.rgb && !Keyboard.effectsAllowed ? " " + I18n.t("Effects are only enabled on keyboards they have been tested with.") : "")
                    color: Colours.fgDim
                    font: Tokens.font.label.small
                }

                // Colour picking (all effects; locked when the source is Caelestia)
                StyledText {
                    visible: Keyboard.effectsAllowed       // otherwise the card itself is titled "Color"
                    text: page.themeLocked ? I18n.t("The colors come from the Caelestia theme")
                        : page.editingList ? I18n.t("Color · color %1").arg(page.editAt + 1) : I18n.t("Color")
                    color: Colours.fg
                    font: Tokens.font.body.small
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    enabled: !page.themeLocked
                    opacity: enabled ? 1 : 0.4

                    ColorWheel {
                        Layout.alignment: Qt.AlignHCenter
                        hue: page.wheelColor.hsvHue < 0 ? 0 : page.wheelColor.hsvHue
                        sat: page.wheelColor.hsvSaturation
                        onPicked: (h, s) => page.pick(Qt.hsva(h, s, 1, 1))
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        Repeater {
                            model: page.presets
                            Rectangle {
                                required property var modelData
                                width: 34; height: 34; radius: 17
                                color: modelData.c
                                border { width: 2; color: Colours.outline }
                                MaterialIcon {
                                    visible: parent.modelData.name === "Caelestia"
                                    anchors.centerIn: parent
                                    text: "palette"
                                    color: Colours.fgOnPrimary
                                    size: Tokens.font.icon.small
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: page.pick(parent.modelData.c)
                                }
                            }
                        }
                    }
                }
            }

        }

        // ---------- overall brightness ----------
        Group {
            id: brightCard
            visible: Keyboard.hasBrightness
            title: I18n.t("Brightness")
            enabled: page.canWrite
            opacity: enabled ? 1 : 0.4

            // Lights with only a few levels (ASUS 0-3, Lenovo 0-1/0-2) get one button per level instead of a slider
            readonly property bool stepped: Keyboard.maxBrightness <= 3
            SliderRow {
                id: bright
                visible: !brightCard.stepped
                label: I18n.t("Level")
                value: Keyboard.maxBrightness > 0 ? Keyboard.brightness / Keyboard.maxBrightness : 0
                icon: "brightness_6"
                readout: page.pct(value)
                onMoved: v => Keyboard.setBrightness(v * Keyboard.maxBrightness)
            }
            // The slider loses its binding once dragged; resync when brightness changes elsewhere (buttons, terminal)
            Connections {
                target: Keyboard
                function onBrightnessChanged() {
                    if (Keyboard.maxBrightness > 0) bright.value = Keyboard.brightness / Keyboard.maxBrightness
                }
            }
            GroupItem {
                RowLayout {
                    visible: !brightCard.stepped
                    Layout.fillWidth: true
                    spacing: Tokens.spacing.small
                    Repeater {
                        model: [{ t: I18n.t("Off"), v: 0 }, { t: page.pct(0.25), v: 0.25 }, { t: page.pct(0.5), v: 0.5 }, { t: page.pct(1), v: 1 }]
                        Btn {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1      // equal widths
                            text: modelData.t
                            onClicked: Keyboard.setBrightness(modelData.v * Keyboard.maxBrightness)
                        }
                    }
                }
                RowLayout {
                    visible: brightCard.stepped
                    Layout.fillWidth: true
                    spacing: Tokens.spacing.small
                    Repeater {
                        // 1 level: Off/On; 2: Off/Low/High; 3: Off/Low/Medium/High
                        model: Keyboard.maxBrightness === 1 ? [I18n.t("Off"), I18n.t("On")]
                             : Keyboard.maxBrightness === 2 ? [I18n.t("Off"), I18n.t("Low"), I18n.t("High")]
                             : [I18n.t("Off"), I18n.t("Low"), I18n.t("Medium"), I18n.t("High")]
                        Btn {
                            required property string modelData
                            required property int index
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1      // equal widths
                            text: modelData
                            filled: Keyboard.brightness === index
                            onClicked: Keyboard.setBrightness(index)
                        }
                    }
                }
            }
        }
    }
}
