import QtQuick
import QtQuick.Layouts
import qs.services

// A row with a stepper (Caelestia's settings app, common/StepperRow.qml)
GroupItem {
    id: r
    property alias label: st.label
    property alias sub: st.sub
    property alias value: st.value
    property alias from: st.from
    property alias to: st.to
    property alias unit: st.unit
    signal moved(int v)

    opacity: enabled ? 1 : 0.5
    Stepper { id: st; Layout.fillWidth: true; onMoved: v => r.moved(v) }
}
