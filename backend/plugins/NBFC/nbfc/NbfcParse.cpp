#include "NbfcParse.h"

#include "core/JsCompat.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <cmath>

using namespace Qt::StringLiterals;

namespace Nbfc {

QString firstLine(const QString &s)
{
    static const QRegularExpression info(u"^"_s + QString::fromUtf16(js::reSpace) + u"*INFO:"_s);
    const QStringList all = js::trim(s).split(u'\n');
    for (const QString &l : all) if (!info.match(l).hasMatch()) return l;
    return all.first();
}

QStringList lines(const QString &s)
{
    QStringList out;
    for (const QString &l : s.split(u'\n')) {
        const QString t = js::trim(l);
        if (!t.isEmpty()) out << t;
    }
    return out;
}

bool isTrue(const QString &v)
{
    static const QRegularExpression re(u"^(true|yes|on|enabled|1)"_s, QRegularExpression::CaseInsensitiveOption);
    return re.match(js::trim(v)).hasMatch();
}

Status parseStatus(const QString &txt)
{
    Status res;
    struct Fan : FanStatus { bool curSeen = false; };
    QList<Fan> fans;
    qsizetype fan = -1;                                   // index of the fan the lines belong to
    auto fresh = [&](const QString &name) { Fan f; f.name = name; fans.append(f); return fans.size() - 1; };
    for (const QString &line : txt.split(u'\n')) {
        const qsizetype i = line.indexOf(u':');
        if (i < 0) continue;
        const QString k = js::trim(line.left(i)).toLower();
        const QString v = js::trim(line.mid(i + 1));
        auto has = [&](const char16_t *w) { return k.contains(QStringView(w)); };
        if (has(u"fan") && has(u"name")) { fan = fresh(v); continue; }
        if (fan < 0 && has(u"config") && has(u"name")) { res.config = v; continue; }   // "Selected Config Name"
        if (has(u"read") && has(u"only")) { res.readOnly = isTrue(v); continue; }
        if (has(u"temp")) {
            const double n = js::parseFloat(v);
            if (fan >= 0) fans[fan].temp = n; else res.temp = n;
            continue;
        }
        if (has(u"current") && has(u"speed")) {
            if (fan < 0 || fans[fan].curSeen) fan = fresh(u"Fan %1"_s.arg(fans.size() + 1));   // output without names
            const double c = js::parseFloat(v);
            fans[fan].current = std::isnan(c) || c == 0 ? 0 : c;                                // parseFloat(v) || 0
            fans[fan].curSeen = true;
            continue;
        }
        if (fan < 0) continue;
        if (has(u"target") && has(u"speed")) { const double t = js::parseFloat(v); fans[fan].target = std::isnan(t) ? 0 : t; }
        else if (has(u"critical")) fans[fan].critical = isTrue(v);
        else if (has(u"auto")) fans[fan].autoControl = isTrue(v);
    }
    // N-8: the service's temperature, else the hottest fan's
    if (!std::isfinite(res.temp)) {
        double m = NaN;
        for (const Fan &f : fans) if (std::isfinite(f.temp)) m = std::isfinite(m) ? std::max(m, f.temp) : f.temp;
        res.temp = m;
    }
    for (const Fan &f : fans) res.fans.append(f);
    return res;
}

std::optional<ConfigCurve> toCurve(const QJsonValue &th, bool *thrown)
{
    if (!th.isArray() || th.toArray().isEmpty()) return std::nullopt;
    struct Row { double up, down, s; };
    QList<Row> rows;
    for (const QJsonValue &r : th.toArray()) {
        // r.UpThreshold on null throws (the whole preview becomes []); on other non-objects it is undefined
        if (r.isNull() || r.isUndefined()) { if (thrown) *thrown = true; return std::nullopt; }
        const QJsonObject o = r.toObject();
        rows.append({ js::number(o.value(u"UpThreshold"_s)), js::number(o.value(u"DownThreshold"_s)),
                      js::number(o.value(u"FanSpeed"_s)) });
    }
    rows.removeIf([](const Row &r) { return !std::isfinite(r.up) || !std::isfinite(r.s); });
    js::qv4Sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) { return a.up - b.up < 0; });
    ConfigCurve c;
    for (const Row &r : rows) {
        int t = int(std::max(30.0, std::min(100.0, js::round(r.up))));          // the editor's range 30..100
        if (!c.points.isEmpty() && t <= c.points.last().t) t = c.points.last().t + 1;
        if (t > 100) break;
        c.points.append({ t, int(std::max(0.0, std::min(100.0, js::round(r.s)))) });
    }
    if (c.points.size() < 2) return std::nullopt;
    double sum = 0;
    int n = 0;
    for (const Row &r : rows) if (r.up > 0 && std::isfinite(r.down)) { sum += r.up - r.down; ++n; }
    c.hyst = n ? int(std::max(0.0, std::min(10.0, js::round(sum / n)))) : 3;
    return c;
}

