import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.components
import qs.services

// The effect list (clicking an effect activates it) and the switch for Caelestia's colours
ColumnLayout {
    id: panel
    required property var page
    property bool pushCaelestiaDown: false    // the side column: the switch at the bottom
    Layout.fillWidth: true
    Layout.fillHeight: pushCaelestiaDown
    spacing: Tokens.spacing.large

    Group {
        title: I18n.t("Effects · single zone")
        Repeater {
            model: panel.page.effects
            NavItem {
                required property var modelData
                required property int index
                icon: modelData.icon
                label: modelData.name
                sub: panel.pushCaelestiaDown ? "" : modelData.sub
                selected: Keyboard.effect === modelData.id
                first: index === 0
                last: index === panel.page.effects.length - 1
                onClicked: Keyboard.setEffect(modelData.id)
            }
        }
    }
    Item { visible: panel.pushCaelestiaDown; Layout.fillHeight: true }
    Rectangle { visible: panel.pushCaelestiaDown; Layout.fillWidth: true; implicitHeight: 1; color: Colours.outline; opacity: 0.5 }
    Group {
        ToggleRow {
            label: "Caelestia"
            sub: panel.page.animated ? I18n.t("Follows the theme") : I18n.t("For breathing and color cycle")
            controlled: true
            checked: panel.page.animated && Keyboard.source === "theme"
            enabled: panel.page.animated && panel.page.canWrite
            onToggled: v => panel.page.setTheme(v)
        }
    }
}
