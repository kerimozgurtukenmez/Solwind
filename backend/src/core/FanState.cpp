#include "FanState.h"

#include "FanController.h"
#include "I18n.h"
#include "JsCompat.h"
#include "Notifier.h"
#include "plugins/FanPlugin.h"

#include <QJsonValue>
#include <QLocale>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {
QString lower(const QString &s) { return QLocale().toLower(s); }     // toLocaleLowerCase()

FanSt curveSt(QList<CurvePoint> pts, int hyst)
{
    FanSt s;
    s.mode = 2;
    s.fixed = 0.5;
    s.curve = std::move(pts);
    s.smooth = true;
    s.src = u"max"_s;
    s.hyst = hyst;
    return s;
}
} // namespace

FanState::FanState(FanPlugin &plugin, FanController &control, Settings &settings, const I18n &i18n, Notifier &notifier,
                   QObject *parent)
    : QObject(parent), m_plugin(plugin), m_control(control), m_settings(settings), m_i18n(i18n), m_notifier(notifier),
      m_globalSt(autoSt())
{
}

// ---------- defaults (ST-2, ST-3) ----------

QList<CurvePoint> FanState::defaultCurve() { return { { 45, 20 }, { 55, 40 }, { 65, 60 }, { 75, 80 }, { 85, 100 } }; }

FanSt FanState::autoSt()
{
    FanSt s;
    s.mode = 0;
    s.fixed = 0.5;
    s.curve = defaultCurve();
    s.smooth = true;
    s.src = u"cpu"_s;
    s.hyst = 3;
    return s;
}

FanSt FanState::full(const FanSt &s)
{
    FanSt o = s;
    if (o.curve.size() < 2) o.curve = defaultCurve();
    return o;
}

// ---------- restore (F-1..F-4) ----------

void FanState::restore()
{
    const auto &fans = m_plugin.fans();
    if (fans.isEmpty()) return;
    if (m_restored && m_fanSt.size() == fans.size()) return;
    m_restored = true;
    m_applying = true;
    const QString id = m_plugin.configId();
    QList<FanSt> sts;
    QList<int> saved;
    for (int i = 0; i < fans.size(); ++i) {
        if (const auto s = m_settings.fanOf(id, i)) { sts << full(*s); saved << i; continue; }
        // No saved setting: the fan's current state, for display only - it gets no command
        FanSt st = autoSt();
        if (!fans[i].autoControl) { st.mode = 1; st.fixed = std::max(0.0, std::min(1.0, fans[i].target / 100)); }
        sts << st;
    }
    m_fanSt = sts;
    const auto g = m_settings.globalOf(id);
    if (g) m_globalSt = full(g->st);                      // (otherwise the previous one stays, as in the QML)
    m_globalOn = g && g->on;
    if (m_globalOn) m_control.applyTarget(-1, m_globalSt);
    else for (int i : saved) m_control.applyTarget(i, m_fanSt[i]);
    m_applying = false;
    emit changed();
}

// ---------- changes (F-5..F-8) ----------

void FanState::setFan(int i, const FanSt &st)
{
    if (i < 0 || i >= m_fanSt.size()) return;             // (clients are checked before; never out of range here)
    m_fanSt[i] = full(st);
    if (!m_globalOn) m_control.applyTarget(i, m_fanSt[i]);
    m_settings.setFan(m_plugin.configId(), i, m_fanSt[i]);
    manualEdit();
    emit changed();
}

void FanState::setGlobal(const FanSt &st)
{
    m_globalSt = full(st);
    if (m_globalOn) m_control.applyTarget(-1, m_globalSt);
    m_settings.setGlobal(m_plugin.configId(), m_globalOn, m_globalSt);
    manualEdit();
    emit changed();
}

void FanState::setGlobalOn(bool on)
{
    if (m_globalOn == on) return;
    m_globalOn = on;
    reapply();
    m_settings.setGlobal(m_plugin.configId(), m_globalOn, m_globalSt);
    manualEdit();
    emit changed();
}

// Global on: the global setting goes to every fan; off: each fan back to its own setting
void FanState::reapply()
{
    if (m_globalOn) m_control.applyTarget(-1, m_globalSt);
    else for (int i = 0; i < m_fanSt.size(); ++i) m_control.applyTarget(i, m_fanSt[i]);
}

void FanState::manualEdit()
{
    if (!m_applying) m_settings.setActive(m_plugin.configId(), QString());
}

// ---------- profiles ----------

