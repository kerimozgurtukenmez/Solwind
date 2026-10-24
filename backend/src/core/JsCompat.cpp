#include "JsCompat.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

#include <charconv>
#include <cmath>
#include <limits>

namespace js {

namespace {

constexpr double NaN = std::numeric_limits<double>::quiet_NaN();
constexpr double Inf = std::numeric_limits<double>::infinity();

// White space as the QML engine (QV4) sees it, which isn't quite the standard's StrWhiteSpaceChar. Measured against
// Quickshell with .dev/settingsdiff.sh: Number() skips QChar::isSpace() characters (U+0085 yes, U+FEFF no), while
// String.prototype.trim also removes U+FEFF.
bool numberSpace(QChar c) { return c.isSpace(); }
bool trimSpace(QChar c) { return c.isSpace() || c.unicode() == 0xFEFF; }

QString trimWith(const QString &s, bool (*space)(QChar))
{
    qsizetype a = 0, b = s.size();
    while (a < b && space(s[a])) ++a;
    while (b > a && space(s[b - 1])) --b;
    return s.mid(a, b - a);
}

// Non-decimal integer literals (0x / 0o / 0b, no sign allowed) as a double, like JS does for big values
double radixNumber(const QString &digits, int base)
{
    double v = 0;
    for (QChar c : digits) v = v * base + QString(c).toInt(nullptr, base);
    return v;
}

// StringToNumber
double stringToNumber(const QString &raw)
{
    const QString s = trimWith(raw, numberSpace);
    if (s.isEmpty()) return 0;

    static const QRegularExpression hex(QStringLiteral("^0[xX]([0-9a-fA-F]+)$"));
    static const QRegularExpression oct(QStringLiteral("^0[oO]([0-7]+)$"));
    static const QRegularExpression bin(QStringLiteral("^0[bB]([01]+)$"));
    static const QRegularExpression dec(QStringLiteral("^[+-]?(\\d+\\.?\\d*|\\.\\d+)([eE][+-]?\\d+)?$"));
    QRegularExpressionMatch m;
    if ((m = hex.match(s)).hasMatch()) return radixNumber(m.captured(1), 16);
    if ((m = oct.match(s)).hasMatch()) return radixNumber(m.captured(1), 8);
    if ((m = bin.match(s)).hasMatch()) return radixNumber(m.captured(1), 2);
    if (s == QLatin1String("Infinity") || s == QLatin1String("+Infinity")) return Inf;
    if (s == QLatin1String("-Infinity")) return -Inf;
    if (!dec.match(s).hasMatch()) return NaN;

    // from_chars doesn't depend on the process locale (Qt sets it from the environment) and takes no leading '+'
    QByteArray b = s.toLatin1();
    double sign = 1;
    if (b.startsWith('+')) b.remove(0, 1);
    else if (b.startsWith('-')) { sign = -1; b.remove(0, 1); }
    double v = 0;
    const auto r = std::from_chars(b.constData(), b.constData() + b.size(), v, std::chars_format::general);
    if (r.ec == std::errc::result_out_of_range)                 // JS: too big -> Infinity, too small -> 0
        return sign * (b.contains("e-") || b.contains("E-") ? 0.0 : Inf);
    if (r.ec != std::errc() || r.ptr != b.constData() + b.size()) return NaN;
    return sign * v;
}

QString arrayToString(const QJsonArray &a);

// String(v) of an array element (Array.prototype.join: null and undefined become "")
QString elementToString(const QJsonValue &v)
{
    switch (v.type()) {
    case QJsonValue::Null:
    case QJsonValue::Undefined: return QString();
    case QJsonValue::Bool: return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Double: {
        const double d = v.toDouble();
        if (std::isnan(d)) return QStringLiteral("NaN");
        if (std::isinf(d)) return d > 0 ? QStringLiteral("Infinity") : QStringLiteral("-Infinity");
        return QString::number(d, 'g', 17);       // only read back as a number: any exact form will do
    }
    case QJsonValue::String: return v.toString();
    case QJsonValue::Array: return arrayToString(v.toArray());
    case QJsonValue::Object: return QStringLiteral("[object Object]");
    }
    return QString();
}

QString arrayToString(const QJsonArray &a)
{
    QStringList parts;
    for (const QJsonValue &v : a) parts << elementToString(v);
    return parts.join(QLatin1Char(','));
}

} // namespace

double number(const QJsonValue &v)
{
    switch (v.type()) {
    case QJsonValue::Null: return 0;
    case QJsonValue::Bool: return v.toBool() ? 1 : 0;
    case QJsonValue::Double: return v.toDouble();
    case QJsonValue::String: return stringToNumber(v.toString());
    case QJsonValue::Array: return stringToNumber(arrayToString(v.toArray()));
    case QJsonValue::Object:
    case QJsonValue::Undefined: return NaN;
    }
    return NaN;
}

double round(double x)
{
    if (!std::isfinite(x)) return x;
    return std::floor(x + 0.5);
}

double num(const QJsonValue &v, double lo, double hi)
{
    const double n = number(v);
    return std::isfinite(n) ? std::max(lo, std::min(hi, n)) : NaN;
}

bool truthy(const QJsonValue &v)
{
    switch (v.type()) {
    case QJsonValue::Null:
    case QJsonValue::Undefined: return false;
    case QJsonValue::Bool: return v.toBool();
    case QJsonValue::Double: { const double d = v.toDouble(); return d != 0 && !std::isnan(d); }
    case QJsonValue::String: return !v.toString().isEmpty();
    case QJsonValue::Array:
    case QJsonValue::Object: return true;
    }
    return false;
}

QJsonValue andGet(const QJsonValue &p, const QString &key)
{
    if (!truthy(p)) return p;
    if (p.isObject()) return p.toObject().value(key);
    return QJsonValue(QJsonValue::Undefined);
}

QString trim(const QString &s) { return trimWith(s, trimSpace); }

} // namespace js
