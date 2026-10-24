import QtQuick
import Caelestia.Config
import qs.services

// A Material Symbols icon in Caelestia's icon font (adapted from Caelestia's components/MaterialIcon.qml, GPL-3.0).
// fill: 0 outlined .. 1 filled. size: a Tokens.font.icon.* style (small, medium, large, extraLarge).
StyledText {
    property real fill: 0
    property int grade: Colours.light ? 0 : -25
    property font size: Tokens.font.icon.small

    font: Tokens.font.icon.size(size.pointSize).weight(size.weight).vaxes(size.variableAxes).fill(fill.toFixed(1)).grade(grade).build()
    textFormat: Text.PlainText
    Behavior on fill { Anim { type: Anim.DefaultEffects } }
}
