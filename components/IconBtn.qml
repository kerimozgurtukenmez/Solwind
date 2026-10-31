import QtQuick
import Caelestia.Config
import qs.services

// A round button with only an icon (the arrows that fold the navigation and side panels)
Rectangle {
    id: b
    property string icon
    signal clicked()

    implicitWidth: implicitHeight
    implicitHeight: Tokens.font.icon.medium.pointSize + Tokens.padding.small * 2
    radius: height / 2
    color: "transparent"

    StateLayer { onClicked: b.clicked() }
    MaterialIcon {
        anchors.centerIn: parent
        text: b.icon
        color: Colours.fgDim
        size: Tokens.font.icon.medium
    }
}
