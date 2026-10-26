// The keyboard light plugins (Keyboard.qml's backends, SPEC.md K-6..K-20), and QML's colour helpers.
// SAFETY: only files a kernel driver exposes in /sys are used (LED class files, or the hp-wmi module's rgb_zones),
// and their paths only ever come from detection; the driver validates every value.
#include "core/JsCompat.h"
#include "plugins/KeyboardPlugin.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

// ---------- QML's helpers ----------

QColor Qml::rgba(double r, double g, double b, double a)
{
    return QColor::fromRgbF(qBound(0.0, r, 1.0), qBound(0.0, g, 1.0), qBound(0.0, b, 1.0), qBound(0.0, a, 1.0));
}

QColor Qml::hsva(double h, double s, double v, double a)
{
    if (h != -1) h = qBound(0.0, h, 1.0);
    return QColor::fromHsvF(h, qBound(0.0, s, 1.0), qBound(0.0, v, 1.0), qBound(0.0, a, 1.0));
}

QColor Qml::lighter(const QColor &c, double factor) { return c.lighter(int(qRound(factor * 100.))); }

QString Qml::number(double v)
{
    if (std::isnan(v)) return u"NaN"_s;
    if (std::isinf(v)) return v > 0 ? u"Infinity"_s : u"-Infinity"_s;
    if (v == std::floor(v) && std::fabs(v) < 1e15) return QString::number(qint64(v));   // (-0 -> "0", as JS)
    return js::toString(QJsonValue(v));
}

QString Qml::hex(const QColor &c)
{
    auto h = [](float x) { return (u"0"_s + QString::number(qint64(js::round(double(x) * 255)), 16)).right(2); };
    return (u"#"_s + h(c.redF()) + h(c.greenF()) + h(c.blueF())).toUpper();
}

// ---------- shared ----------

