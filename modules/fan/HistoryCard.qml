import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import QtQuick.Shapes
import qs.components
import qs.services

// Last 10 minutes. Two charts sharing the time axis:
//  - temperatures (CPU / GPU °C): 30-90 °C in 10 °C steps
//  - average fan speed (%): its own 0-100 scale below
// Hovering shows the exact values at that moment.
Card {
    id: card
    title: I18n.t("Last 10 minutes")

    // While the window is hidden samples are still recorded but the graph isn't computed
    readonly property var h: Nbfc.uiVisible ? Nbfc.history : []
    readonly property double end: h.length ? h[h.length - 1].t : Date.now()
    readonly property var last: h.length ? h[h.length - 1] : null

    // Temperature scale: fixed 30-90 °C in 10 °C steps (widened only if a value falls outside)
    readonly property int stepPx: 22                // height of one 10 °C step
    readonly property var range: {
        let lo = 30, hi = 90
        for (const s of h) {
            for (const k of ["cpu", "gpu"]) {
                const v = s[k]
                if (isFinite(v)) { lo = Math.min(lo, Math.floor(v / 10) * 10); hi = Math.max(hi, Math.ceil(v / 10) * 10) }
            }
        }
        return { lo: Math.max(0, lo), hi: hi, step: 10 }
    }
    readonly property var ticks: {
        const a = []
        for (let v = range.lo; v <= range.hi + 0.01; v += range.step) a.push(v)
        return a
    }

    function px(t) { return (t - (end - Nbfc.historyMs)) / Nbfc.historyMs * tplot.width }
    function ty(v) { return tplot.height * (1 - (Math.max(range.lo, Math.min(range.hi, v)) - range.lo) / (range.hi - range.lo)) }
    function fy(v) { return fplot.height * (1 - Math.max(0, Math.min(100, v)) / 100) }
    // Polylines for one series; the line breaks at NaN samples (e.g. GPU asleep)
    function lines(key, yOf) {
        const out = []
        let cur = []
        for (const s of h) {
            const v = s[key]
            if (isFinite(v)) cur.push(Qt.point(px(s.t), yOf(v)))
            else if (cur.length) { out.push(cur); cur = [] }
        }
        if (cur.length) out.push(cur)
        return out.filter(l => l.length > 1)
    }
    function deg(v) { return isFinite(v) ? Math.round(v) + "°C" : "—" }
    function pct(v) { return isFinite(v) ? I18n.t("%1%").arg(Math.round(v)) : "—" }

    readonly property var temps: [
        { key: "cpu", label: "CPU", color: Colours.primary, show: true },
        { key: "gpu", label: "GPU", color: Colours.sky, show: Sensors.hasGpu }
    ].filter(s => s.show)

    // Hover: the sample closest to the mouse
    property real hoverX: -1
    readonly property var hovered: {
        if (hoverX < 0 || h.length === 0) return null
        let best = null, bd = Infinity
        for (const s of h) {
            const d = Math.abs(px(s.t) - hoverX)
            if (d < bd) { bd = d; best = s }
        }
        return best
    }
    function ago(t) {
        const s = Math.max(0, Math.round((end - t) / 1000))
        return s === 0 ? I18n.t("now") : "-" + Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0")
    }

    // Legend with the current values
    RowLayout {
        Layout.fillWidth: true
        spacing: 16
        Repeater {
            model: card.temps
            RowLayout {
                required property var modelData
                spacing: 6
                Rectangle { width: 14; height: 3; radius: 1.5; color: modelData.color }
                StyledText { text: modelData.label; color: Colours.fgDim; font: Tokens.font.label.small }
                StyledText {
                    text: card.last ? card.deg(card.last[modelData.key]) : "—"
                    color: Colours.fg
                    font: Tokens.font.label.medium
                }
            }
        }
        RowLayout {
            spacing: 6
            Rectangle { width: 14; height: 3; radius: 1.5; color: Colours.fgDim }
            StyledText { text: I18n.t("Fan speed"); color: Colours.fgDim; font: Tokens.font.label.small }
            StyledText {
                text: card.last ? card.pct(card.last.fan) : "—"
                color: Colours.fg
                font: Tokens.font.label.medium
            }
        }
        Item { Layout.fillWidth: true }
        StyledText {
            visible: card.h.length < 2
            text: I18n.t("Collecting data…")
            color: Colours.fgDim
            font: Tokens.font.label.small
        }
    }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: tplot.height + 122
        radius: 14
        color: Colours.surfaceHigh

        // ---------- temperatures ----------
        Item {
            id: tplot
            anchors { left: parent.left; right: parent.right; top: parent.top; leftMargin: 44; rightMargin: 14; topMargin: 14 }
            height: (card.range.hi - card.range.lo) / card.range.step * card.stepPx

            // Vertical grid every 2 minutes (shared look with the fan chart)
            Repeater {
                model: 6
                Rectangle { x: index / 5 * tplot.width; width: 1; height: tplot.height; color: Colours.outline; opacity: 0.18 }
            }
            Repeater {
                model: card.ticks
                Item {
                    required property real modelData
                    width: tplot.width; height: 1
                    y: card.ty(modelData)
                    Rectangle { anchors.fill: parent; color: Colours.outline; opacity: 0.35 }
                    StyledText { x: -width - 8; y: -height / 2; text: parent.modelData + "°"; color: Colours.fgDim; font.pixelSize: 10 }
                }
            }
            // Safety limit, only when it falls inside the visible range
            Item {
                visible: Nbfc.safety >= card.range.lo && Nbfc.safety <= card.range.hi
                width: tplot.width; height: 1
                y: card.ty(Nbfc.safety)
                Rectangle { anchors.fill: parent; color: Colours.error; opacity: 0.7 }
                StyledText {
                    anchors { right: parent.right; bottom: parent.top; bottomMargin: 2 }
                    text: I18n.t("limit %1°C").arg(Nbfc.safety)
                    color: Colours.error
                    font.pixelSize: 9
                }
            }
            Repeater {
                model: card.temps
                Shape {
                    required property var modelData
                    anchors.fill: parent
                    preferredRendererType: Shape.CurveRenderer
                    ShapePath {
                        strokeColor: modelData.color
                        strokeWidth: 2
                        fillColor: "transparent"
                        joinStyle: ShapePath.RoundJoin
                        capStyle: ShapePath.RoundCap
                        PathMultiline { paths: card.lines(modelData.key, card.ty) }
                    }
                }
            }
        }

        // ---------- fan speed ----------
        Item {
            id: fplot
            anchors { left: tplot.left; right: tplot.right; top: tplot.bottom; topMargin: 30 }
            height: 56

            Repeater {
                model: 6
                Rectangle { x: index / 5 * fplot.width; width: 1; height: fplot.height; color: Colours.outline; opacity: 0.18 }
            }
            Repeater {
                model: [0, 50, 100]
                Item {
                    required property int modelData
                    width: fplot.width; height: 1
                    y: card.fy(modelData)
                    Rectangle { anchors.fill: parent; color: Colours.outline; opacity: 0.35 }
                    StyledText { x: -width - 8; y: -height / 2; text: card.pct(parent.modelData); color: Colours.fgDim; font.pixelSize: 9 }
                }
            }
            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: Colours.fgDim
                    strokeWidth: 2
                    fillColor: "transparent"
                    joinStyle: ShapePath.RoundJoin
                    capStyle: ShapePath.RoundCap
                    PathMultiline { paths: card.lines("fan", card.fy) }
                }
            }
            // Time labels
            Repeater {
                model: [I18n.t("-10 min"), I18n.t("-8 min"), I18n.t("-6 min"), I18n.t("-4 min"), I18n.t("-2 min"), I18n.t("now")]
                StyledText {
                    required property string modelData
                    required property int index
                    x: index / 5 * fplot.width - (index === 0 ? 0 : index === 5 ? width : width / 2)
                    y: fplot.height + 6
                    text: modelData
                    color: Colours.fgDim
                    font.pixelSize: 9
                }
            }
        }

        // ---------- hover: a line through both charts + the values at that moment ----------
        Rectangle {
            visible: card.hovered !== null
            x: tplot.x + (card.hovered ? card.px(card.hovered.t) : 0)
            y: tplot.y
            width: 1
            height: fplot.y + fplot.height - tplot.y
            color: Colours.fg
            opacity: 0.5
        }
        Rectangle {
            id: tip
            visible: card.hovered !== null
            readonly property real lineX: tplot.x + (card.hovered ? card.px(card.hovered.t) : 0)
            // Next to the line, flipped to the left near the right edge
            x: lineX + 10 + width > parent.width - 6 ? lineX - 10 - width : lineX + 10
            y: tplot.y + 4
            width: tipText.implicitWidth + 16
            height: tipText.implicitHeight + 10
            radius: 8
            color: Colours.surfaceContainer
            border { width: 1; color: Colours.outline }
            StyledText {
                id: tipText
                anchors.centerIn: parent
                text: !card.hovered ? "" : [card.ago(card.hovered.t)]
                    .concat(card.temps.map(s => s.label + " " + card.deg(card.hovered[s.key])))
                    .concat([I18n.t("Fan speed") + " " + card.pct(card.hovered.fan)]).join("\n")
                color: Colours.fg
                font: Tokens.font.label.small
            }
        }
        MouseArea {
            anchors { left: tplot.left; right: tplot.right; top: tplot.top; bottom: fplot.bottom }
            hoverEnabled: true
            onPositionChanged: mouse => card.hoverX = mouse.x
            onExited: card.hoverX = -1
        }
    }
}
