// SAFETY: only the LED class files the hp-wmi driver exposes in /sys; their paths only ever come from detection, and
// the driver validates every value.
#include "HpWmiKeyboard.h"

using namespace Qt::StringLiterals;

bool HpWmiKeyboard::effectsTested(const QString &driver) const { return driver == u"hp-wmi"_s; }
bool HpWmiKeyboard::tested(const QString &driver) const { return driver == u"hp-wmi"_s; }

QList<KbdLight> HpWmiKeyboard::match(const KbdCandidates &c) const
{
    return c.single && c.single->driver == u"hp-wmi"_s && KbdCandidates::isRgb(*c.single) ? QList<KbdLight>{ *c.single }
                                                                                          : QList<KbdLight>();
}

bool HpWmiKeyboard::writable(const QList<KbdLight> &l) const
{
    return kbd::every(l, [](const KbdLight &x) { return x.w.contains(u"brightness"_s) && x.w.contains(u"multi_intensity"_s); });
}
