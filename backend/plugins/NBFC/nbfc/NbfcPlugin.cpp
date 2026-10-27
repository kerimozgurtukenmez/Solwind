#include "NbfcPlugin.h"

#include "core/JsCompat.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>

using namespace Qt::StringLiterals;

NbfcPlugin::NbfcPlugin(const PluginHost &host, QObject *parent)
    : FanPlugin(parent), m_paths(host.paths()), m_host(host), m_mon(host.paths())
{
    now = [] { return QDateTime::currentMSecsSinceEpoch(); };
    m_mon.lostHeld = [this] { return m_quitting || quiet(); };
    connect(&m_mon, &NbfcMonitor::statusRead, this, [this](bool ok) { syncFans(); emit statusRead(ok); });
    connect(&m_mon, &NbfcMonitor::changed, this, [this] { syncFans(); emit changed(); });
    connect(&m_mon, &NbfcMonitor::configIdChanged, this, &FanPlugin::configIdChanged);
    connect(&m_mon, &NbfcMonitor::serviceLost, this, &FanPlugin::serviceLost);
}

void NbfcPlugin::syncFans()
{
    QList<Fan> fans;
    for (const Nbfc::FanStatus &f : m_mon.fans())
        fans.append({ f.name, f.temp, f.autoControl, f.critical, f.current, f.target });
    m_fans = fans;
}

// While an admin action runs and for a few seconds after it, NBFC may stop and start: that is not reported as
// "stopped unexpectedly" or "could not set the speed" (A-2). A clock jump back doesn't stretch it.
bool NbfcPlugin::quiet() const
{
    const qint64 left = m_adminQuietUntil - now();
    return adminBusy() || (left > 0 && left <= 5000);
}

QString NbfcPlugin::blockedReason(bool withHolding) const
{
    if (!running()) return m_host.t(u"The NBFC service is not running"_s);
    if (readOnly()) return m_host.t(u"NBFC is in read-only mode, fan speeds can't be written"_s);
    if (withHolding && holding()) return m_host.t(u"Waiting for the NBFC service action to finish"_s);
    return QString();
}

// ---------- speed commands (Q-1, Q-2; keys as in Nbfc.qml) ----------

void NbfcPlugin::setPercent(int fan, int pct, Done done)
{
    m_mon.queue().enqueue(u"spd:%1"_s.arg(fan), { u"nbfc"_s, u"set"_s, u"-f"_s, QString::number(fan), u"-s"_s, QString::number(pct) },
                          [done](int code, const QByteArray &out, const QByteArray &err) {
        if (done) done(code == 0, code == 0 ? QString() : Nbfc::firstLine(QString::fromUtf8(err.isEmpty() ? out : err)));
    });
}

void NbfcPlugin::setAuto(int fan, Done done)
{
    const QStringList cmd = fan < 0 ? QStringList{ u"nbfc"_s, u"set"_s, u"-a"_s }
                                    : QStringList{ u"nbfc"_s, u"set"_s, u"-f"_s, QString::number(fan), u"-a"_s };
    m_mon.queue().enqueue(fan < 0 ? u"auto:all"_s : u"auto:%1"_s.arg(fan), cmd,
                          [done](int code, const QByteArray &out, const QByteArray &err) {
        if (done) done(code == 0, code == 0 ? QString() : Nbfc::firstLine(QString::fromUtf8(err.isEmpty() ? out : err)));
    });
}

void NbfcPlugin::releaseAll(Done done)
{
    // Key "": never replaced by a later command, runs after the speed commands already waiting
    m_mon.queue().enqueue(QString(), { u"nbfc"_s, u"set"_s, u"-a"_s }, [done](int code, const QByteArray &out, const QByteArray &err) {
        if (done) done(code == 0, code == 0 ? QString() : Nbfc::firstLine(QString::fromUtf8(err.isEmpty() ? out : err)));
    });
}

void NbfcPlugin::willRestoreFixedSpeeds(std::function<void(bool)> done)
{
    // NBFC saved its targets when it stopped and sets them again at its next start (/var/lib/nbfc/state.json):
    // (JSON.parse(out).TargetFanSpeeds || []).some(v => Number(v) >= 0); unreadable -> false (QT-3)
    QFile f(m_paths.nbfc(u"/var/lib/nbfc/state.json"_s));
    bool fixed = false;
    if (f.open(QIODevice::ReadOnly)) {
        const auto v = js::parseJson(f.read(1 << 20));
        if (v && !v->isNull()) {                          // null.TargetFanSpeeds threw: false
            const QJsonValue t = v->isObject() ? v->toObject().value(u"TargetFanSpeeds"_s) : QJsonValue();
            if (t.isArray())
                for (const QJsonValue &x : t.toArray()) if (js::number(x) >= 0) { fixed = true; break; }
        }
    }
    QTimer::singleShot(0, this, [done, fixed] { done(fixed); });
}

