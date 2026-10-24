// Answers validator calls, one JSON object per line on stdin -> one JSON value per line on stdout:
//   {"fn": "cleanSt" | "cleanProfile" | "number", "arg": <value>}
//   {"fn": "kbd" | "safety" | "uiLang", "data": <whole settings object>}
//   {"fn": "notifyOn" | "uiOn", "data": <settings object>, "key": "<key>"}
//   {"fn": "fileData", "dir": "<folder>"}: the data loaded from <folder>/settings.json
// The same calls go to the old QML Settings.qml, and the answers must match (SPEC.md ST-4: read the file the same way).
// Numbers that JSON can't carry (NaN, Infinity) are answered as strings, the way JS String() writes them.
#include "core/JsCompat.h"
#include "core/Settings.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>

using namespace Qt::StringLiterals;

static QJsonValue numberOut(double n)
{
    if (std::isnan(n)) return u"NaN"_s;
    if (std::isinf(n)) return n > 0 ? u"Infinity"_s : u"-Infinity"_s;
    return n;
}

int main()
{
    QTemporaryDir tmp;
    QFile in, out;
    if (!in.open(stdin, QIODevice::ReadOnly) || !out.open(stdout, QIODevice::WriteOnly)) return 1;
    while (!in.atEnd()) {
        const QByteArray line = in.readLine().trimmed();
        if (line.isEmpty()) continue;
        const QJsonObject q = QJsonDocument::fromJson(line).object();
        const QString fn = q.value(u"fn"_s).toString();
        const QJsonValue arg = q.value(u"arg"_s);
        QJsonValue r;
        if (fn == u"cleanSt"_s) {
            const auto st = Settings::cleanSt(arg);
            r = st ? QJsonValue(st->toJson()) : QJsonValue(QJsonValue::Null);
        } else if (fn == u"cleanProfile"_s) {
            const auto p = Settings::cleanProfile(arg);
            r = p ? QJsonValue(p->toJson()) : QJsonValue(QJsonValue::Null);
        } else if (fn == u"number"_s) {
            r = numberOut(js::number(arg));
        } else if (fn == u"fileData"_s) {
            Settings s(q.value(u"dir"_s).toString());
            r = s.load() ? QJsonValue(s.data()) : QJsonValue(u"not loaded"_s);
        } else {
            // Functions that read the whole settings object: load it through a file, as the daemon does
            QFile f(tmp.path() + u"/settings.json"_s);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 1;
            f.write(QJsonDocument(q.value(u"data"_s).toObject()).toJson());
            f.close();
            Settings s(tmp.path());
            s.load();
            const QString key = q.value(u"key"_s).toString();
            if (fn == u"kbd"_s) {
                const KbdSettings k = s.kbd();
                r = QJsonObject{ { u"effect"_s, k.effect }, { u"themeCount"_s, k.themeCount }, { u"source"_s, k.source },
                                 { u"colors"_s, QJsonArray::fromStringList(k.colors) }, { u"speed"_s, k.speed },
                                 { u"min"_s, k.min }, { u"max"_s, k.max }, { u"color"_s, k.color } };
            } else if (fn == u"safety"_s) r = numberOut(s.safety());
            else if (fn == u"uiLang"_s) r = s.uiLang();
            else if (fn == u"notifyOn"_s) r = s.notifyOn(key);
            else if (fn == u"uiOn"_s) r = s.uiOn(key);
            else r = u"unknown fn"_s;
        }
        out.write(QJsonDocument(QJsonArray{ r }).toJson(QJsonDocument::Compact) + '\n');   // as [value]
        out.flush();
    }
    return 0;
}
