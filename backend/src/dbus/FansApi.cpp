#include "FansApi.h"

#include "core/FanController.h"
#include "core/FanState.h"
#include "core/History.h"
#include "core/I18n.h"
#include "core/Sensors.h"
#include "plugins/fan/nbfc/NbfcPlugin.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {
const QString errUsage = u"solwind.Error.Usage"_s;
const QString errFailed = u"solwind.Error.Failed"_s;
const QString errObserve = u"solwind.Error.Observe"_s;

// A number from a client (any D-Bus integer or double type; not a bool, not a string)
std::optional<double> number(const QVariant &v)
{
    switch (v.metaType().id()) {
    case QMetaType::Int: case QMetaType::UInt: case QMetaType::LongLong: case QMetaType::ULongLong:
    case QMetaType::Short: case QMetaType::UShort: case QMetaType::UChar: case QMetaType::Double:
        return v.toDouble();
    default: return std::nullopt;
    }
}
std::optional<int> wholeNumber(const QVariant &v, int lo, int hi)
{
    const auto n = number(v);
    if (!n || !std::isfinite(*n) || *n != std::floor(*n) || *n < lo || *n > hi) return std::nullopt;
    return int(*n);
}

// The curve's points as maps, from aa{sv}, av of a{sv}, or (in-process) a list of maps
std::optional<QList<QVariantMap>> pointMaps(const QVariant &v)
{
    if (v.canConvert<QDBusArgument>() && v.metaType() == QMetaType::fromType<QDBusArgument>()) {
        const QDBusArgument a = v.value<QDBusArgument>();
        if (a.currentSignature() == u"aa{sv}"_s) return qdbus_cast<QList<QVariantMap>>(a);
        if (a.currentSignature() == u"av"_s) {
            QList<QVariantMap> out;
            for (const QVariant &x : qdbus_cast<QVariantList>(a)) {
                if (x.metaType() == QMetaType::fromType<QDBusArgument>() && x.value<QDBusArgument>().currentSignature() == u"a{sv}"_s)
                    out << qdbus_cast<QVariantMap>(x.value<QDBusArgument>());
                else if (x.metaType().id() == QMetaType::QVariantMap) out << x.toMap();
                else return std::nullopt;
            }
            return out;
        }
        return std::nullopt;
    }
    if (v.metaType() == QMetaType::fromType<VariantMapList>()) return v.value<VariantMapList>();
    if (v.metaType().id() == QMetaType::QVariantList) {
        QList<QVariantMap> out;
        for (const QVariant &x : v.toList()) {
            if (x.metaType().id() != QMetaType::QVariantMap) return std::nullopt;
            out << x.toMap();
        }
        return out;
    }
    return std::nullopt;
}
} // namespace

FansApi::FansApi(NbfcPlugin &nbfc, const Sensors &sensors, const SampleHistory &history, FanController &control,
                 FanState &state, const I18n &i18n, Settings &settings, bool controlMode, QObject *parent)
    : QObject(parent), m_nbfc(nbfc), m_sensors(sensors), m_history(history), m_control(control), m_state(state),
      m_i18n(i18n), m_settings(settings), m_controlMode(controlMode)
{
}

// ---------- reading ----------

bool FansApi::running() const { return m_nbfc.running(); }
bool FansApi::readOnly() const { return m_nbfc.readOnly(); }
double FansApi::temp() const { return m_nbfc.temp(); }

VariantMapList FansApi::fans() const
{
    VariantMapList out;
    const auto &fans = m_nbfc.fans();
    for (int i = 0; i < fans.size(); ++i) {
        const FanPlugin::Fan &f = fans[i];
        out.append({ { u"name"_s, f.name }, { u"temp"_s, f.temp }, { u"auto"_s, f.autoControl },
                     { u"critical"_s, f.critical }, { u"current"_s, f.current }, { u"target"_s, f.target },
                     { u"rpm"_s, m_sensors.state().rpm(i, int(fans.size())) } });
    }
    return out;
}