QList<FanState::ProfileEntry> FanState::builtins() const
{
    auto entry = [](const QString &name, const QString &label, const QString &icon, std::optional<GlobalSetting> g) {
        ProfileEntry e;
        e.name = name;
        e.label = label;
        e.builtin = true;
        e.icon = icon;
        e.profile.name = name;
        e.profile.global = std::move(g);
        return e;
    };
    // The same curve on all fans via Global, following the hotter of CPU and GPU (PR-1)
    return {
        entry(u"auto"_s, u"NBFC Auto"_s, u"auto_mode"_s, std::nullopt),
        entry(u"quiet"_s, u"Quiet"_s, u"volume_off"_s,
              GlobalSetting{ true, curveSt({ { 45, 0 }, { 55, 25 }, { 65, 40 }, { 75, 60 }, { 85, 100 } }, 4) }),
        entry(u"balanced"_s, u"Balanced"_s, u"balance"_s,
              GlobalSetting{ true, curveSt({ { 45, 20 }, { 55, 35 }, { 65, 55 }, { 75, 75 }, { 85, 100 } }, 3) }),
        entry(u"performance"_s, u"Performance"_s, u"bolt"_s,
              GlobalSetting{ true, curveSt({ { 40, 40 }, { 50, 55 }, { 60, 70 }, { 70, 85 }, { 80, 100 } }, 2) }),
    };
}

QList<FanState::ProfileEntry> FanState::allProfiles() const
{
    QList<ProfileEntry> all = builtins();
    for (const Profile &p : m_settings.profilesOf(m_plugin.configId())) {
        ProfileEntry e;
        e.name = p.name;
        e.profile = p;
        all << e;
    }
    return all;
}

QString FanState::displayName(const ProfileEntry &p) const { return p.builtin ? m_i18n.t(p.label) : p.name; }

// Older versions saved the built-in profiles by their Turkish names (PR-3)
QString FanState::activeId() const
{
    static const QHash<QString, QString> legacy{ { u"NBFC Otomatik"_s, u"auto"_s }, { u"Sessiz"_s, u"quiet"_s },
                                                 { u"Dengeli"_s, u"balanced"_s }, { u"Performans"_s, u"performance"_s } };
    const QString a = m_settings.activeOf(m_plugin.configId());
    return legacy.value(a, a);
}

QString FanState::activeName() const
{
    const QString id = activeId();
    for (const ProfileEntry &p : allProfiles()) if (p.name == id) return displayName(p);
    return QString();
}

// Every name a profile answers to: id, English label and its translations (case-insensitive) - PR-4
QStringList FanState::namesOf(const ProfileEntry &p) const
{
    if (!p.builtin) return { lower(p.name) };
    QStringList n{ p.name, p.label };
    for (const QString &l : I18n::languages()) n << m_i18n.tIn(l, p.label);
    for (QString &x : n) x = lower(x);
    return n;
}

bool FanState::reservedName(const QString &name) const
{
    const QString k = lower(js::trim(name));
    for (const ProfileEntry &p : builtins()) if (namesOf(p).contains(k)) return true;
    return false;
}

QStringList FanState::reservedNames() const
{
    QStringList out;
    for (const ProfileEntry &p : builtins())
        for (const QString &n : namesOf(p)) if (!out.contains(n)) out << n;
    return out;
}

Profile FanState::snapshot(const QString &name) const
{
    Profile p;
    p.name = name;
    for (int i = 0; i < m_fanSt.size(); ++i) p.fans.insert(QString::number(i), m_fanSt[i]);
    p.global = GlobalSetting{ m_globalOn, m_globalSt };
    return p;
}

void FanState::applyProfile(const ProfileEntry &p)
{
    if (!m_restored) return;
    m_applying = true;
    const QString id = m_plugin.configId();
    QList<FanSt> a;
    for (int i = 0; i < m_plugin.fans().size(); ++i) {
        FanSt s = autoSt();                                // a fan the profile has no (valid) setting for: auto
        const auto it = p.profile.fans.constFind(QString::number(i));
        if (it != p.profile.fans.cend()) s = *it;
        a << full(s);
        m_settings.setFan(id, i, a[i]);
    }
    m_fanSt = a;
    if (p.profile.global) m_globalSt = full(p.profile.global->st);
    m_globalOn = p.profile.global && p.profile.global->on;
    reapply();
    m_settings.setGlobal(id, m_globalOn, m_globalSt);
    m_settings.setActive(id, p.name);
    m_applying = false;
    emit changed();
}

