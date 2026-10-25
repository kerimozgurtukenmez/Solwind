#include "JsCompat.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>

#include <cctype>
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

double parseFloat(const QString &raw)
{
    QStringView s(raw);
    qsizetype i = 0;
    while (i < s.size() && numberSpace(s[i])) ++i;
    static const QRegularExpression lit(QStringLiteral("^[+-]?(Infinity|(\\d+\\.?\\d*|\\.\\d+)([eE][+-]?\\d+)?)"));
    const QRegularExpressionMatch m = lit.matchView(s.mid(i));
    return m.hasMatch() ? stringToNumber(m.captured(0)) : NaN;
}

QString toString(const QJsonValue &v)
{
    switch (v.type()) {
    case QJsonValue::Undefined: return QStringLiteral("undefined");
    case QJsonValue::Null: return QStringLiteral("null");
    case QJsonValue::Object: return QStringLiteral("[object Object]");
    case QJsonValue::Double: {
        // Integers and plain decimals as JS writes them; very large/small numbers may differ in the exponent's
        // form (1e-07 vs JS 1e-7), which none of the values this is used for can be
        const double d = v.toDouble();
        if (std::isnan(d)) return QStringLiteral("NaN");
        if (std::isinf(d)) return d > 0 ? QStringLiteral("Infinity") : QStringLiteral("-Infinity");
        if (d == std::floor(d) && std::fabs(d) < 1e15) return QString::number(qint64(d));
        return QString::number(d, 'g', QLocale::FloatingPointShortest);
    }
    default: return elementToString(v);
    }
}

double parseInt(const QString &raw)
{
    // Leading white space as Number() skips it, then an optional sign, then digits (hex after 0x)
    QStringView s(raw);
    qsizetype i = 0;
    while (i < s.size() && numberSpace(s[i])) ++i;
    double sign = 1;
    if (i < s.size() && (s[i] == u'+' || s[i] == u'-')) { if (s[i] == u'-') sign = -1; ++i; }
    int base = 10;
    if (i + 1 < s.size() && s[i] == u'0' && (s[i + 1] == u'x' || s[i + 1] == u'X')) { base = 16; i += 2; }
    double v = 0;
    bool any = false;
    for (; i < s.size(); ++i) {
        const int d = s[i].isDigit() && s[i].unicode() < 128 ? s[i].unicode() - '0'
                    : base == 16 && s[i].unicode() < 128 && std::isxdigit(s[i].unicode()) ? (s[i].toLower().unicode() - 'a' + 10) : -1;
        if (d < 0 || d >= base) break;
        v = v * base + d;
        any = true;
    }
    return any ? sign * v : NaN;
}

// JSON.parse refuses a raw control character (below 0x20) inside a string; QJsonDocument accepts it
static bool rawControlInString(const QByteArray &text)
{
    bool in = false, esc = false;
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        if (!in) { if (c == '"') in = true; continue; }
        if (esc) { esc = false; continue; }
        if (c == '\\') esc = true;
        else if (c == '"') in = false;
        else if (c < 0x20) return true;
    }
    return false;
}

std::optional<QJsonValue> parseJson(const QByteArray &bytes, QString *why)
{
    QByteArray text = QString::fromUtf8(bytes).toUtf8();
    if (text.startsWith("\xEF\xBB\xBF")) text.remove(0, 3);    // a file starting with a BOM was read (measured)
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(text, &err);
    QJsonValue value;
    QString w;
    if (err.error == QJsonParseError::NoError) {
        value = doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array());
    } else {
        // A top-level scalar: read as the only element of an array (exactly one, so "1, 2" stays an error)
        QJsonParseError e2;
        const QJsonDocument wrapped = QJsonDocument::fromJson("[" + text + "]", &e2);
        const QJsonArray a = wrapped.array();
        if (e2.error == QJsonParseError::NoError && a.size() == 1 && !a[0].isArray() && !a[0].isObject()) value = a[0];
        else w = err.errorString();
    }
    if (w.isEmpty() && rawControlInString(text)) w = QStringLiteral("control character in a string");
    if (why) *why = w;
    if (!w.isEmpty()) return std::nullopt;
    return value;
}

} // namespace js