QString FansApi::configId() const { return m_nbfc.configId(); }
QString FansApi::fileConfigId() const { return m_nbfc.monitor().fileConfigId(); }
bool FansApi::noConfig() const { return m_nbfc.monitor().noConfig(); }
bool FansApi::configPending() const { return m_nbfc.monitor().configPending(); }
bool FansApi::bootEnabled() const { return m_nbfc.monitor().bootEnabled(); }
QStringList FansApi::configs() const { return m_nbfc.monitor().configs(); }

QVariantMap FansApi::configList() const
{
    const NbfcMonitor &m = m_nbfc.monitor();
    return { { u"dirs"_s, m.configDirsUsed() }, { u"fromDirs"_s, int(m.configsFromDirs().size()) },
             { u"fromCli"_s, int(m.configsFromCli().size()) }, { u"cliCode"_s, m.cliListCode() },
             { u"cliLine"_s, m.cliListLine() } };
}

VariantMapList FansApi::configCurves() const
{
    VariantMapList out;
    for (const auto &c : m_nbfc.monitor().configCurves()) {
        if (!c) { out.append(QVariantMap()); continue; }
        VariantMapList pts;
        for (const Nbfc::CurvePoint &p : c->points) pts.append({ { u"t"_s, p.t }, { u"s"_s, p.s } });
        out.append({ { u"points"_s, QVariant::fromValue(pts) }, { u"hyst"_s, c->hyst } });
    }
    return out;
}

QStringList FansApi::recommended() const { return m_nbfc.monitor().recommended(); }

QVariantMap FansApi::stMap(const FanSt &st)
{
    VariantMapList pts;
    for (const CurvePoint &p : st.curve) pts.append({ { u"t"_s, p.t }, { u"s"_s, p.s } });
    return { { u"mode"_s, st.mode }, { u"fixed"_s, st.fixed }, { u"curve"_s, QVariant::fromValue(pts) },
             { u"smooth"_s, st.smooth }, { u"src"_s, st.src }, { u"hyst"_s, st.hyst } };
}

VariantMapList FansApi::fanSettings() const
{
    VariantMapList out;
    for (const FanSt &st : m_state.fanSt()) out.append(stMap(st));
    return out;
}

QVariantMap FansApi::global() const { return stMap(m_state.globalSt()); }
bool FansApi::globalOn() const { return m_state.globalOn(); }
bool FansApi::restored() const { return m_state.restored(); }

VariantMapList FansApi::profiles() const
{
    VariantMapList out;
    for (const FanState::ProfileEntry &p : m_state.allProfiles())
        out.append({ { u"name"_s, p.name }, { u"label"_s, m_state.displayName(p) }, { u"builtin"_s, p.builtin },
                     { u"icon"_s, p.builtin ? p.icon : u"tune"_s } });
    return out;
}

QString FansApi::activeProfile() const { return m_state.activeId(); }
QStringList FansApi::reservedNames() const { return m_state.reservedNames(); }
qlonglong FansApi::boostUntil() const { return m_control.boostUntil(); }
QString FansApi::boostBlocked() const { return m_controlMode ? m_control.boostBlocked() : m_i18n.t(u"Solwind only watches the fans now"_s); }
double FansApi::safety() const { return m_control.safety(); }

QList<int> FansApi::safetyHeld() const
{
    QList<int> out;
    for (int i = 0; i < m_nbfc.fans().size(); ++i) if (m_control.hot(i)) out << i;
    return out;
}

bool FansApi::controlling() const { return m_control.controlling(); }
bool FansApi::holding() const { return m_nbfc.holding(); }
bool FansApi::adminBusy() const { return m_nbfc.adminBusy(); }
QString FansApi::message() const { return m_nbfc.message(); }
bool FansApi::messageError() const { return m_nbfc.messageError(); }

QVariantMap FansApi::sampleMap(qint64 t, double cpu, double gpu, double fan)
{
    return { { u"t"_s, t }, { u"cpu"_s, cpu }, { u"gpu"_s, gpu }, { u"fan"_s, fan } };
}

// ---------- checking what clients send ----------

