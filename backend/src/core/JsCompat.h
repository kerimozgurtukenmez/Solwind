#pragma once

#include <QJsonValue>
#include <QString>

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

} // namespace js