namespace {
const QRegularExpression &spaces()
{
    static const QRegularExpression re(QString::fromUtf16(js::reSpace) + u"+"_s);
    return re;
}
// s.trim().split(/\s+/)
QStringList words(const QString &s) { return js::trim(s).split(spaces()); }

// colour x level per hardware channel, clamped (Keyboard.qml intensity(), K-20; frame() leaves it unrounded)
QString intensity(const QColor &c, double level, const KbdFormat &f)
{
    const double p[3] = { c.redF(), c.greenF(), c.blueF() };
    QStringList out;
    for (qsizetype k = 0; k < f.channels.size(); ++k) {
        const double top = k < f.maxIntensity.size() ? f.maxIntensity[k] : 255;
        out << Qml::number(js::round(js::max(0, js::min(1, p[f.channels[k]] * level)) * top));
    }
    return out.join(u' ');
}

// A light with an RGB channel per colour (multi_intensity): its channel order and top values, the colour it shows
KbdFormat multiFormat(const KbdLight &first)
{
    KbdFormat f;
    f.maxIntensity.clear();
    const QString max = first.v.value(u"multi_max_intensity"_s);
    for (const QString &x : words(max.isEmpty() ? u"255 255 255"_s : max)) {
        const double n = js::parseInt(x);
        f.maxIntensity << (n == 0 || std::isnan(n) ? 255 : n);   // parseInt(x) || 255
    }
    f.channels.clear();
    static const QHash<QString, int> part{ { u"red"_s, 0 }, { u"green"_s, 1 }, { u"blue"_s, 2 } };
    for (const QString &n : words(first.v.value(u"multi_index"_s))) f.channels << part.value(n);   // (isRgb: all three)
    return f;
}
std::optional<QColor> multiColor(const KbdLight &first, const KbdFormat &f)
{
    QList<double> it;
    for (const QString &x : words(first.v.value(u"multi_intensity"_s))) {
        const double n = js::parseInt(x);
        it << (n == 0 || std::isnan(n) ? 0 : n);                  // parseInt(x) || 0
    }
    const QString idx = first.v.value(u"multi_index"_s);
    const QStringList order = words(idx.isEmpty() ? u"red green blue"_s : idx);
    const double nan = std::nan("");
    auto ch = [&](const QString &n) {
        const qsizetype k = order.indexOf(n);
        if (k < 0) return 0.0;
        return (k < it.size() ? it[k] : nan) / (k < f.maxIntensity.size() ? f.maxIntensity[k] : nan);   // (undefined -> NaN)
    };
    return Qml::rgba(ch(u"red"_s), ch(u"green"_s), ch(u"blue"_s), 1);
}

bool every(const QList<KbdLight> &l, bool (*ok)(const KbdLight &)) { return !l.isEmpty() && std::all_of(l.begin(), l.end(), ok); }

// ---------- the plugins ----------

// Four-zone keyboards with one LED per zone (the kernel's naming *::kbd_zoned_backlight-<zone>; TUXOV's
// hp-wmi-fan-and-backlight-control 0.0.4+ right/middle/left/wasd; its 0.0.3 numbers them *::kbd_backlight_zoneN).
// Every zone gets the same colour and brightness. No software effects: every zone write is a WMI colour-table read
// and write in both module versions, and it wasn't tried on such a keyboard.
class HpZoned : public KeyboardPlugin
{
public:
    QString id() const override { return u"hp-zoned"_s; }
    bool rgb() const override { return true; }
    QList<KbdLight> match(const KbdCandidates &c) const override
    {
        return every(c.zoned, KbdCandidates::isRgb) ? c.zoned : QList<KbdLight>();
    }
    bool writable(const QList<KbdLight> &l) const override
    {
        return every(l, [](const KbdLight &x) { return x.w.contains(u"brightness"_s) && x.w.contains(u"multi_intensity"_s); });
    }
    KbdFormat format(const KbdLight &first) const override { return multiFormat(first); }
    std::optional<QColor> shownColor(const KbdLight &first, const KbdFormat &f) const override { return multiColor(first, f); }
    AtStart atStart() const override { return AtStart::WhenStaticAndOn; }
};

// HP four-zone keyboards with older copies of that module (0.0.2, e.g. the one victus-control installs):
// /sys/devices/platform/hp-wmi/rgb_zones/zone00..03, hex RRGGBB. No brightness file: colour only, the same on all.
class HpRgbZones : public KeyboardPlugin
{
public:
    QString id() const override { return u"hp-rgbzones"_s; }
    bool rgb() const override { return true; }
    bool hasBrightness() const override { return false; }
    QList<KbdLight> match(const KbdCandidates &c) const override
    {
        return every(c.rgbZones, KbdCandidates::isHex) ? c.rgbZones : QList<KbdLight>();
    }
    bool writable(const QList<KbdLight> &l) const override
    {
        return every(l, [](const KbdLight &x) { return x.w.contains(u"color"_s); });
    }
    std::optional<QColor> shownColor(const KbdLight &first, const KbdFormat &) const override
    {
        const int h = first.v.value(u"color"_s).toInt(nullptr, 16);   // (isHex: six hex digits)
        return Qml::rgba(((h >> 16) & 255) / 255.0, ((h >> 8) & 255) / 255.0, (h & 255) / 255.0, 1);
    }
    QString colorPath(const QString &zone) const override { return zone; }
    QString colorValue(const QColor &c, double level, const KbdFormat &) const override
    {
        auto at = [&](float x) { return js::max(0, js::min(1, x * level)); };
        return Qml::hex(Qml::rgba(at(c.redF()), at(c.greenF()), at(c.blueF()), 1)).mid(1);
    }
};

// One RGB light: brightness + multi_intensity (HP Victus/Omen single zone: hp::kbd_backlight). Software effects run
// here, but only with the hp-wmi driver they were tested with.
class LedRgb : public KeyboardPlugin
{
public:
    QString id() const override { return u"led-rgb"_s; }
    bool rgb() const override { return true; }
    bool effectsTested(const QString &driver) const override { return driver == u"hp-wmi"_s; }
    bool tested(const QString &driver) const override { return driver == u"hp-wmi"_s; }
    QList<KbdLight> match(const KbdCandidates &c) const override
    {
        return c.single && KbdCandidates::isRgb(*c.single) ? QList<KbdLight>{ *c.single } : QList<KbdLight>();
    }
    bool writable(const QList<KbdLight> &l) const override
    {
        return every(l, [](const KbdLight &x) { return x.w.contains(u"brightness"_s) && x.w.contains(u"multi_intensity"_s); });
    }
    KbdFormat format(const KbdLight &first) const override { return multiFormat(first); }
    std::optional<QColor> shownColor(const KbdLight &first, const KbdFormat &f) const override { return multiColor(first, f); }
    AtStart atStart() const override { return AtStart::WhenStaticAndOn; }
};

// ASUS TUF: asus::kbd_backlight brightness (0..3) + kbd_rgb_mode "cmd mode r g b speed" for the colour. Always cmd 0
// (apply only, never 1 = save to BIOS) and mode 0 (static). kbd_rgb_mode can't be read back: the saved colour is
// written once at start. Only tried with a fake /sys.
class AsusTuf : public KeyboardPlugin
{
public:
    QString id() const override { return u"asus-tuf"_s; }
    bool rgb() const override { return true; }
    QList<KbdLight> match(const KbdCandidates &c) const override
    {
        return c.single && c.single->x.contains(u"kbd_rgb_mode"_s) ? QList<KbdLight>{ *c.single } : QList<KbdLight>();
    }
    bool writable(const QList<KbdLight> &l) const override
    {
        return every(l, [](const KbdLight &x) { return x.w.contains(u"brightness"_s) && x.w.contains(u"kbd_rgb_mode"_s); });
    }
    AtStart atStart() const override { return AtStart::Always; }
    QString colorPath(const QString &zone) const override { return zone + u"/kbd_rgb_mode"_s; }
    QString colorValue(const QColor &c, double level, const KbdFormat &) const override
    {
        QStringList v;
        for (float x : { c.redF(), c.greenF(), c.blueF() }) v << Qml::number(js::round(js::max(0, js::min(1, x * level)) * 255));
        return u"0 0 "_s + v.join(u' ') + u" 1"_s;
    }
};

// Brightness only (ASUS non-RGB, Lenovo IdeaPad/ThinkPad, others): the single light, else every zone of a keyboard
// whose zones aren't RGB
class LedMono : public KeyboardPlugin
{
public:
    QString id() const override { return u"led-mono"_s; }
    bool rgb() const override { return false; }
    QList<KbdLight> match(const KbdCandidates &c) const override
    {
        if (c.single) return { *c.single };
        return c.zoned;
    }
    bool writable(const QList<KbdLight> &l) const override
    {
        return every(l, [](const KbdLight &x) { return x.w.contains(u"brightness"_s); });
    }
};
} // namespace

bool KbdCandidates::isRgb(const KbdLight &l)
{
    if (l.v.value(u"multi_intensity"_s).isEmpty()) return false;
    QStringList w = words(l.v.value(u"multi_index"_s));
    std::sort(w.begin(), w.end());
    return w.join(u' ') == u"blue green red"_s;
}

bool KbdCandidates::isHex(const KbdLight &l)
{
    static const QRegularExpression hex(u"\\A[0-9A-Fa-f]{6}\\z"_s);
    return hex.match(l.v.value(u"color"_s)).hasMatch();
}

QString KeyboardPlugin::colorValue(const QColor &c, double level, const KbdFormat &f) const { return intensity(c, level, f); }

const QList<const KeyboardPlugin *> &KeyboardPlugin::all()
{
    static const HpZoned hpZoned;
    static const HpRgbZones hpRgbZones;
    static const LedRgb ledRgb;
    static const AsusTuf asusTuf;
    static const LedMono ledMono;
    static const QList<const KeyboardPlugin *> list{ &hpZoned, &hpRgbZones, &ledRgb, &asusTuf, &ledMono };
    return list;
}
