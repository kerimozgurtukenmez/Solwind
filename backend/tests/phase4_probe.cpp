// Answers keyboard calls, one JSON object per line on stdin -> one JSON line on stdout, so the same inputs can be run
// through services/Keyboard.qml + scripts/kbdinfo.sh and compared (.dev/phase4diff.sh):
//   {"fn": "scan", "root": "<fake root>", "udev": "<fake udev root>"}          -> KeyboardScan's text
//   {"fn": "parse", "text", "ready", "kbd", "ui", "effect", "color", "prevDmi", "prevKernel"} -> the state after parse()
//   {"fn": "value", "text", "c", "level"}                                       -> colorValue / frame on that light
//   {"fn": "mixHsv", "a", "b", "f"}  {"fn": "vivid", "c"}  {"fn": "theme", "primary", "secondary", "tertiary", "accents", "count"}
//   {"fn": "tick", "text", "effect", "source", "colors", "color", "speed", "min", "max", "t0", "now", "theme": {...}}
//   {"fn": "range", "min", "max", "steps": [[lo, hi], ...]}   {"fn": "speed", "t0", "now", "speed", "v"}
// A colour is "#rrggbb" or [r, g, b] (Qt.rgba). Nothing is written to any light (a fresh controller per call, observe
// mode, its settings in a temporary folder; HOME should point at a scratch folder for the theme's scheme.json).
// The keyboard plugins: HP's three (since P2 the only ones; the harness gives the QML copy the same set).
#include "HpWmiKeyboard.h"
#include "HpWmiKeyboardZones.h"
#include "HpWmiRgbZones.h"
#include "core/I18n.h"
#include "core/KeyboardController.h"
#include "core/KeyboardScan.h"
#include "core/Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <memory>

using namespace Qt::StringLiterals;

namespace {
QColor colour(const QJsonValue &v)
{
    if (v.isString()) return QColor::fromString(v.toString());
    const QJsonArray a = v.toArray();
    return Qml::rgba(a.at(0).toDouble(), a.at(1).toDouble(), a.at(2).toDouble(), 1);
}
QJsonArray floats(const QColor &c) { return { c.redF(), c.greenF(), c.blueF() }; }
QJsonObject settingsOf(const QJsonObject &q)
{
    QJsonObject d{ { u"version"_s, 1 }, { u"configs"_s, QJsonObject() } };
    if (q.contains(u"kbd"_s)) d.insert(u"kbd"_s, q.value(u"kbd"_s));
    if (q.contains(u"ui"_s)) d.insert(u"ui"_s, q.value(u"ui"_s));
    return d;
}

} // namespace

struct KeyboardProbe {
    static QJsonValue one(const QJsonObject &q);
    QTemporaryDir dir;
    std::unique_ptr<Settings> settings;
    I18n i18n;
    std::unique_ptr<KeyboardController> k;
    qint64 clock = 0;

