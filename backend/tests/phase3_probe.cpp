// Runs fan control scenarios through the C++ core (FanController, FanState, Settings, I18n), one JSON scenario per
// line on stdin -> one JSON object { steps: [snapshot, ...] } per line on stdout, so the same scenarios can be run
// through the QML app's services/Nbfc.qml + FanState.qml and compared (.dev/phase3diff.sh, which also describes the
// scenario format). The plugin here is the QML's NBFC part, rule for rule: one nbfc command at a time, a waiting one
// replaced by a newer one with the same key, the next one started a moment after one ends (Qt.callLater), and the
// status read of Nbfc.qml poll() / applyStatus(). Wired as solwindd's main.cpp wires the real plugin. Runs nothing.
#include "core/FanController.h"
#include "core/FanState.h"
#include "core/I18n.h"
#include "core/JsCompat.h"
#include "core/Notifier.h"
#include "core/Settings.h"
#include "plugins/FanPlugin.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimer>

#include <cmath>
#include <optional>

using namespace Qt::StringLiterals;

namespace {
constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

double num(const QJsonValue &v) { return v.isDouble() ? v.toDouble() : NaN; }       // null = NaN
QJsonValue out(double v)
{
    if (std::isinf(v)) return v > 0 ? u"inf"_s : u"-inf"_s;
    return std::isfinite(v) ? QJsonValue(v) : QJsonValue(QJsonValue::Null);          // JSON.stringify(NaN) = null
}
FanPlugin::Fan fanFrom(const QJsonObject &o)
{
    FanPlugin::Fan f;
    f.name = o.value(u"name"_s).toString();
    f.temp = num(o.value(u"temp"_s));
    f.autoControl = o.value(u"auto"_s).toBool();
    f.critical = o.value(u"critical"_s).toBool();
    f.current = o.value(u"current"_s).toDouble();
    f.target = o.value(u"target"_s).toDouble();
    return f;
}
// A scenario's st is always complete and valid (the generator makes it so), as the QML UI hands it over
FanSt stFrom(const QJsonValue &v)
{
    const QJsonObject o = v.toObject();
    FanSt s;
    s.mode = o.value(u"mode"_s).toInt();
    s.fixed = o.value(u"fixed"_s).toDouble();
    for (const QJsonValue &p : o.value(u"curve"_s).toArray())
        s.curve.append({ p.toObject().value(u"t"_s).toInt(), p.toObject().value(u"s"_s).toInt() });
    s.smooth = o.value(u"smooth"_s).toBool();
    s.src = o.value(u"src"_s).toString();
    s.hyst = o.value(u"hyst"_s).toInt();
    return s;
}

class ProbePlugin : public FanPlugin
{
public:
    // What NBFC would answer
    struct {
        bool running = true, readOnly = false, fixedSaved = false;
        QString config;
        QList<Fan> fans;
    } sim;
    std::function<qint64()> now;

    // As the last status read left it (Nbfc.qml poll / applyStatus)
    bool isRunning = false, isReadOnly = false, isHolding = false;
    qint64 quietUntil = 0;
    QString serviceConfig, fileConfig, config;
    int failedPolls = 0;
    double driverTemp = NaN;
    QList<Fan> fanList;
    quint64 polls = 0, reads = 0, readSeq = 0;
    bool reading = false;

    struct Command { QString key, text; Done done; };
    std::optional<Command> runningCmd;
    QList<Command> waiting;
    QStringList started;          // since the last snapshot

    QString id() const override { return u"probe"_s; }
    Trust trust() const override { return Trust::WriteTested; }
    bool running() const override { return isRunning; }
    bool readOnly() const override { return isReadOnly; }
    bool holding() const override { return isHolding; }
    bool quiet() const override
    {
        const qint64 left = quietUntil - now();          // Nbfc.qml adminQuiet() (no admin queue here)
        return left > 0 && left <= 5000;
    }
    QString configId() const override { return config; }
    double temp() const override { return driverTemp; }
    const QList<Fan> &fans() const override { return fanList; }
    quint64 pollSeq() const override { return polls; }
    quint64 statusSeq() const override { return reads; }
    const I18n *i18n = nullptr;
    // Nbfc.qml boostBlocked()
    QString blockedReason(bool withHolding) const override
    {
        if (!isRunning) return i18n->t(u"The NBFC service is not running"_s);
        if (isReadOnly) return i18n->t(u"NBFC is in read-only mode, fan speeds can't be written"_s);
        if (withHolding && isHolding) return i18n->t(u"Waiting for the NBFC service action to finish"_s);
        return QString();
    }

