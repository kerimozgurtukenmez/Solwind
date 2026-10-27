// What the keyboard light plugins share (SDK, libsolwind-sdk): QML's colour helpers to the bit, the multicolor LED
// class's format and colour, and the default colour value (Keyboard.qml's intensity(), SPEC.md K-13, K-14, K-20).
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

// ---------- shared by the keyboard plugins (kbd::, KeyboardPlugin.h) ----------

namespace {
const QRegularExpression &spaces()
{
    static const QRegularExpression re(QString::fromUtf16(js::reSpace) + u"+"_s);
    return re;
}
} // namespace

namespace kbd {
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
} // namespace kbd

bool KbdCandidates::isRgb(const KbdLight &l)
{
    if (l.v.value(u"multi_intensity"_s).isEmpty()) return false;
    QStringList w = kbd::words(l.v.value(u"multi_index"_s));
    std::sort(w.begin(), w.end());
    return w.join(u' ') == u"blue green red"_s;
}

bool KbdCandidates::isHex(const KbdLight &l)
{
    static const QRegularExpression hex(u"\\A[0-9A-Fa-f]{6}\\z"_s);
    return hex.match(l.v.value(u"color"_s)).hasMatch();
}

QString KeyboardPlugin::colorValue(const QColor &c, double level, const KbdFormat &f) const { return kbd::intensity(c, level, f); }