    explicit KeyboardProbe(const QJsonObject &settingsData, const QJsonObject &scheme = {})
    {
        QDir().mkpath(dir.path() + u"/solwind"_s);
        QFile f(dir.path() + u"/solwind/settings.json"_s);
        if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(settingsData).toJson());
        f.close();
        settings = std::make_unique<Settings>(dir.path() + u"/solwind"_s);
        settings->load();
        i18n.savedLanguage = [this] { return settings->uiLang(); };
        Paths p;
        p.home = dir.path();
        if (!scheme.isEmpty()) {
            QDir().mkpath(dir.path() + u"/.local/state/caelestia"_s);
            QFile s(dir.path() + u"/.local/state/caelestia/scheme.json"_s);
            if (s.open(QIODevice::WriteOnly)) s.write(QJsonDocument(QJsonObject{ { u"colours"_s, scheme } }).toJson());
        }
        k = std::make_unique<KeyboardController>(p, *settings, i18n, false);
        static const HpWmiKeyboard single;
        static const HpWmiKeyboardZones zones;
        static const HpWmiRgbZones rgbZones;
        k->setPlugins({ &zones, &rgbZones, &single });    // (their priorities' order)
        k->now = [this] { return clock; };
    }
    QJsonObject state() const
    {
        const QColor c = k->m_color;
        QJsonObject pending;
        if (k->m_pendingColor) pending.insert(u"color"_s, *k->m_pendingColor);
        if (k->m_pendingBrightness) pending.insert(u"brightness"_s, *k->m_pendingBrightness);
        QJsonArray mi, ch;
        for (double x : k->m_format.maxIntensity) mi << x;
        for (int x : k->m_format.channels) ch << x;
        return {
            { u"backend"_s, k->backend() }, { u"zones"_s, QJsonArray::fromStringList(k->zones()) }, { u"name"_s, k->name() },
            { u"driver"_s, k->driver() }, { u"writable"_s, k->writable() }, { u"maxBrightness"_s, k->maxBrightness() },
            { u"brightness"_s, k->brightness() }, { u"maxIntensity"_s, mi }, { u"channels"_s, ch },
            { u"color"_s, QJsonArray{ c.redF(), c.greenF(), c.blueF() } }, { u"hex"_s, Qml::hex(c) },
            { u"notes"_s, QJsonArray::fromStringList(k->notes()) }, { u"ruleAt"_s, k->ruleAt() },
            { u"ruleLegacy"_s, k->ruleLegacy() }, { u"ruleOld"_s, k->ruleOld() }, { u"dmi"_s, QJsonArray::fromStringList(k->dmi()) },
            { u"kernel"_s, k->kernel() }, { u"rgb"_s, k->rgb() }, { u"hasBrightness"_s, k->hasBrightness() },
            { u"effectsAllowed"_s, k->effectsAllowed() }, { u"tested"_s, k->tested() }, { u"ready"_s, k->ready() },
            { u"effect"_s, k->effect() }, { u"source"_s, k->source() }, { u"colors"_s, QJsonArray::fromStringList(k->colors()) },
            { u"speed"_s, k->speed() }, { u"themeCount"_s, k->themeCount() }, { u"minLevel"_s, k->minLevel() },
            { u"maxLevel"_s, k->maxLevel() }, { u"pending"_s, pending },
            { u"kind"_s, k->kindText() }, { u"info"_s, k->infoText() }, { u"support"_s, k->supportText() },
            { u"status"_s, k->statusText() },
        };
    }
};


