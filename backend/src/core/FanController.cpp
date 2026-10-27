#include "FanController.h"

#include "I18n.h"
#include "JsCompat.h"

#include <QDateTime>
#include <QTimer>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {
constexpr double NaN = std::numeric_limits<double>::quiet_NaN();
constexpr double Inf = std::numeric_limits<double>::infinity();
}

FanController::FanController(FanPlugin &plugin, const I18n &i18n, QObject *parent)
    : QObject(parent), m_plugin(plugin), m_i18n(i18n)
{
    now = [] { return QDateTime::currentMSecsSinceEpoch(); };
    gpuTemp = [] { return NaN; };
}

// Writing only through a plugin trusted to write (ARCH-PLAN rule 4); a ReadOnly / Untested plugin gets no command
bool FanController::canWrite() const { return m_plugin.trust() == FanPlugin::Trust::WriteTested; }

// ---------- temperatures (C-6) ----------

double FanController::tempOf(int i) const
{
    const auto &f = m_plugin.fans();
    const double t = i >= 0 && i < f.size() ? f[i].temp : NaN;
    return std::isfinite(t) ? t : m_plugin.temp();
}

double FanController::sourceTemp(int i, const QString &src) const
{
    const double c = tempOf(i), g = gpuTemp();
    if (src == u"gpu"_s) return std::isfinite(g) ? g : c;
    if (src == u"max"_s) return std::isfinite(g) ? (std::isfinite(c) ? js::max(c, g) : g) : c;
    return c;
}

// ---------- curves (C-9) ----------

double FanController::curveAt(const FanSt &st, double t)
{
    const auto &p = st.curve;
    if (p.isEmpty()) return NaN;
    if (t <= p[0].t) return p[0].s;
    for (qsizetype i = 1; i < p.size(); ++i) {
        if (t < p[i].t) {
            if (!st.smooth) return p[i - 1].s;
            return p[i - 1].s + double(p[i].s - p[i - 1].s) * (t - p[i - 1].t) / (p[i].t - p[i - 1].t);
        }
    }
    return p.last().s;
}

double FanController::curveLevel(int i, const FanSt &st, double t) const
{
    const double up = curveAt(st, t);
    const auto prevIt = m_level.constFind(i);
    if (prevIt == m_level.cend() || !std::isfinite(*prevIt) || up >= *prevIt) return up;
    const double prev = *prevIt;
    const double down = curveAt(st, t + st.hyst);
    const double target = js::min(prev, js::max(up, down));
    return js::max(target, prev - rampDown);
}

// ---------- targets (C-2) ----------

void FanController::applyTarget(int idx, const FanSt &st)
{
    QList<int> ids;
    if (idx < 0) for (int i = 0; i < m_plugin.fans().size(); ++i) ids << i;
    else ids << idx;
    for (int i : ids) {
        m_lastSent.remove(i);
        m_sentSeq.insert(i, 0);
        m_level.remove(i);                                // the new setting applies immediately
        if (st.mode == 0) { m_targets.remove(i); markReleasing(i); }
        else { m_targets.insert(i, st); m_releasing.remove(i); }
    }
    emit changed();
    if (boosting()) return;                               // while max fan runs, each fan gets its own setting at the end
    if (st.mode == 0) sendAuto(idx);
    else control();
}

// A fan handed back to automatic: confirmed by a read started after it was asked, otherwise asked again (every 4 s,
// at most 5 times, then reported) - C-12
void FanController::markReleasing(int i) { m_releasing.insert(i, { now(), m_plugin.pollSeq(), 0 }); }

void FanController::checkRelease(int i, qint64 t)
{
    auto r = m_releasing.find(i);
    if (r == m_releasing.end()) return;
    if (m_plugin.fans()[i].autoControl && m_plugin.statusSeq() > r->seq) { m_releasing.erase(r); return; }
    const qint64 age = t - r->at;
    if (age >= 0 && age < 4000) return;                   // (a clock jump back counts as time up)
    if (r->tries >= 5) {
        if (m_plugin.quiet()) return;                     // an admin action may explain it: wait until it's over
        m_releasing.erase(r);
        emit releaseFailed(m_i18n.t(u"Fan %1 could not be handed back to NBFC's automatic control"_s).arg(i + 1));
        return;
    }
    r->at = t;
    r->seq = m_plugin.pollSeq();
    r->tries++;
    sendAuto(i);
}

// ---------- the loop (C-1, C-3..C-11) ----------