QList<std::optional<ConfigCurve>> configCurves(const QByteArray &fileText)
{
    // (JSON.parse(out).FanConfigurations || []).map(f => toCurve(f.TemperatureThresholds)), [] on any throw
    const auto doc = js::parseJson(fileText);
    if (!doc) return {};
    if (doc->isNull()) return {};                                        // null.FanConfigurations throws
    QJsonValue fc;                                                       // other scalars, arrays: undefined
    if (doc->isObject()) fc = doc->toObject().value(u"FanConfigurations"_s);
    if (!js::truthy(fc)) return {};                                      // || [] -> nothing
    if (!fc.isArray()) return {};                                        // .map isn't a function -> throws
    QList<std::optional<ConfigCurve>> out;
    for (const QJsonValue &f : fc.toArray()) {
        if (f.isNull() || f.isUndefined()) return {};                    // f.TemperatureThresholds on null throws
        bool thrown = false;
        out.append(toCurve(f.isObject() ? f.toObject().value(u"TemperatureThresholds"_s) : QJsonValue(), &thrown));
        if (thrown) return {};
    }
    return out;
}

QStringList mergeConfigs(const QStringList &fromDirs, const QStringList &fromCli, const QString &configId,
                         const QString &fileConfigId)
{
    QStringList src = fromDirs.isEmpty() ? fromCli : fromDirs;
    for (const QString &c : { configId, fileConfigId }) if (!c.isEmpty()) src << c;
    QSet<QString> seen;
    QStringList all;
    for (const QString &c : src) if (!seen.contains(c)) { seen.insert(c); all << c; }
    // a.localeCompare(b) in the QML engine, with its own sort
    js::qv4Sort(all.begin(), all.end(), [](const QString &a, const QString &b) { return QString::localeAwareCompare(a, b) < 0; });
    return all;
}

std::optional<QString> selectedConfigId(const QByteArray &text, QString *why)
{
    const auto v = js::parseJson(text, why);
    if (!v) return std::nullopt;
    if (v->isNull()) { if (why) *why = u"null"_s; return std::nullopt; }      // null.SelectedConfigId throws
    const QJsonValue id = v->isObject() ? v->toObject().value(u"SelectedConfigId"_s) : QJsonValue();
    if (!js::truthy(id)) return QString();
    // An object can't go into the QML's string property (it threw: the id was kept). An array went in as garbage
    // (measured: [1,2] -> "\u0001\u0002"); kept here too, on purpose (SPEC.md N-13 note).
    if (id.isObject() || id.isArray()) { if (why) *why = u"SelectedConfigId is not a name"_s; return std::nullopt; }
    return id.isString() ? id.toString() : js::toString(id);
}

QStringList parseConfigList(const QString &out)
{
    static const QRegularExpression noise(u"^(usage|error|warning)"_s, QRegularExpression::CaseInsensitiveOption);
    QStringList r;
    for (const QString &l : lines(out)) if (!noise.match(l).hasMatch()) r << l;
    return r;
}

QStringList matchRecommendations(const QString &out, const QStringList &known)
{
    static const QRegularExpression cols(QString::fromUtf16(js::reSpace) + u"{2,}|\\t"_s);
    const QSet<QString> k(known.begin(), known.end());
    QStringList found;
    for (const QString &l : lines(out)) {
        for (const QString &c : { l, js::trim(l.split(cols).first()) }) {
            if (k.contains(c) && !found.contains(c)) { found << c; break; }
        }
    }
    return found;
}

} // namespace Nbfc
