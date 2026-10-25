#pragma once

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QString>

#include <optional>
#include <utility>

// Until 0.5.x the settings file was validated by JavaScript (services/Settings.qml), and the daemon must read it the
// same way (SPEC.md ST-4): a value the old app accepted is accepted, one it dropped is dropped. These are the few
// JavaScript rules those validators depend on.
namespace js {

// Number(v): null, false, "" -> 0, true -> 1, numeric strings (with spaces, hex 0x.., Infinity) -> their value,
// arrays through their string form ([5] -> 5, [] -> 0), objects and other strings -> NaN, a missing value -> NaN
double number(const QJsonValue &v);

// Math.round: halves go up (2.5 -> 3, -2.5 -> -2), unlike std::round
double round(double x);

// Settings.qml num(v, lo, hi): Number(v) clamped to lo..hi, NaN if it isn't a finite number
double num(const QJsonValue &v, double lo, double hi);

// Truthiness: undefined, null, false, 0, NaN and "" are false
bool truthy(const QJsonValue &v);

// `p && p[key]`: p itself if it is falsy, the member of an object (undefined if missing), undefined otherwise
QJsonValue andGet(const QJsonValue &p, const QString &key);

// String.prototype.trim as the QML engine does it (QChar::isSpace() characters and U+FEFF)
QString trim(const QString &s);

// parseInt(s) (radix 10, or 16 after a 0x prefix): the leading integer, NaN if there is none
double parseInt(const QString &s);
// parseFloat(s): the longest decimal number (or Infinity) at the start, NaN if there is none
double parseFloat(const QString &s);
// String(v) of a JSON value as JS writes it (numbers in their shortest form; objects "[object Object]")
QString toString(const QJsonValue &v);

// Math.max(a, b) / Math.min(a, b): NaN if either is NaN (std::max / std::min aren't)
inline double max(double a, double b) { return a != a || b != b ? a + b : (a > b ? a : b); }
inline double min(double a, double b) { return a != a || b != b ? a + b : (a < b ? a : b); }

// JS RegExp pieces whose meaning differs in PCRE: \s (JS: Unicode white space and line terminators), "." (JS: not a
// line terminator) and "$" without the m flag (JS: the very end only)
inline constexpr char16_t reSpace[] = u"[\\t\\n\\v\\f\\r \\x{a0}\\x{1680}\\x{2000}-\\x{200a}\\x{2028}\\x{2029}\\x{202f}\\x{205f}\\x{3000}\\x{feff}]";
inline constexpr char16_t reDot[] = u"[^\\n\\r\\x{2028}\\x{2029}]";

// JSON.parse of a file's text the way the QML app read files (FileView text() + JSON.parse; measured with
// .dev/settingsdiff.sh): the bytes are decoded as UTF-8 (invalid bytes become U+FFFD instead of failing), and a raw
// control character inside a string fails (QJsonDocument alone would accept it), and a top-level scalar (5, "x",
// null) is a value as in JS (QJsonDocument refuses it, measured). nullopt + why on failure.
std::optional<QJsonValue> parseJson(const QByteArray &bytes, QString *why = nullptr);

// Array.prototype.sort as the QML engine does it: Qt's quicksort from qv4arraydata_p.h (sortHelper, Qt 6.12,
// GPL-2.0/GPL-3.0), copied unchanged. It isn't stable, and points with equal temperatures must come out in the same
// order as in 0.5.x (checked with .dev/settingsdiff.sh).
template <typename RandomAccessIterator, typename LessThan>
void qv4Sort(RandomAccessIterator start, RandomAccessIterator end, LessThan lessThan)
{
top:
    using std::swap;

    int span = int(end - start);
    if (span < 2)
        return;

    --end;
    RandomAccessIterator low = start, high = end - 1;
    RandomAccessIterator pivot = start + span / 2;

    if (lessThan(*end, *start))
        swap(*end, *start);
    if (span == 2)
        return;

    if (lessThan(*pivot, *start))
        swap(*pivot, *start);
    if (lessThan(*end, *pivot))
        swap(*end, *pivot);
    if (span == 3)
        return;

    swap(*pivot, *end);

    while (low < high) {
        while (low < high && lessThan(*low, *end))
            ++low;

        while (high > low && lessThan(*end, *high))
            --high;

        if (low < high) {
            swap(*low, *high);
            ++low;
            --high;
        } else {
            break;
        }
    }

    if (lessThan(*low, *end))
        ++low;

    swap(*end, *low);
    qv4Sort(start, low, lessThan);

    start = low + 1;
    ++end;
    goto top;
}

} // namespace js