std::optional<FanSt> FansApi::stFromClient(const QVariantMap &m, QString *why)
{
    auto fail = [&](const QString &w) { if (why) *why = w; return std::nullopt; };
    FanSt st = FanState::autoSt();
    for (auto it = m.begin(); it != m.end(); ++it) {
        const QString &k = it.key();
        const QVariant &v = it.value();
        if (k == u"mode"_s) {
            const auto n = wholeNumber(v, 0, 2);
            if (!n) return fail(u"mode must be 0 (auto), 1 (fixed) or 2 (curve)"_s);
            st.mode = *n;
        } else if (k == u"fixed"_s) {
            const auto n = number(v);
            if (!n || !std::isfinite(*n) || *n < 0 || *n > 1) return fail(u"fixed must be a number from 0 to 1"_s);
            st.fixed = *n;
        } else if (k == u"curve"_s) {
            const auto pts = pointMaps(v);
            if (!pts) return fail(u"curve must be a list of {t, s}"_s);
            if (pts->size() < 2 || pts->size() > 12) return fail(u"a curve has 2 to 12 points"_s);
            QList<CurvePoint> curve;
            for (const QVariantMap &p : *pts) {
                if (p.size() != 2 || !p.contains(u"t"_s) || !p.contains(u"s"_s)) return fail(u"a curve point is {t, s}"_s);
                const auto t = wholeNumber(p.value(u"t"_s), 30, 100), s = wholeNumber(p.value(u"s"_s), 0, 100);
                if (!t) return fail(u"a curve point's t is a whole °C from 30 to 100"_s);
                if (!s) return fail(u"a curve point's s is a whole % from 0 to 100"_s);
                if (!curve.isEmpty() && *t <= curve.last().t) return fail(u"curve points rise in temperature, at least 1 °C apart"_s);
                curve.append({ *t, *s });
            }
            st.curve = curve;
        } else if (k == u"smooth"_s) {
            if (v.metaType().id() != QMetaType::Bool) return fail(u"smooth must be true or false"_s);
            st.smooth = v.toBool();
        } else if (k == u"src"_s) {
            const QString s = v.metaType().id() == QMetaType::QString ? v.toString() : QString();
            if (s != u"cpu"_s && s != u"gpu"_s && s != u"max"_s) return fail(u"src must be cpu, gpu or max"_s);
            st.src = s;
        } else if (k == u"hyst"_s) {
            const auto n = wholeNumber(v, 0, 10);
            if (!n) return fail(u"hyst must be a whole °C from 0 to 10"_s);
            st.hyst = *n;
        } else {
            return fail(u"unknown field: "_s + k);
        }
    }
    return st;
}

// Changes are refused in observe mode, and once quitting has begun: nothing would be written any more, and a client
// must not be told it's done (deliberate: the QML app had no such moment, it was gone)
bool FansApi::changeRefused()
{
    if (!m_controlMode) {
        if (calledFromDBus()) sendErrorReply(errObserve, m_i18n.t(u"Solwind only watches the fans now"_s));
        return true;
    }
    if (m_control.quitting()) {
        if (calledFromDBus()) sendErrorReply(errFailed, m_i18n.t(u"Solwind is quitting"_s));
        return true;
    }
    return false;
}
void FansApi::usage(const QString &msg) { if (calledFromDBus()) sendErrorReply(errUsage, msg); }
void FansApi::failed(const QString &msg) { if (calledFromDBus()) sendErrorReply(errFailed, msg); }

// ---------- methods ----------

bool FansApi::Recommend(QStringList &)
{
    // Answered when nbfc config -r has finished. It waits in the command queue behind the other nbfc commands (each
    // limited to 15 s), so a client should allow more than D-Bus's default 25 s.
    if (!calledFromDBus()) return false;
    setDelayedReply(true);
    const QDBusMessage msg = QDBusContext::message();
    QDBusConnection conn = connection();
    m_nbfc.recommend([msg, conn](bool ok, const QStringList &found) mutable {
        conn.send(msg.createReply(QVariantList{ ok, found }));
    });
    return false;
}

VariantMapList FansApi::History() const
{
    VariantMapList out;
    for (const auto &s : m_history.samples()) out.append(sampleMap(s.t, s.cpu, s.gpu, s.fan));
    return out;
}