QJsonValue KeyboardProbe::one(const QJsonObject &q)
{
    const QString fn = q.value(u"fn"_s).toString();
    if (fn == u"scan"_s) {
        Paths p;
        p.sysRoot = q.value(u"root"_s).toString();
        p.udevRoot = q.value(u"udev"_s).toString();
        return KeyboardScan::scan(p);
    }
    if (fn == u"parse"_s || fn == u"value"_s) {
        KeyboardProbe pr(settingsOf(q));
        pr.k->m_ready = q.value(u"ready"_s).toBool(fn == u"value"_s);
        if (q.contains(u"effect"_s)) pr.k->m_effect = q.value(u"effect"_s).toString();
        if (q.contains(u"color"_s)) pr.k->m_color = colour(q.value(u"color"_s));
        if (q.contains(u"prevDmi"_s)) {
            QStringList d;
            for (const QJsonValue &x : q.value(u"prevDmi"_s).toArray()) d << x.toString();
            pr.k->m_dmi = d;
        }
        if (q.contains(u"prevKernel"_s)) pr.k->m_kernel = q.value(u"prevKernel"_s).toString();
        pr.k->parse(q.value(u"text"_s).toString());
        if (fn == u"parse"_s) return pr.state();
        const QColor c = colour(q.value(u"c"_s));
        const double level = q.value(u"level"_s).toDouble();
        return QJsonObject{ { u"value"_s, pr.k->colorValue(c, level) }, { u"frame"_s, pr.k->frame(c, level) } };
    }
    if (fn == u"perm"_s) {
        KeyboardProbe pr(settingsOf(q));
        pr.k->m_paths.appDir = q.value(u"appDir"_s).toString();
        return QJsonArray::fromStringList(pr.k->permissionCommand(q.value(u"grant"_s).toBool()));
    }
    if (fn == u"mixHsv"_s) return floats(KeyboardController::mixHsv(colour(q.value(u"a"_s)), colour(q.value(u"b"_s)), q.value(u"f"_s).toDouble()));
    if (fn == u"vivid"_s) return floats(KeyboardController::vivid(colour(q.value(u"c"_s))));
    if (fn == u"theme"_s) {
        QStringList acc;
        for (const QJsonValue &x : q.value(u"accents"_s).toArray()) acc << x.toString();
        QJsonArray out;
        for (const QColor &c : KeyboardController::themeColorsOf(colour(q.value(u"primary"_s)), colour(q.value(u"secondary"_s)),
                                                                  colour(q.value(u"tertiary"_s)), acc, q.value(u"count"_s).toInt()))
            out << floats(c);
        return out;
    }
    if (fn == u"tick"_s) {
        KeyboardProbe pr(settingsOf(q), q.value(u"theme"_s).toObject());
        pr.k->parse(q.value(u"text"_s).toString());
        pr.k->m_effect = q.value(u"effect"_s).toString();
        pr.k->m_source = q.value(u"source"_s).toString();
        QStringList cs;
        for (const QJsonValue &x : q.value(u"colors"_s).toArray()) cs << x.toString();
        pr.k->m_colors = cs;
        pr.k->m_color = colour(q.value(u"color"_s));
        pr.k->m_themeCount = q.value(u"themeCount"_s).toInt(3);
        pr.k->m_speed = q.value(u"speed"_s).toDouble();
        pr.k->m_minLevel = q.value(u"min"_s).toDouble();
        pr.k->m_maxLevel = q.value(u"max"_s).toDouble();
        pr.k->m_t0 = q.value(u"t0"_s).toDouble();
        pr.clock = qint64(q.value(u"now"_s).toDouble());
        pr.k->m_lastFrame.clear();
        QString written;
        QObject::connect(pr.k.get(), &KeyboardController::wroteFrame, [&](const QString &v) { written = v; });
        pr.k->tick();
        return QJsonObject{ { u"frame"_s, written }, { u"cycleIndex"_s, pr.k->cycleIndex() }, { u"t0"_s, pr.k->m_t0 } };
    }
    if (fn == u"range"_s) {
        KeyboardProbe pr(settingsOf(q));
        pr.k->m_minLevel = q.value(u"min"_s).toDouble();
        pr.k->m_maxLevel = q.value(u"max"_s).toDouble();
        QJsonArray out;
        for (const QJsonValue &s : q.value(u"steps"_s).toArray()) {
            pr.k->setRange(s.toArray().at(0).toDouble(), s.toArray().at(1).toDouble());
            out << QJsonArray{ pr.k->minLevel(), pr.k->maxLevel() };
        }
        return out;
    }
    if (fn == u"speed"_s) {
        KeyboardProbe pr(settingsOf(q));
        pr.k->m_t0 = q.value(u"t0"_s).toDouble();
        pr.k->m_speed = q.value(u"speed"_s).toDouble();
        pr.clock = qint64(q.value(u"now"_s).toDouble());
        pr.k->setSpeed(q.value(u"v"_s).toDouble());
        return QJsonObject{ { u"t0"_s, pr.k->m_t0 }, { u"speed"_s, pr.k->speed() }, { u"period"_s, pr.k->period() } };
    }
    return u"unknown fn"_s;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QFile in, out;
    if (!in.open(stdin, QIODevice::ReadOnly) || !out.open(stdout, QIODevice::WriteOnly)) return 1;
    while (!in.atEnd()) {
        const QByteArray line = in.readLine().trimmed();
        if (line.isEmpty()) continue;
        out.write(QJsonDocument(QJsonArray{ KeyboardProbe::one(QJsonDocument::fromJson(line).object()) }).toJson(QJsonDocument::Compact) + '\n');
        out.flush();
    }
    return 0;
}