void FanController::control()
{
    if (!m_plugin.running() || m_plugin.readOnly() || m_quitting || m_plugin.holding() || !canWrite()) return;
    const qint64 t = now();
    const auto &fans = m_plugin.fans();
    bool hotChanged = false;
    for (int i = 0; i < fans.size(); ++i) {
        const FanPlugin::Fan &f = fans[i];
        // The driver went back to auto or holds another speed than the one sent (changed outside Solwind, an old one
        // restored): write it again. Judged only from a read started after the driver took the command; not in
        // critical mode, where the driver runs the fan at 100% by itself.
        const auto last = m_lastSent.constFind(i);
        if (last != m_lastSent.cend() && double(m_plugin.statusSeq()) > m_sentSeq.value(i, 0)
            && (f.autoControl || (!f.critical && std::fabs(f.target - *last) >= 1)))
            m_lastSent.remove(i);
        if (boosting()) {                                 // max fan: every fan at 100%
            if (m_lastSent.value(i, -1) != 100) { m_lastSent.insert(i, 100); send(i, 100); }
            continue;
        }
        const auto target = m_targets.constFind(i);
        if (target == m_targets.cend()) { checkRelease(i, t); continue; }
        const FanSt &st = *target;
        const double T = sourceTemp(i, st.src);
        const double H = hottest(i);
        double pct;
        // Safety first. It lets go only 3 °C below the limit, so a fan doesn't flip between 100% and its setting every
        // round while the temperature hovers at the limit; an unreadable temperature keeps it (C-7)
        const bool wasHot = m_hot.value(i, false);
        if (std::isfinite(H) && H >= m_safety) m_hot.insert(i, true);
        else if (std::isfinite(H) && H < m_safety - safetyHyst) m_hot.insert(i, false);
        if (m_hot.value(i, false) != wasHot) hotChanged = true;
        if (m_hot.value(i, false)) { pct = 100; m_level.insert(i, 100); }
        else if (st.mode == 1) { pct = st.fixed * 100; m_level.remove(i); }
        else if (std::isfinite(T)) { pct = curveLevel(i, st, T); m_level.insert(i, pct); }
        else continue;
        if (!std::isfinite(pct)) continue;
        const int p = int(std::max(0.0, std::min(100.0, js::round(pct))));
        const auto sent = m_lastSent.constFind(i);
        if (sent == m_lastSent.cend() || std::abs(p - *sent) >= 2 || (p != *sent && (p == 0 || p == 100))) {
            m_lastSent.insert(i, p);
            send(i, p);
        }
    }
    if (hotChanged) emit changed();
}

void FanController::send(int i, int pct)
{
    if (!canWrite()) return;
    m_sentSeq.insert(i, Inf);
    m_plugin.setPercent(i, pct, [this, i, pct](bool ok, const QString &err) {
        if (ok) {
            if (m_lastSent.value(i, -1) == pct) m_sentSeq.insert(i, double(m_plugin.pollSeq()));
            return;                                       // (a newer speed may already be on the way)
        }
        m_lastSent.remove(i);                             // written again on the next round
        m_sentSeq.insert(i, 0);
        if (!m_plugin.quiet())
            emit commandFailed(m_i18n.t(u"Could not set the speed of fan %1: %2"_s).arg(i + 1).arg(err));
    });
}

void FanController::sendAuto(int fan)
{
    if (canWrite()) m_plugin.setAuto(fan, nullptr);
}

// ---------- max fan (B-1..B-5) ----------

QString FanController::boostBlocked() const
{
    if (m_plugin.id().isEmpty()) return m_plugin.blockedReason(true);   // (no fan plugin installed: it says so)
    if (!canWrite()) return m_i18n.t(u"This fan driver isn't trusted to write fan speeds yet"_s);
    return m_plugin.blockedReason(true);                  // nothing would be written while it isn't running etc.
}

bool FanController::setBoost(qint64 minutes)
{
    if (minutes != 0 && !boostBlocked().isEmpty()) return false;
    if (minutes > 0) m_boostUntil = now() + minutes * 60000;
    else if (minutes < 0) m_boostUntil = -1;
    else if (boosting()) {
        m_boostUntil = 0;
        // Each fan back to its own setting: fans without one go to automatic, Fixed/Curve ones are worked out again
        for (int i = 0; i < m_plugin.fans().size(); ++i) {
            m_lastSent.remove(i);
            m_level.remove(i);
            if (!m_targets.contains(i)) {
                markReleasing(i);
                sendAuto(i);
            }
        }
    }
    emit changed();
    control();
    return true;
}

void FanController::checkBoost()
{
    if (m_boostUntil > 0 && now() >= m_boostUntil) {
        setBoost(0);
        emit boostEnded();
    }
}

// ---------- another config (CF-1) ----------

void FanController::forgetFans()
{
    const QList<int> owned = m_targets.keys() + m_releasing.keys();
    m_targets.clear();
    m_lastSent.clear();
    m_sentSeq.clear();
    m_level.clear();
    m_hot.clear();
    m_releasing.clear();
    for (int k : owned) markReleasing(k);
    if (m_plugin.running() && !boosting()) for (int k : owned) sendAuto(k);
    emit changed();
}

// ---------- quitting (QT-2, QT-3) ----------

void FanController::quit(std::function<void(const QuitResult &)> done)
{
    if (m_quitting) return;
    m_quitting = true;
    // A fan still being handed back counts too (deliberate, the QML forgot those: SPEC.md QT-2): one whose "auto"
    // hasn't taken yet would otherwise stay at its last speed
    const bool own = owned() || !m_releasing.isEmpty();
    m_targets.clear();
    m_boostUntil = 0;
    emit changed();
    if (own && m_plugin.running() && canWrite()) {
        // (deliberate: a failed hand-back is reported, the QML quit silently)
        m_plugin.releaseAll([done](bool ok, const QString &err) {
            QuitResult r;
            r.handBackFailed = !ok;
            r.error = err;
            done(r);
        });
    } else if (own && canWrite()) {
        // Not running, so the fans can't be handed back now: say so if the driver will set fixed speeds again
        m_plugin.willRestoreFixedSpeeds([done](bool fixed) {
            QuitResult r;
            r.fixedSpeedsSaved = fixed;
            done(r);
        });
    } else {
        QTimer::singleShot(0, this, [done] { done(QuitResult()); });
    }
}
