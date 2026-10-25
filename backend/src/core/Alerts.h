#pragma once

#include <QObject>
#include <QTimer>

#include <functional>
#include <limits>

class FanController;
class FanPlugin;
class I18n;
class Notifier;

// Watches for events and sends notifications - port of services/Alerts.qml (SPEC.md NT-2..NT-6)
class Alerts : public QObject
{
    Q_OBJECT
public:
    Alerts(FanPlugin &plugin, FanController &control, const I18n &i18n, Notifier &notifier, QObject *parent = nullptr);

    std::function<double()> sensorCpu;    // the kernel's CPU temperature (used while NBFC reports none)
    std::function<double()> sensorGpu;
    std::function<qint64()> now;

    // After every NBFC status read and every sensor round: the hottest temperature may have changed (NT-2 runs only
    // when it did, like the QML's onHotChanged)
    void update();

private:
    void hotChanged(double hot, double cpu);

    FanPlugin &m_plugin;
    FanController &m_control;
    const I18n &m_i18n;
    Notifier &m_notifier;
    double m_hot = std::numeric_limits<double>::quiet_NaN();
    bool m_armed = true;                 // critical temperature: once at the limit, again after 5 °C cooling
    QTimer m_critical;                   // worded 300 ms later: saved fan settings may still be on their way
    double m_critTemp = std::numeric_limits<double>::quiet_NaN();
    QString m_critWho;
    qint64 m_lastFail = 0, m_lastRelease = 0;
};
