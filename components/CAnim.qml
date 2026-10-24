import QtQuick
import Caelestia.Config

// Colour transition, as Caelestia's (components/CAnim.qml, GPL-3.0)
ColorAnimation {
    duration: Tokens.anim.durations.expressiveSlowEffects
    easing: Tokens.anim.expressiveSlowEffects
}
