import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.components
import qs.services

Card {
    id: fan
    property string name: "Fan"
    property string subtitle: ""
    property bool isGlobal: false
    property int fanIndex: 0
    property bool active: true               // false on the per-fan cards while Global is on
    property var info: null                  // Nbfc.fans[i] (null on the Global card)
    // The card is a view of FanState: settings come from there, changes go there (the card is destroyed with the window)
    readonly property var source: isGlobal ? FanState.globalSt : (FanState.fanSt[fanIndex] ?? FanState.autoSt)
    property int mode: 0
    property bool smoothCurve: true
    property string src: "cpu"               // temperature followed in Curve mode: cpu | gpu | max
    readonly property var srcKeys: ["cpu", "gpu", "max"]
    property int hyst: 3                     // Curve: how many degrees the temperature must drop before slowing down
    property var curve: [{ t: 45, s: 20 }, { t: 55, s: 40 }, { t: 65, s: 60 }, { t: 75, s: 80 }, { t: 85, s: 100 }]
    readonly property bool allowed: isGlobal ? FanState.globalOn : active
    // mode: 0 Auto, 1 Fixed, 2 Curve
    readonly property var st: ({ mode: fan.mode, fixed: fixedRow.value, curve: fan.curve, smooth: fan.smoothCurve, src: fan.src, hyst: fan.hyst })

    // The user changed something: wait 400 ms, then write it to FanState once
    function touch() { debounce.restart() }
    Timer {
        id: debounce
        interval: 400
        onTriggered: fan.isGlobal ? FanState.setGlobal(fan.st) : FanState.setFan(fan.fanIndex, fan.st)
    }
    // When FanState changes (startup, profile, terminal), sync the card to it
    function sync() {
        const s = source
        if (!s) return
        curve = s.curve
        smoothCurve = s.smooth
        src = s.src
        hyst = s.hyst
        fixedRow.value = s.fixed
        mode = s.mode
    }
    onSourceChanged: if (!debounce.running) sync()
    // The card is destroyed when the window closes: don't lose a pending (400 ms) change, write it to FanState now
    Component.onDestruction: if (debounce.running) {
        debounce.stop()
        fan.isGlobal ? FanState.setGlobal(fan.st) : FanState.setFan(fan.fanIndex, fan.st)
    }
    Component.onCompleted: sync()

    RowLayout {
        Layout.fillWidth: true
        spacing: 10
        MaterialIcon { text: "mode_fan"; color: Colours.primary; size: Tokens.font.icon.medium }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            StyledText { text: fan.name; color: Colours.fg; font: Tokens.font.title.small }
            StyledText { Layout.fillWidth: true; text: fan.subtitle; elide: Text.ElideRight; color: Colours.fgDim; font: Tokens.font.label.small }
        }
        ColumnLayout {
            visible: !fan.isGlobal
            spacing: 0
            StyledText {
                Layout.alignment: Qt.AlignRight
                text: fan.info ? Math.round(fan.info.current) + "%" : "—"
                color: Colours.primary
                font: Tokens.font.label.large
            }
            // Real RPM: only if the driver reports trustworthy values (always 0 on some models)
            StyledText {
                readonly property real rpm: Sensors.rpm(fan.fanIndex)
                visible: isFinite(rpm)
                Layout.alignment: Qt.AlignRight
                text: Math.round(rpm) + " RPM"
                color: Colours.fgDim
                font: Tokens.font.label.small
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 12
        enabled: fan.allowed
        opacity: fan.allowed ? 1 : 0.35
        Behavior on opacity { Anim {} }

        Segment {
            model: [I18n.t("Auto"), I18n.t("Fixed"), I18n.t("Curve")]
            controlled: true
            current: fan.mode
            onPicked: i => { fan.mode = i; fan.touch() }
        }

        // Auto: the NBFC config's own curve (read-only) + copy it into your own curve
        readonly property var cfgCurve: Nbfc.configCurves[fan.isGlobal ? 0 : fan.fanIndex] ?? null
        StyledText {
            visible: fan.mode === 0
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: parent.cfgCurve
                ? I18n.t("Uses the NBFC config's curve") + (fan.isGlobal ? " " + I18n.t("(showing Fan 1's)") : "") + " · " + I18n.t("slow-down ~%1°C").arg(parent.cfgCurve.hyst)
                : (Nbfc.configCurves.length > 0 ? I18n.t("The config defines no threshold table; NBFC uses its own default.") : I18n.t("Uses the curve from the config."))
            color: Colours.fgDim
            font: Tokens.font.label.small
        }
        CurveEditor {
            visible: fan.mode === 0 && parent.cfgCurve !== null
            Layout.fillWidth: true
            readOnly: true
            points: parent.cfgCurve ? parent.cfgCurve.points : []
            smoothCurve: false
            liveTemp: Nbfc.sourceTemp(fan.isGlobal ? 0 : fan.fanIndex, "cpu")
        }
        Btn {
            visible: fan.mode === 0 && parent.cfgCurve !== null
            Layout.fillWidth: true
            icon: "edit"
            text: I18n.t("Edit this curve")
            onClicked: {
                const c = parent.cfgCurve
                fan.curve = c.points.map(p => ({ t: p.t, s: p.s }))
                fan.smoothCurve = false
                fan.hyst = c.hyst
                fan.src = "cpu"
                fan.mode = 2
                fan.touch()
            }
        }

        SliderRow {
            id: fixedRow
            visible: fan.mode === 1
            flat: true
            label: I18n.t("Fixed speed")
            value: 0.5
            readout: Math.round(value * 100) + "%"
            onMoved: fan.touch()
        }

        // Temperature source (only meaningful with a discrete GPU)
        RowLayout {
            visible: fan.mode === 2 && Sensors.hasGpu
            Layout.fillWidth: true
            spacing: 10
            StyledText { text: I18n.t("Temperature"); color: Colours.fgDim; font: Tokens.font.body.small }
            Segment {
                controlled: true
                model: ["CPU", "GPU", I18n.t("Hottest")]
                current: Math.max(0, fan.srcKeys.indexOf(fan.src))
                onPicked: i => { fan.src = fan.srcKeys[i]; fan.touch() }
            }
        }

        CurveEditor {
            visible: fan.mode === 2
            Layout.fillWidth: true
            points: fan.curve
            smoothCurve: fan.smoothCurve
            liveTemp: fan.isGlobal ? Nbfc.sourceTemp(0, fan.src) : Nbfc.sourceTemp(fan.fanIndex, fan.src)
            onEdited: pts => { fan.curve = pts; fan.touch() }
            onSmoothPicked: v => { fan.smoothCurve = v; fan.touch() }
        }

        Stepper {
            visible: fan.mode === 2
            Layout.fillWidth: true
            label: I18n.t("Slow-down delay")
            value: fan.hyst
            from: 0
            to: 10
            unit: "°C"
            onMoved: v => { fan.hyst = v; fan.touch() }
        }
    }
}
