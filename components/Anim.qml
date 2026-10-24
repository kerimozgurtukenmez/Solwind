import QtQuick
import Caelestia.Config

// Motion animation with Caelestia's durations and curves (adapted from Caelestia's components/Anim.qml, GPL-3.0).
// type: Anim.DefaultSpatial (default), FastSpatial, SlowSpatial, FastEffects, DefaultEffects, SlowEffects
NumberAnimation {
    enum Type { FastSpatial, DefaultSpatial, SlowSpatial, FastEffects, DefaultEffects, SlowEffects }
    property int type: Anim.DefaultSpatial

    duration: type === Anim.FastSpatial ? Tokens.anim.durations.expressiveFastSpatial
            : type === Anim.SlowSpatial ? Tokens.anim.durations.expressiveSlowSpatial
            : type === Anim.FastEffects ? Tokens.anim.durations.expressiveFastEffects
            : type === Anim.DefaultEffects ? Tokens.anim.durations.expressiveDefaultEffects
            : type === Anim.SlowEffects ? Tokens.anim.durations.expressiveSlowEffects
            : Tokens.anim.durations.expressiveDefaultSpatial
    easing: type === Anim.FastSpatial ? Tokens.anim.expressiveFastSpatial
          : type === Anim.SlowSpatial ? Tokens.anim.expressiveSlowSpatial
          : type === Anim.FastEffects ? Tokens.anim.expressiveFastEffects
          : type === Anim.DefaultEffects ? Tokens.anim.expressiveDefaultEffects
          : type === Anim.SlowEffects ? Tokens.anim.expressiveSlowEffects
          : Tokens.anim.expressiveDefaultSpatial
}
