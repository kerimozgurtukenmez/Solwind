// Answers calls to the phase 2 functions, one JSON object per line on stdin -> one JSON array [answer] per line on
// stdout, so the same inputs can be compared with services/Nbfc.qml and Sensors.qml (.dev/phase2diff.sh):
//   {"fn": "parseStatus", "arg": "<nbfc status -a text>"}      {"fn": "toCurve", "arg": <thresholds>}
//   {"fn": "configCurves", "arg": "<config file text>"}         {"fn": "selectedConfigId", "path": "<nbfc.json file>"}
//   {"fn": "merge", "dirs": [..], "cli": [..], "configId": "", "fileConfigId": ""}
//   {"fn": "recommend", "arg": "<nbfc config -r text>", "known": [..]}   {"fn": "configList", "arg": "<text>"}
//   {"fn": "firstLine" | "parseInt" | "parseFloat" | "cleanCpu" | "cleanGpu" | "parseHw", "arg": "<text>"}
//   {"fn": "sensors", "hw": "<hwinfo text>", "rounds": ["<sensors.sh text>", ...]}
//   {"fn": "reader", "root": "<fake root>", "gpu": true}   {"fn": "hwinfo", "root": "<fake root or "">"}
// NaN / Infinity are answered as strings ("NaN", "Infinity"), the way JS String() writes them.
#include "core/JsCompat.h"
#include "core/SensorReader.h"
#include "core/Sensors.h"
#include "plugins/fan/nbfc/NbfcParse.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {
QJsonValue num(double n)
{
    if (std::isnan(n)) return u"NaN"_s;
    if (std::isinf(n)) return n > 0 ? u"Infinity"_s : u"-Infinity"_s;
    return n;
}
QStringList strings(const QJsonValue &v)
{
    QStringList out;
    for (const QJsonValue &x : v.toArray()) out << x.toString();
    return out;
}
QJsonValue curve(const std::optional<Nbfc::ConfigCurve> &c)
{
    if (!c) return QJsonValue::Null;
    QJsonArray pts;
    for (const auto &p : c->points) pts.append(QJsonObject{ { u"t"_s, p.t }, { u"s"_s, p.s } });
    return QJsonObject{ { u"points"_s, pts }, { u"hyst"_s, c->hyst } };
}
QJsonObject readingsState(const SensorParser &p, qint64 now)
{
    QJsonArray rs, rpms;
    for (const Reading &r : p.readings)
        rs.append(QJsonObject{ { u"label"_s, r.label }, { u"model"_s, r.model }, { u"threads"_s, r.threads },
                               { u"value"_s, num(r.value) }, { u"note"_s, r.note } });
    for (double v : p.rpms) rpms.append(num(v));
    return { { u"readings"_s, rs }, { u"rpms"_s, rpms }, { u"rpmWorks"_s, p.rpmWorks }, { u"nvFails"_s, p.nvFails },
             { u"wait"_s, double(p.nvRetryAt ? p.nvRetryAt - now : 0) } };
}
} // namespace

int main()
{
    QFile in, out;
    if (!in.open(stdin, QIODevice::ReadOnly) || !out.open(stdout, QIODevice::WriteOnly)) return 1;
    while (!in.atEnd()) {
        const QByteArray line = in.readLine().trimmed();
        if (line.isEmpty()) continue;
        const QJsonObject q = QJsonDocument::fromJson(line).object();
        const QString fn = q.value(u"fn"_s).toString();
        const QJsonValue arg = q.value(u"arg"_s);
        QJsonValue r;
        if (fn == u"parseStatus"_s) {
            const Nbfc::Status s = Nbfc::parseStatus(arg.toString());
            QJsonArray fans;
            for (const auto &f : s.fans)
                fans.append(QJsonObject{ { u"name"_s, f.name }, { u"temp"_s, num(f.temp) }, { u"auto"_s, f.autoControl },
                                         { u"critical"_s, f.critical }, { u"current"_s, num(f.current) },
                                         { u"target"_s, num(f.target) } });
            r = QJsonObject{ { u"readOnly"_s, s.readOnly ? QJsonValue(*s.readOnly) : QJsonValue(QJsonValue::Null) },
                             { u"temp"_s, num(s.temp) }, { u"fans"_s, fans }, { u"config"_s, s.config } };
        } else if (fn == u"toCurve"_s) {
            bool thrown = false;
            const auto c = Nbfc::toCurve(arg, &thrown);
            r = thrown ? QJsonValue(u"THROW"_s) : curve(c);
        } else if (fn == u"configCurves"_s) {
            QJsonArray a;
            for (const auto &c : Nbfc::configCurves(arg.toString().toUtf8())) a.append(curve(c));
            r = a;
        } else if (fn == u"selectedConfigId"_s) {
            QFile f(q.value(u"path"_s).toString());                    // the bytes, as the daemon reads nbfc.json
            const auto id = f.open(QIODevice::ReadOnly) ? Nbfc::selectedConfigId(f.readAll()) : std::nullopt;
            r = id ? QJsonValue(*id) : QJsonValue(u"KEEP"_s);
        } else if (fn == u"merge"_s) {
            r = QJsonArray::fromStringList(Nbfc::mergeConfigs(strings(q.value(u"dirs"_s)), strings(q.value(u"cli"_s)),
                                                              q.value(u"configId"_s).toString(), q.value(u"fileConfigId"_s).toString()));
        } else if (fn == u"recommend"_s) {
            r = QJsonArray::fromStringList(Nbfc::matchRecommendations(arg.toString(), strings(q.value(u"known"_s))));
        } else if (fn == u"configList"_s) {
            r = QJsonArray::fromStringList(Nbfc::parseConfigList(arg.toString()));
        } else if (fn == u"firstLine"_s) {
            r = Nbfc::firstLine(arg.toString());
        } else if (fn == u"parseInt"_s) {
            r = num(js::parseInt(arg.toString()));
        } else if (fn == u"parseFloat"_s) {
            r = num(js::parseFloat(arg.toString()));
        } else if (fn == u"cleanCpu"_s) {
            r = SensorParser::cleanCpu(arg.toString());
        } else if (fn == u"cleanGpu"_s) {
            r = SensorParser::cleanGpu(arg.toString());
        } else if (fn == u"parseHw"_s) {
            SensorParser p;
            p.parseHw(arg.toString());
            r = QJsonObject{ { u"cpu"_s, p.hw.cpu }, { u"cores"_s, p.hw.cores }, { u"gpu"_s, p.hw.gpu }, { u"igpu"_s, p.hw.igpu } };
        } else if (fn == u"sensors"_s) {
            // A sequence of rounds on one parser, all at the same moment (the QML side uses its own clock)
            SensorParser p;
            const qint64 now = 1'000'000'000;
            p.parseHw(q.value(u"hw"_s).toString());
            QJsonArray steps;
            for (const QJsonValue &round : q.value(u"rounds"_s).toArray()) {
                p.parse(round.toString(), now);
                steps.append(readingsState(p, now));
            }
            r = steps;
        } else if (fn == u"reader"_s) {
            Paths paths;
            paths.sysRoot = q.value(u"root"_s).toString();
            r = SensorReader::round(paths, q.value(u"gpu"_s).toBool());
        } else if (fn == u"hwinfo"_s) {
            Paths paths;
            paths.sysRoot = q.value(u"root"_s).toString();
            r = SensorReader::hwInfo(paths);
        } else {
            r = u"unknown fn"_s;
        }
        out.write(QJsonDocument(QJsonArray{ r }).toJson(QJsonDocument::Compact) + '\n');
        out.flush();
    }
    return 0;
}