// ---------- admin actions (A-1..A-9) ----------

void NbfcPlugin::setMessage(const QString &text, bool error)
{
    if (m_message == text && m_messageError == error) return;
    m_message = text;
    m_messageError = error;
    emit changed();
}

void NbfcPlugin::pumpAdmin()
{
    if (m_admin.busy() || m_adminQueue.isEmpty()) return;
    AdminJob j = m_adminQueue.takeFirst();
    m_admin.go(j.cmd, [this, done = std::move(j.done)](int code, const QByteArray &out, const QByteArray &err) {
        if (done) done(code, out, err);
        QTimer::singleShot(0, this, &NbfcPlugin::pumpAdmin);
    });
}

void NbfcPlugin::sudo(const QStringList &args, const QString &label, std::function<void(int)> then)
{
    setMessage(label, false);
    m_adminQueue.append({ QStringList{ u"pkexec"_s } + args, [this, then](int code, const QByteArray &out, const QByteArray &err) {
        m_adminQuietUntil = now() + 5000;
        if (code == 0) setMessage(QString(), false);
        else setMessage(m_host.t(u"Failed: %1"_s).arg(Nbfc::firstLine(QString::fromUtf8(err.isEmpty() ? out : err)) + u" ("_s
                                                        + m_host.t(u"code %1"_s).arg(code) + u")"_s), true);
        if (then) then(code);
        m_mon.pollNow();
        emit changed();
    } });
    emit changed();                                        // (busy)
    pumpAdmin();
}

void NbfcPlugin::handBackThen(const QStringList &args, std::function<void(int)> then)
{
    ++m_holds;
    emit changed();
    auto act = [this, args, then] {
        sudo(args, m_host.t(u"Waiting for admin permission…"_s), [this, then](int code) {
            --m_holds;
            if (then) then(code);
            emit changed();
        });
    };
    // Always, also when the last read shows them on auto: a speed may still be on its way, and in read-only mode NBFC
    // keeps and saves the targets too. Key "" runs after the speed commands already waiting.
    if (running()) m_mon.queue().enqueue(QString(), { u"nbfc"_s, u"set"_s, u"-a"_s }, [act](int, const QByteArray &, const QByteArray &) { act(); });
    else act();
}

void NbfcPlugin::startService()
{
    m_mon.clearLost();
    QStringList args{ u"nbfc"_s, u"start"_s };
    if (readOnly()) args << u"-r"_s;
    sudo(args, m_host.t(u"Waiting for admin permission…"_s));
}

void NbfcPlugin::stopService()
{
    handBackThen({ u"nbfc"_s, u"stop"_s }, [this](int code) { if (code == 0) m_mon.setUserStopped(); });
}

void NbfcPlugin::restartService(bool ro)
{
    QStringList args{ u"nbfc"_s, u"restart"_s };
    if (ro) args << u"-r"_s;
    sudo(args, m_host.t(u"Waiting for admin permission…"_s));
}

void NbfcPlugin::setReadOnly(bool on)
{
    const bool was = running();
    m_mon.setReadOnlyWanted(on);
    if (was) restartService(on);
}

void NbfcPlugin::setBoot(bool on)
{
    sudo({ u"systemctl"_s, on ? u"enable"_s : u"disable"_s, u"nbfc_service"_s }, m_host.t(u"Waiting for admin permission…"_s),
         [this](int) { m_mon.refreshBoot(); });
}

void NbfcPlugin::applyConfig(const QString &name)
{
    // The name goes in as a positional parameter of the shell, never into the script text
    handBackThen({ u"sh"_s, u"-c"_s, u"nbfc config -s \"$1\" && nbfc restart $2"_s, u"sh"_s, name,
                   readOnly() ? u"-r"_s : QString() });
}

void NbfcPlugin::recommend(std::function<void(bool, const QStringList &)> done)
{
    m_mon.recommend([this, done](bool ok, const QStringList &found) {
        if (ok && found.isEmpty()) setMessage(m_host.t(u"No recommendation found"_s), false);
        if (done) done(ok, found);
    });
}

QVariantMap NbfcPlugin::configList() const
{
    return { { u"dirs"_s, m_mon.configDirsUsed() }, { u"fromDirs"_s, int(m_mon.configsFromDirs().size()) },
             { u"fromCli"_s, int(m_mon.configsFromCli().size()) }, { u"cliCode"_s, m_mon.cliListCode() },
             { u"cliLine"_s, m_mon.cliListLine() } };
}

QList<std::optional<FanService::Curve>> NbfcPlugin::configCurves() const
{
    QList<std::optional<Curve>> out;
    for (const auto &c : m_mon.configCurves()) {
        if (!c) { out << std::nullopt; continue; }
        Curve x;
        x.hyst = c->hyst;
        for (const Nbfc::CurvePoint &p : c->points) x.points << CurvePoint{ p.t, p.s };
        out << x;
    }
    return out;
}
