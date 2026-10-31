#pragma once

#include "Paths.h"
#include "SensorReader.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <cmath>
#include <functional>
#include <limits>

// One temperature tile (SPEC.md S-7). Texts are left to the client: `note` is a code, `threads` a number as text.
struct Reading {
    QString label;                 // CPU | GPU | iGPU
    QString model;                 // cleaned hardware name ("" if unknown)
    QString threads;               // CPU only: nproc's answer ("" if unknown)
    double value = std::numeric_limits<double>::quiet_NaN();   // °C, NaN = not known
    QString note;                  // "" | asleep | error | nosensor
    double usage = std::numeric_limits<double>::quiet_NaN();   // % busy, NaN = not known (the iGPU's isn't read)
    bool operator==(const Reading &o) const;
};

struct HwNames {
    QString cpu, cores, gpu, igpu;
    bool operator==(const HwNames &) const = default;
};

// The parsing and state of services/Sensors.qml (SPEC.md S-4..S-11, P-3), with the clock passed in, so it can be
// checked against the QML (.dev/phase2diff.sh) and tested without waiting.
class SensorParser
{
public:
    static QString cleanCpu(const QString &n);
    static QString cleanGpu(const QString &n);
    void parseHw(const QString &txt);                 // hwinfo.sh output
    void parse(const QString &txt, qint64 nowMs);     // one sensors.sh round
    void deactivate();                                // not read any more: no stale values (P-3)
    // Sensors.poll(): is this round a GPU round? (advances the round counter, keeps the back-off below 10 min)
    bool nextRoundReadsGpu(bool fast, qint64 nowMs);

    double gpu() const;
    double cpu() const;
    bool hasGpu() const;
    // NBFC's fan i next to the first fan driver's fan i; only when the counts match (S-10)
    double rpm(int i, int nbfcFans) const;

    HwNames hw;
    QList<Reading> readings;
    QList<double> rpms;
    bool rpmWorks = false;
    // nvidia-smi back-off (S-9)
    int tick = 0;
    double lastNv = std::numeric_limits<double>::quiet_NaN();
    double lastNvUsage = std::numeric_limits<double>::quiet_NaN();
    // /proc/stat's counters of the round before: the CPU's usage is what they did in between
    double lastCpuTotal = 0, lastCpuIdle = 0;
    QString lastNvNote;
    int nvFails = 0;
    qint64 nvRetryAt = 0;
    std::function<void(const QString &)> log;         // the QML's console.log lines
};

class SensorWorker;

// CPU / GPU / iGPU temperatures, hardware names, fan RPM (services/Sensors.qml). Reads in a worker thread every
// 3 s (fast) or 5 s while active; the GPU every round when fast, else every 4th round (nvidia-smi is expensive).
class Sensors : public QObject
{
    Q_OBJECT
public:
    explicit Sensors(const Paths &paths, QObject *parent = nullptr);
    ~Sensors() override;

    void start();                                     // the worker thread and the hardware names (once)
    void pollNow() { poll(); }                        // a round now (skipped if one is running)

    void setActive(bool on);
    void setFast(bool on);
    bool active() const { return m_active; }
    bool fast() const { return m_fast; }
    const SensorParser &state() const { return m_p; }
    int interval() const { return m_timer.interval(); }

    // For tests: reader options and the clock
    void setReaderOptions(const SensorReader::Options &o);
    std::function<qint64()> now;

signals:
    void changed();                                   // readings, rpms or hardware names changed

private:
    void poll();

    SensorParser m_p;
    QTimer m_timer;
    QThread m_thread;
    SensorWorker *m_worker = nullptr;
    bool m_active = false;
    bool m_fast = true;
    bool m_busy = false;
    bool m_hwBusy = false;
    bool m_started = false;
    quint64 m_generation = 0;                         // a round from before the last setActive(false) is stale
    std::atomic<bool> m_stop{ false };                // shutdown: a program the worker runs is stopped at once
};
