import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.components
import qs.services

// Keyboard light. Logic and hardware detection live in services/Keyboard.qml; this is only the view.
// The page adapts to what was detected: a detection summary on top, then (if the light has colour) the settings of
// the selected effect on the left and the effect list on the right (effects only on tested drivers), brightness below
// (a slider, or level buttons when the light has only a few levels).
PageBase {
    id: page
    title: I18n.t("Keyboard")
    sub: Keyboard.available ? Keyboard.name : ""

    readonly property bool canWrite: Keyboard.available && Keyboard.writable
    readonly property bool breathing: Keyboard.effect === "breathing"
    readonly property bool animated: Keyboard.effectsAllowed && Keyboard.effect !== "static"     // breathing or colour cycle
    // Colour source options: no "chosen colour" for the colour cycle (a cycle needs more than one colour)
    readonly property var sources: breathing ? [{ id: "single", t: I18n.t("Chosen color") }, { id: "multi", t: I18n.t("Several") }, { id: "theme", t: "Caelestia" }]
                                             : [{ id: "multi", t: I18n.t("Several") }, { id: "theme", t: "Caelestia" }]
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

    // Read the current state when the window opens (may have changed via Fn keys)
    Component.onCompleted: Keyboard.refresh()

    ColumnLayout {
        id: col
        Layout.fillWidth: true
        spacing: Tokens.spacing.largeIncreased

        // ---------- status ----------
        Card {
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
                        text: !Keyboard.ready ? I18n.t("Reading…") : Keyboard.available ? Keyboard.name : I18n.t("No keyboard light found")
                        color: Colours.fg
                        font: Tokens.font.title.small
                    }
                    StyledText {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: !Keyboard.available ? I18n.t("This computer has no keyboard light the kernel recognizes.")
                            : Keyboard.kindText() + (Keyboard.effectsAllowed ? " · " + I18n.t("effect: %1").arg(page.current.name) : "")
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
                            elide: Text.ElideRight
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
            RowLayout {
                visible: Keyboard.writable && (Keyboard.ruleLegacy || Keyboard.ruleOld)
                Layout.fillWidth: true
                spacing: 8
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
            RowLayout {
                visible: Keyboard.writable && Keyboard.ruleAt === "etc" && !Keyboard.ruleLegacy && !Keyboard.ruleOld
                Layout.fillWidth: true
                spacing: 8
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

        // ---------- effect: the list, then the settings of the selected one ----------
        ColumnLayout {
            visible: Keyboard.rgb
            Layout.fillWidth: true
            spacing: Tokens.spacing.largeIncreased
            enabled: page.canWrite
            opacity: enabled ? 1 : 0.4

            // Effect list: clicking an effect activates it
            Group {
                title: I18n.t("Effects")
                visible: Keyboard.effectsAllowed
                Repeater {
                    model: page.effects
                    NavItem {
                        required property var modelData
                        required property int index
                        icon: modelData.icon
                        label: modelData.name
                        sub: modelData.sub
                        selected: Keyboard.effect === modelData.id
                        first: index === 0
                        last: index === page.effects.length - 1
                        onClicked: Keyboard.setEffect(modelData.id)
                    }
                }
            }

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
