// SAFETY: only the LED class files the hp-wmi driver exposes in /sys; their paths only ever come from detection, and
// the driver validates every value.
#include "HpWmiKeyboardZones.h"

using namespace Qt::StringLiterals;

QList<KbdLight> HpWmiKeyboardZones::match(const KbdCandidates &c) const
{
    return kbd::every(c.zoned, KbdCandidates::isRgb) && kbd::every(c.zoned, [](const KbdLight &l) { return l.driver == u"hp-wmi"_s; })
         ? c.zoned : QList<KbdLight>();
}

bool HpWmiKeyboardZones::writable(const QList<KbdLight> &l) const
{
    return kbd::every(l, [](const KbdLight &x) { return x.w.contains(u"brightness"_s) && x.w.contains(u"multi_intensity"_s); });
}
