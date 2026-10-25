#pragma once

#include "Settings.h"
#include "plugins/FanPlugin.h"

#include <QMap>
#include <QObject>

#include <functional>
#include <limits>

class I18n;

// The fan control loop - port of services/Nbfc.qml's control part (SPEC.md §6 C-1..C-12, §7 B-1..B-5, §11 CF-1,
// §10 QT-2/QT-3). Runs after every status read of the plugin; writes only through the plugin, and only while the
// plugin is trusted to write and can take speeds now (C-1). Every rule here is the core's, never a plugin's
// (ARCH-PLAN rule 2): the safety limit, curves, "did it take", handing fans back, max fan.
class FanController : public QObject
{
    Q_OBJECT
public:
    FanController(FanPlugin &plugin, const I18n &i18n, QObject *parent = nullptr);

    std::function<double()> gpuTemp;      // the discrete GPU's temperature (NaN while asleep / absent)
    std::function<qint64()> now;          // the clock (tests)

    // ---------- settings ----------
    double safety() const { return m_safety; }           // at this temperature Fixed/Curve fans go to 100% (C-7)
    void setSafety(double v) { m_safety = v; }
    static constexpr int safetyHyst = 3;                  // released this far below the limit
    static constexpr int rampDown = 5;                    // % per round at most when slowing down

    // ---------- control ----------
    // idx = fan index, -1 = all current fans; st.mode 0 hands the fan back to the driver's automatic control (C-2)
    void applyTarget(int idx, const FanSt &st);
    void control();                                       // after every successful status read (C-1..C-11)
    void checkBoost();                                    // after every successful status read (B-5)
    void forgetFans();                                    // another config: the old one's settings don't apply (CF-1)

    // Max fan (B-1..B-4): minutes > 0 for that long, < 0 until turned off, 0 off. false if it can't be turned on now
    bool setBoost(qint64 minutes);
    bool boosting() const { return m_boostUntil != 0; }
    qint64 boostUntil() const { return m_boostUntil; }  // 0 off, -1 until turned off, else the end (ms)
    QString boostBlocked() const;                         // why it can't be turned on now ("" = it can)

    // Fans in Fixed/Curve mode (sensors are needed, P-1), and whether Solwind drives fans at all (fast pace)
    bool needsSensors() const { return !m_targets.isEmpty(); }
    bool controlling() const { return needsSensors() || boosting(); }
    bool owned() const { return controlling(); }          // something to hand back when quitting
    const QMap<int, FanSt> &targets() const { return m_targets; }
    bool hot(int fan) const { return m_hot.value(fan, false); }   // held at 100% by the safety limit
    bool quitting() const { return m_quitting; }

    // Quit (QT-2, QT-3): nothing is written any more; owned fans go back to automatic first, then done()
    struct QuitResult {
        bool fixedSpeedsSaved = false;    // the driver isn't running, and sets fixed speeds again when it starts
        bool handBackFailed = false;      // the driver didn't take "everything back to automatic"
        QString error;                    // why (its first line)
    };
    void quit(std::function<void(const QuitResult &)> done);

    // ---------- the maths (pure, as in Nbfc.qml) ----------
    static double curveAt(const FanSt &st, double t);
    // Speed up at once; slow down only when the temperature is st.hyst °C below the curve, by at most rampDown (C-9)
    double curveLevel(int fan, const FanSt &st, double t) const;
    double sourceTemp(int fan, const QString &src) const;   // C-6: cpu (the driver's) | gpu | max, GPU falls back
    double hottest(int fan) const { return sourceTemp(fan, QStringLiteral("max")); }
    double level(int fan) const { return m_level.value(fan, std::numeric_limits<double>::quiet_NaN()); }
    void setLevel(int fan, double v) { m_level.insert(fan, v); }   // (tests)

    // The loop's own state, read only (tests: the differential check against the QML)
    struct Release { qint64 at; quint64 seq; int tries; };
    const QMap<int, int> &lastSent() const { return m_lastSent; }
    const QMap<int, double> &sentSeq() const { return m_sentSeq; }
    const QMap<int, double> &levels() const { return m_level; }
    const QMap<int, bool> &hotFans() const { return m_hot; }
    const QMap<int, Release> &releasing() const { return m_releasing; }

signals:
    void commandFailed(const QString &msg);               // a speed could not be written (C-11)
    void releaseFailed(const QString &msg);               // a fan could not be handed back (C-12)
    void boostEnded();                                    // a timed max fan run ended (B-5)
    void changed();                                       // targets, max fan or held fans changed

private:
    bool canWrite() const;
    double tempOf(int fan) const;
    void markReleasing(int fan);
    void checkRelease(int fan, qint64 now);
    void send(int fan, int pct);
    void sendAuto(int fan);

    FanPlugin &m_plugin;
    const I18n &m_i18n;
    double m_safety = 90;
    // Per fan index, like the JS objects they port (a key may outlive the current fan list)
    QMap<int, FanSt> m_targets;           // Fixed/Curve fans
    QMap<int, int> m_lastSent;            // absent = nothing sent / to be written again
    QMap<int, double> m_sentSeq;          // poll number when the driver took the last speed; +inf while on the way
    QMap<int, double> m_level;            // current level of a Curve fan (%)
    QMap<int, bool> m_hot;
    QMap<int, Release> m_releasing;       // handed back, not confirmed yet
    qint64 m_boostUntil = 0;
    bool m_quitting = false;
};
