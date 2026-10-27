// SAFETY: only the hp-wmi module's rgb_zones files in /sys; their paths only ever come from detection, and the driver
// validates every value.
#include "HpWmiRgbZones.h"

#include "core/JsCompat.h"

using namespace Qt::StringLiterals;

QList<KbdLight> HpWmiRgbZones::match(const KbdCandidates &c) const
{
    return kbd::every(c.rgbZones, KbdCandidates::isHex) && kbd::every(c.rgbZones, [](const KbdLight &l) { return l.driver == u"hp-wmi"_s; })
         ? c.rgbZones : QList<KbdLight>();
}

bool HpWmiRgbZones::writable(const QList<KbdLight> &l) const
{
    return kbd::every(l, [](const KbdLight &x) { return x.w.contains(u"color"_s); });
}

std::optional<QColor> HpWmiRgbZones::shownColor(const KbdLight &first, const KbdFormat &) const
{
    const int h = first.v.value(u"color"_s).toInt(nullptr, 16);   // (isHex: six hex digits)
    return Qml::rgba(((h >> 16) & 255) / 255.0, ((h >> 8) & 255) / 255.0, (h & 255) / 255.0, 1);
}

QString HpWmiRgbZones::colorValue(const QColor &c, double level, const KbdFormat &) const
{
    auto at = [&](float x) { return js::max(0, js::min(1, x * level)); };
    return Qml::hex(Qml::rgba(at(c.redF()), at(c.greenF()), at(c.blueF()), 1)).mid(1);
}
