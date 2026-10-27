#include "Alerts.h"

#include "FanController.h"
#include "I18n.h"
#include "JsCompat.h"
#include "Notifier.h"
#include "plugins/FanPlugin.h"

#include <QDateTime>

#include <cmath>

using namespace Qt::StringLiterals;

Alerts::Alerts(FanPlugin &plugin, FanController &control, const I18n &i18n, Notifier &notifier, QObject *parent)
    : QObject(parent), m_plugin(plugin), m_control(control), m_i18n(i18n), m_notifier(notifier)
{
    now = [] { return QDateTime::currentMSecsSinceEpoch(); };
    sensorCpu = sensorGpu = [] { return std::numeric_limits<double>::quiet_NaN(); };

    m_critical.setSingleShot(true);
    m_critical.setInterval(300);
    connect(&m_critical, &QTimer::timeout, this, [this] {
        // Say what really happens: nothing is written while NBFC is stopped or read-only
        const bool owned = !m_control.targets().isEmpty();
        const QString what = m_plugin.id().isEmpty()
            ? m_i18n.t(u"Safety limit (%1°C) exceeded; no fan plugin is installed, so Solwind can't change the fan speed."_s)
            : !m_plugin.running()
            ? m_i18n.t(u"Safety limit (%1°C) exceeded; NBFC isn't running, so Solwind can't change the fan speed."_s)
            : m_plugin.readOnly()
            ? m_i18n.t(u"Safety limit (%1°C) exceeded; NBFC is in read-only mode, so fan speeds can't be set."_s)
            : owned ? m_i18n.t(u"Safety limit (%1°C) exceeded; fans driven by Solwind were set to 100%."_s)
                    : m_i18n.t(u"Safety limit (%1°C) exceeded; the fans are under NBFC control."_s);
        m_notifier.send(m_i18n.t(u"Critical temperature: %1°C (%2)"_s).arg(qint64(js::round(m_critTemp))).arg(m_critWho),
                        what.arg(m_control.safety()), Notifier::Critical, u"dialog-warning"_s);
    });

    connect(&m_plugin, &FanPlugin::serviceLost, this, [this] {
        m_notifier.send(m_i18n.t(u"NBFC service stopped"_s),
                        m_i18n.t(u"Fan control is not possible. Restart it from Solwind or with `sudo nbfc start`."_s),
                        Notifier::Critical, u"dialog-error"_s);
    });
    connect(&m_control, &FanController::commandFailed, this, [this](const QString &msg) {
        const qint64 t = now();
        if (t >= m_lastFail && t - m_lastFail < 300000) return;     // at most once every 5 minutes
        m_lastFail = t;
        m_notifier.send(m_i18n.t(u"Could not set the fan speed"_s), msg, Notifier::Critical, u"dialog-error"_s);
    });
    // Its own limit: a speed error just before must not hide that a fan was left at a fixed speed
    connect(&m_control, &FanController::releaseFailed, this, [this](const QString &msg) {
        const qint64 t = now();
        if (t >= m_lastRelease && t - m_lastRelease < 300000) return;
        m_lastRelease = t;
        m_notifier.send(m_i18n.t(u"Could not hand a fan back to NBFC"_s), msg, Notifier::Critical, u"dialog-error"_s);
    });
    connect(&m_control, &FanController::boostEnded, this, [this] {
        m_notifier.send(m_i18n.t(u"Max fan ended"_s), m_i18n.t(u"The fans went back to their previous settings."_s),
                        Notifier::Low, u"dialog-information"_s);
    });
}

void Alerts::update()
{
    // CPU: NBFC's sensor, or the kernel's while NBFC isn't running (then NBFC reports nothing)
    const double cpu = std::isfinite(m_plugin.temp()) ? m_plugin.temp() : sensorCpu();
    const double g = sensorGpu();
    const double hot = std::isfinite(g) ? (std::isfinite(cpu) ? js::max(cpu, g) : g) : cpu;
    const bool changed = !(hot == m_hot || (std::isnan(hot) && std::isnan(m_hot)));
    m_hot = hot;
    if (changed) hotChanged(hot, cpu);
}

void Alerts::hotChanged(double hot, double cpu)
{
    if (!std::isfinite(hot)) return;
    const double safety = m_control.safety();
    if (m_armed && hot >= safety) {
        m_armed = false;
        // The temperature and where it came from are the ones that crossed the limit
        const double g = sensorGpu();
        m_critTemp = hot;
        m_critWho = std::isfinite(g) && !(g < cpu) ? u"GPU"_s : u"CPU"_s;
        m_critical.start();
    } else if (!m_armed && hot < safety - 5) {
        m_armed = true;
    }
}