FanState::Result FanState::applyByName(const QString &name, bool fromClient)
{
    const QString key = lower(js::trim(name));
    const QList<ProfileEntry> all = allProfiles();
    // The exact name of a user profile first (older settings may hold two names that only differ in case), then any
    // case. Not for a name a built-in answers to: older versions let a user profile be called "quiet" too (PR-9)
    const ProfileEntry *p = nullptr;
    if (!reservedName(name))
        for (const ProfileEntry &x : all) if (!x.builtin && x.name == js::trim(name)) { p = &x; break; }
    if (!p) for (const ProfileEntry &x : all) if (namesOf(x).contains(key)) { p = &x; break; }
    if (!p) return { false, true, m_i18n.t(u"Profile not found: %1"_s).arg(name) + u'\n' + profileList() };
    if (!m_restored) return { false, false, m_i18n.t(u"Fans not read yet (is NBFC running?), try again"_s) };
    if (const QString why = m_plugin.blockedReason(false); !why.isEmpty()) return { false, false, why };
    applyProfile(*p);
    if (fromClient && m_settings.notifyOn(u"profile"_s))
        m_notifier.send(m_i18n.t(u"Fan profile: %1"_s).arg(displayName(*p)), QString(), Notifier::Low, u"dialog-information"_s);
    return { true, false, m_i18n.t(u"Applied: %1"_s).arg(displayName(*p)) };
}

QString FanState::profileList() const
{
    const QString active = activeId();
    QStringList lines;
    for (const ProfileEntry &p : allProfiles())
        lines << (p.name == active ? u"* "_s : u"  "_s) + displayName(p) + (p.builtin ? u"  ("_s + p.name + u")"_s : QString());
    return lines.join(u'\n');
}

// The fan lines are formatted in one pass: FanState.qml's chained .arg() made the Turkish "%%5" a placeholder again
// when a value before it was 0 or 1, and the speeds came out wrong (SPEC.md §21e, a deliberate fix)
QString FanState::statusText(double gpu) const
{
    const QStringList modes{ m_i18n.t(u"Auto"_s), m_i18n.t(u"Fixed"_s), m_i18n.t(u"Curve"_s) };
    const auto mode = [&](int m) { return m >= 0 && m < modes.size() ? modes[m] : u"undefined"_s; };   // (modes[m])
    const QString active = activeName();
    const double t = m_plugin.temp();
    QStringList lines{
        m_i18n.t(u"Service: %1"_s).arg(m_plugin.running() ? m_i18n.t(u"running"_s) : m_i18n.t(u"stopped"_s))
            + (m_plugin.readOnly() ? u" ("_s + m_i18n.t(u"read-only"_s) + u")"_s : QString()),
        u"Config: "_s + (m_plugin.configId().isEmpty() ? u"-"_s : m_plugin.configId()),
        m_i18n.t(u"Profile: %1"_s).arg(active.isEmpty() ? m_i18n.t(u"custom settings"_s) : active),
        // (Number.toFixed)
        u"CPU: "_s + (std::isfinite(t) ? QString::number(t, 'f', 1) + u"°C"_s : u"-"_s)
            + u"   GPU: "_s + (std::isfinite(gpu) ? QString::number(gpu, 'f', 0) + u"°C"_s : u"-"_s),
        u"Global: "_s + (m_globalOn ? m_i18n.t(u"on (%1)"_s).arg(mode(m_globalSt.mode)) : m_i18n.t(u"off"_s))
            + (m_control.boosting() ? u"   "_s + m_i18n.t(u"Max fan: on"_s) : QString()),
    };
    const QList<FanPlugin::Fan> &fans = m_plugin.fans();
    for (qsizetype i = 0; i < fans.size(); ++i) {
        const FanPlugin::Fan &f = fans[i];
        const int m = m_globalOn ? m_globalSt.mode : (i < m_fanSt.size() ? m_fanSt[i].mode : 0);
        lines << m_i18n.t(u"Fan %1 (%2): %3 · NBFC %4 · current %5% · target %6%"_s)
                     .arg(QString::number(i + 1), f.name, mode(m),
                          f.autoControl ? m_i18n.t(u"auto"_s) : m_i18n.t(u"manual"_s),
                          js::toString(QJsonValue(js::round(f.current))), js::toString(QJsonValue(js::round(f.target))));
    }
    return lines.join(u'\n');
}

QString FanState::saveProfile(const QString &raw)
{
    const QString name = js::trim(raw);
    if (name.isEmpty()) return m_i18n.t(u"A profile needs a name"_s);
    if (name.size() > 32) return m_i18n.t(u"A profile name can have at most 32 characters"_s);   // (UTF-16 units, as cleanName)
    if (reservedName(name)) return m_i18n.t(u"This name belongs to a built-in profile, pick another one."_s);
    if (!m_restored) return m_i18n.t(u"Fans not read yet (is NBFC running?), try again"_s);
    m_settings.saveProfile(m_plugin.configId(), snapshot(name));
    m_settings.setActive(m_plugin.configId(), name);
    emit changed();
    return QString();
}

QString FanState::removeProfile(const QString &name)
{
    bool found = false;
    for (const Profile &p : m_settings.profilesOf(m_plugin.configId())) if (p.name == name) found = true;
    if (!found) return m_i18n.t(u"Profile not found: %1"_s).arg(name);
    m_settings.removeProfile(m_plugin.configId(), name);
    emit changed();
    return QString();
}