    // ---------- the queue (Nbfc.qml enqueue / pump) ----------
    void enqueue(const QString &key, const QString &text, Done done)
    {
        QList<Command> q;
        for (Command &e : waiting) if (key.isEmpty() || e.key != key) q.append(std::move(e));
        q.append({ key, text, std::move(done) });
        waiting = std::move(q);
        pump();
    }
    void pump()
    {
        if (runningCmd || waiting.isEmpty()) return;
        runningCmd = waiting.takeFirst();
        started << runningCmd->text;
        // The saved speeds' file is read at once (a moment later), as the QML harness answers `cat`
        if (runningCmd->text.startsWith(u"cat "_s)) QTimer::singleShot(0, [this] { finish(true, QString()); });
    }
    void finish(bool ok, const QString &err)
    {
        if (!runningCmd) return;
        Command c = std::move(*runningCmd);
        runningCmd.reset();
        if (ok) {
            const QStringList w = c.text.split(u' ');
            if (w.size() == 6 && w[1] == u"set" && w[4] == u"-s" && w[3].toInt() < sim.fans.size()) {
                sim.fans[w[3].toInt()].autoControl = false;
                sim.fans[w[3].toInt()].target = w[5].toDouble();
            } else if (w.size() == 5 && w[1] == u"set" && w[4] == u"-a" && w[3].toInt() < sim.fans.size()) {
                sim.fans[w[3].toInt()].autoControl = true;
            } else if (w.size() == 3 && w[1] == u"set" && w[2] == u"-a") {
                for (Fan &f : sim.fans) f.autoControl = true;
            }
        }
        if (c.done) c.done(ok, err);
        QTimer::singleShot(0, [this] { pump(); });         // Qt.callLater(root.pump)
    }

    void setPercent(int fan, int pct, Done done) override
    {
        enqueue(u"spd:%1"_s.arg(fan), u"nbfc set -f %1 -s %2"_s.arg(fan).arg(pct), std::move(done));
    }
    void setAuto(int fan, Done done) override
    {
        if (fan < 0) enqueue(u"auto:all"_s, u"nbfc set -a"_s, std::move(done));
        else enqueue(u"auto:%1"_s.arg(fan), u"nbfc set -f %1 -a"_s.arg(fan), std::move(done));
    }
    void releaseAll(Done done) override { enqueue(u"quit"_s, u"nbfc set -a"_s, std::move(done)); }
    // In the queue like the QML's `cat /var/lib/nbfc/state.json` (key "quit"), so the order compares
    void willRestoreFixedSpeeds(std::function<void(bool)> done) override
    {
        enqueue(u"quit"_s, u"cat /var/lib/nbfc/state.json"_s, [this, done](bool, const QString &) { done(sim.fixedSaved); });
    }