QString FansApi::StatusText() const { return m_state.statusText(m_sensors.state().gpu()); }
QString FansApi::ProfileList() const { return m_state.profileList(); }

void FansApi::SetFan(int fan, const QVariantMap &raw)
{
    if (changeRefused()) return;
    QString why;
    const auto st = stFromClient(raw, &why);
    if (!st) return usage(why);
    if (!m_state.restored()) return failed(m_i18n.t(u"Fans not read yet (is NBFC running?), try again"_s));
    if (fan < 0 || fan >= m_state.fanSt().size()) return usage(u"no fan %1"_s.arg(fan));
    if (const QString b = m_nbfc.blockedReason(false); !b.isEmpty()) return failed(b);
    m_state.setFan(fan, *st);
}

void FansApi::SetGlobal(const QVariantMap &raw)
{
    if (changeRefused()) return;
    QString why;
    const auto st = stFromClient(raw, &why);
    if (!st) return usage(why);
    if (!m_state.restored()) return failed(m_i18n.t(u"Fans not read yet (is NBFC running?), try again"_s));
    if (const QString b = m_nbfc.blockedReason(false); !b.isEmpty()) return failed(b);
    m_state.setGlobal(*st);
}

void FansApi::SetGlobalOn(bool on)
{
    if (changeRefused()) return;
    if (!m_state.restored()) return failed(m_i18n.t(u"Fans not read yet (is NBFC running?), try again"_s));
    if (const QString b = m_nbfc.blockedReason(false); !b.isEmpty()) return failed(b);
    m_state.setGlobalOn(on);
}

QString FansApi::ApplyProfile(const QString &name, bool notify)
{
    if (changeRefused()) return QString();
    const FanState::Result r = m_state.applyByName(name, notify);
    if (r.usage) { usage(r.text); return QString(); }
    if (!r.ok) { failed(r.text); return QString(); }
    return r.text;
}

void FansApi::SaveProfile(const QString &name)
{
    if (changeRefused()) return;
    if (!m_state.restored()) return failed(m_i18n.t(u"Fans not read yet (is NBFC running?), try again"_s));
    if (const QString why = m_state.saveProfile(name); !why.isEmpty()) usage(why);
}

void FansApi::RemoveProfile(const QString &name)
{
    if (changeRefused()) return;
    if (const QString why = m_state.removeProfile(name); !why.isEmpty()) usage(why);
}

void FansApi::SetBoost(qlonglong minutes)
{
    if (changeRefused()) return;
    // (at most a year: minutes * 60000 stays far inside a 64-bit time)
    if (minutes > 525600 || minutes < -525600) return usage(u"minutes: -525600..525600"_s);
    if (!m_control.setBoost(minutes)) failed(m_control.boostBlocked());
}

void FansApi::SetSafety(int celsius)
{
    if (changeRefused()) return;
    if (celsius < 70 || celsius > 100) return usage(u"the safety limit is 70..100 °C"_s);
    m_control.setSafety(celsius);                          // used from the next status read on, as in the QML
    m_settings.setSafety(celsius);
}

void FansApi::StartService() { if (!changeRefused()) m_nbfc.startService(); }
void FansApi::StopService() { if (!changeRefused()) m_nbfc.stopService(); }
void FansApi::RestartService() { if (!changeRefused()) m_nbfc.restartService(m_nbfc.readOnly()); }
void FansApi::SetReadOnly(bool on) { if (!changeRefused()) m_nbfc.setReadOnly(on); }
void FansApi::SetBoot(bool on) { if (!changeRefused()) m_nbfc.setBoot(on); }

void FansApi::ApplyConfig(const QString &name)
{
    if (changeRefused()) return;
    if (name.isEmpty()) return usage(u"no config name"_s);
    if (name == m_nbfc.configId()) return;                 // already the one (UI-6)
    // Only one of the listed configs, as the QML UI offered (deliberate): `nbfc config -s` also takes a file path,
    // and a config decides what NBFC writes to the embedded controller
    if (!configs().contains(name)) return usage(u"not a known config: %1"_s.arg(name));
    m_nbfc.applyConfig(name);
}