    // ---------- the status read (Nbfc.qml poll) ----------
    void updateConfigId()
    {
        const QString id = !serviceConfig.isEmpty() ? serviceConfig : fileConfig;
        if (id != config) { config = id; emit configIdChanged(); }
    }
    void readStart()
    {
        if (reading) return;                               // statusCmd.busy
        readSeq = ++polls;
        reading = true;
    }
    void readEnd()
    {
        if (!reading) return;
        reading = false;
        const bool ok = sim.running;
        reads = readSeq;
        isRunning = ok;
        failedPolls = ok ? 0 : failedPolls + 1;
        if (failedPolls >= 2 && !serviceConfig.isEmpty()) { serviceConfig.clear(); updateConfigId(); }
        if (!ok) { driverTemp = NaN; emit statusRead(false); return; }
        if (!sim.config.isEmpty()) { serviceConfig = sim.config; updateConfigId(); }
        isReadOnly = sim.readOnly;
        double t = NaN;                                    // no temperature of its own: the hottest fan's
        for (const Fan &f : sim.fans) if (std::isfinite(f.temp)) t = std::isfinite(t) ? js::max(t, f.temp) : f.temp;
        driverTemp = t;
        fanList = sim.fans;
        emit statusRead(true);
    }
};

void settle()
{
    for (int i = 0; i < 50; ++i) QCoreApplication::processEvents(QEventLoop::AllEvents);
}

QJsonObject stJson(const FanSt &s) { return s.toJson(); }

QJsonObject runOne(const QJsonObject &sc)
{
    QTemporaryDir dir;
    if (!sc.value(u"settings"_s).isNull()) {
        QDir().mkpath(dir.path() + u"/solwind"_s);
        QFile f(dir.path() + u"/solwind/settings.json"_s);
        if (!f.open(QIODevice::WriteOnly)) return { { u"error"_s, u"settings file"_s } };
        f.write(QJsonDocument(sc.value(u"settings"_s).toObject()).toJson());
    }
    const qint64 clock0 = qint64(sc.value(u"clock0"_s).toDouble());
    qint64 clock = clock0;
    double gpu = NaN;

    Settings settings(dir.path() + u"/solwind"_s);
    settings.load();
    I18n i18n;
    i18n.savedLanguage = [&] { return settings.uiLang(); };
    Notifier notifier(QDBusConnection::connectToBus(u"unix:path=/nonexistent/solwind-probe"_s, u"none"_s));
    notifier.enabled = [&] { return settings.notifyOn(u"enabled"_s); };
    ProbePlugin plugin;
    plugin.now = [&] { return clock; };
    plugin.i18n = &i18n;
    const QJsonObject sim = sc.value(u"sim"_s).toObject();
    plugin.sim.running = sim.value(u"running"_s).toBool();
    plugin.sim.readOnly = sim.value(u"readOnly"_s).toBool();
    plugin.sim.fixedSaved = sim.value(u"fixedSaved"_s).toBool();
    plugin.sim.config = sim.value(u"config"_s).toString();
    for (const QJsonValue &f : sim.value(u"fans"_s).toArray()) plugin.sim.fans << fanFrom(f.toObject());
    plugin.fileConfig = sim.value(u"fileConfig"_s).toString();
    plugin.updateConfigId();
    FanController fans(plugin, i18n);
    fans.now = [&] { return clock; };
    fans.gpuTemp = [&] { return gpu; };
    fans.setSafety(std::isfinite(settings.safety()) ? settings.safety() : 90);
    FanState state(plugin, fans, settings, i18n, notifier);

    QJsonArray events;
    QObject::connect(&fans, &FanController::commandFailed, [&](const QString &m) { events.append(QJsonArray{ u"commandFailed"_s, m }); });
    QObject::connect(&fans, &FanController::releaseFailed, [&](const QString &m) { events.append(QJsonArray{ u"releaseFailed"_s, m }); });
    QObject::connect(&fans, &FanController::boostEnded, [&] { events.append(QJsonArray{ u"boostEnded"_s }); });
    QObject::connect(&notifier, &Notifier::sent, [&](const QString &title, const QString &body, int urgency, const QString &icon) {
        static const char *const names[] = { "low", "normal", "critical" };
        events.append(QJsonArray{ u"notify"_s, title, body, QString::fromLatin1(names[urgency]),
                                  icon.isEmpty() ? u"dialog-information"_s : icon });
    });
    // As main.cpp
    QObject::connect(&plugin, &FanPlugin::configIdChanged, [&] { fans.forgetFans(); state.unrestore(); });
    QObject::connect(&plugin, &FanPlugin::statusRead, [&](bool ok) {
        if (!ok) return;
        fans.checkBoost();
        fans.control();
        QTimer::singleShot(0, &state, [&] { if (!fans.quitting()) state.restore(); });
    });
    auto quit = [&] {
        fans.quit([&](const FanController::QuitResult &r) {
            if (r.handBackFailed)
                notifier.send(i18n.t(u"Fans may stay at a fixed speed"_s),
                              i18n.t(u"Solwind could not hand the fans back to NBFC's automatic control (%1). Run `nbfc set -a` or open Solwind."_s).arg(r.error),
                              Notifier::Critical, u"dialog-warning"_s, true, false);
            if (r.fixedSpeedsSaved)
                notifier.send(i18n.t(u"Fans may stay at a fixed speed"_s),
                              i18n.t(u"NBFC is stopped and has saved fixed fan speeds; it sets them again when it starts. After starting it, run `nbfc set -a` or open Solwind."_s),
                              Notifier::Normal, u"dialog-warning"_s, true, false);
            events.append(QJsonArray{ u"quit"_s });
        });
    };

    auto snapshot = [&](const QJsonValue &ret) {
        QJsonObject targets, lastSent, sentSeq, level, releasing;
        for (auto it = fans.targets().cbegin(); it != fans.targets().cend(); ++it) targets.insert(QString::number(it.key()), stJson(*it));
        for (auto it = fans.lastSent().cbegin(); it != fans.lastSent().cend(); ++it) lastSent.insert(QString::number(it.key()), *it);
        for (auto it = fans.sentSeq().cbegin(); it != fans.sentSeq().cend(); ++it) sentSeq.insert(QString::number(it.key()), out(*it));
        for (auto it = fans.levels().cbegin(); it != fans.levels().cend(); ++it) level.insert(QString::number(it.key()), out(*it));
        for (auto it = fans.releasing().cbegin(); it != fans.releasing().cend(); ++it)
            releasing.insert(QString::number(it.key()), QJsonObject{ { u"at"_s, double(it->at - clock0) }, { u"seq"_s, double(it->seq) },
                                                                    { u"tries"_s, it->tries } });
        QJsonArray hot;
        for (auto it = fans.hotFans().cbegin(); it != fans.hotFans().cend(); ++it) if (*it) hot.append(it.key());
        QJsonArray waiting, fanSt;
        for (const auto &c : plugin.waiting) waiting.append(c.text);
        for (const FanSt &s : state.fanSt()) fanSt.append(stJson(s));
        const qint64 b = fans.boostUntil();
        QJsonObject snap{
            { u"started"_s, QJsonArray::fromStringList(plugin.started) },
            { u"waiting"_s, waiting },
            { u"running"_s, plugin.runningCmd ? QJsonValue(plugin.runningCmd->text) : QJsonValue(QJsonValue::Null) },
            { u"events"_s, events },
            { u"c"_s, QJsonObject{ { u"targets"_s, targets }, { u"lastSent"_s, lastSent }, { u"sentSeq"_s, sentSeq },
                                   { u"level"_s, level }, { u"hot"_s, hot }, { u"releasing"_s, releasing },
                                   { u"boost"_s, double(b > 0 ? b - clock0 : b) }, { u"safety"_s, fans.safety() },
                                   { u"running"_s, plugin.isRunning }, { u"readOnly"_s, plugin.isReadOnly },
                                   { u"configId"_s, plugin.config }, { u"pollSeq"_s, double(plugin.polls) },
                                   { u"statusSeq"_s, double(plugin.reads) } } },
            { u"s"_s, QJsonObject{ { u"fanSt"_s, fanSt }, { u"globalSt"_s, stJson(state.globalSt()) }, { u"globalOn"_s, state.globalOn() },
                                   { u"restored"_s, state.restored() }, { u"active"_s, state.activeId() },
                                   { u"activeName"_s, state.activeName() }, { u"status"_s, state.statusText(gpu) },
                                   { u"profiles"_s, state.profileList() } } },
            { u"settings"_s, settings.data() },
            { u"ret"_s, ret } };
        plugin.started.clear();
        events = QJsonArray();
        return snap;
    };

    auto step = [&](const QJsonObject &s) -> QJsonValue {
        const QString op = s.value(u"op"_s).toString();
        if (op == u"read") { plugin.readStart(); plugin.readEnd(); }
        else if (op == u"readStart") plugin.readStart();
        else if (op == u"readEnd") plugin.readEnd();
        else if (op == u"finish") plugin.finish(s.value(u"ok"_s).toBool(), s.value(u"err"_s).toString());
        else if (op == u"finishAll") {
            for (int n = 0; n < 50 && plugin.runningCmd; ++n) { plugin.finish(s.value(u"ok"_s).toBool(), s.value(u"err"_s).toString()); settle(); }
        }
        else if (op == u"clock") clock += qint64(s.value(u"ms"_s).toDouble());
        else if (op == u"gpu") gpu = num(s.value(u"v"_s));
        else if (op == u"simFan") {
            const int i = s.value(u"fan"_s).toInt();
            if (i < plugin.sim.fans.size()) {
                ProbePlugin::Fan &f = plugin.sim.fans[i];
                if (s.contains(u"temp"_s)) f.temp = num(s.value(u"temp"_s));
                if (s.contains(u"auto"_s)) f.autoControl = s.value(u"auto"_s).toBool();
                if (s.contains(u"critical"_s)) f.critical = s.value(u"critical"_s).toBool();
                if (s.contains(u"target"_s)) f.target = s.value(u"target"_s).toDouble();
                if (s.contains(u"current"_s)) f.current = s.value(u"current"_s).toDouble();
            }
        }
        else if (op == u"simFans") {
            plugin.sim.fans.clear();
            for (const QJsonValue &f : s.value(u"fans"_s).toArray()) plugin.sim.fans << fanFrom(f.toObject());
        }
        else if (op == u"sim") {
            if (s.contains(u"running"_s)) plugin.sim.running = s.value(u"running"_s).toBool();
            if (s.contains(u"readOnly"_s)) plugin.sim.readOnly = s.value(u"readOnly"_s).toBool();
            if (s.contains(u"config"_s)) plugin.sim.config = s.value(u"config"_s).toString();
            if (s.contains(u"fixedSaved"_s)) plugin.sim.fixedSaved = s.value(u"fixedSaved"_s).toBool();
        }
        else if (op == u"hold") plugin.isHolding = s.value(u"v"_s).toBool();
        else if (op == u"quiet") plugin.quietUntil = clock + qint64(s.value(u"ms"_s).toDouble());
        else if (op == u"setFan") {
            const int i = s.value(u"fan"_s).toInt();
            if (i >= state.fanSt().size()) return u"skipped"_s;
            state.setFan(i, stFrom(s.value(u"st"_s)));
        }
        else if (op == u"setGlobal") state.setGlobal(stFrom(s.value(u"st"_s)));
        else if (op == u"setGlobalOn") state.setGlobalOn(s.value(u"on"_s).toBool());
        else if (op == u"apply") {
            const FanState::Result r = state.applyByName(s.value(u"name"_s).toString(), s.value(u"ipc"_s).toBool());
            return QJsonObject{ { u"ok"_s, r.ok }, { u"usage"_s, r.usage }, { u"text"_s, r.text } };
        }
        else if (op == u"boost") return fans.setBoost(qint64(s.value(u"min"_s).toDouble()));
        else if (op == u"safety") { fans.setSafety(s.value(u"v"_s).toDouble()); settings.setSafety(s.value(u"v"_s).toDouble()); }
        else if (op == u"save") {
            const QString why = state.saveProfile(s.value(u"name"_s).toString());
            return why.isEmpty() ? u""_s : why == i18n.t(u"Fans not read yet (is NBFC running?), try again"_s) ? u"refused"_s : why;
        }
        else if (op == u"remove") {
            const QString why = state.removeProfile(s.value(u"name"_s).toString());
            return why.isEmpty() ? u""_s : u"notFound"_s;
        }
        else if (op == u"quit") quit();
        else return u"unknown op "_s + op;
        return QJsonValue::Null;
    };

    settle();
    QJsonArray steps{ snapshot(QJsonValue::Null) };
    for (const QJsonValue &s : sc.value(u"steps"_s).toArray()) {
        const QJsonValue ret = step(s.toObject());
        settle();
        steps.append(snapshot(ret));
    }
    return { { u"steps"_s, steps } };
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QFile in, outFile;
    if (!in.open(stdin, QIODevice::ReadOnly) || !outFile.open(stdout, QIODevice::WriteOnly)) return 1;
    while (!in.atEnd()) {
        const QByteArray line = in.readLine().trimmed();
        if (line.isEmpty()) continue;
        outFile.write(QJsonDocument(runOne(QJsonDocument::fromJson(line).object())).toJson(QJsonDocument::Compact) + '\n');
        outFile.flush();
    }
    return 0;
}
