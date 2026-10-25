#pragma once

#include <QList>

#include <limits>

// The graph's data: the last 10 minutes, one sample per successful NBFC status read (SPEC.md H-1). Not saved.
class SampleHistory
{
public:
    struct Sample {
        qint64 t = 0;                                        // ms since the epoch
        double cpu = std::numeric_limits<double>::quiet_NaN();   // NBFC's temperature
        double gpu = std::numeric_limits<double>::quiet_NaN();
        double fan = std::numeric_limits<double>::quiet_NaN();   // average current speed of the fans, %
    };
    static constexpr qint64 spanMs = 600000;

    // Adds a sample; samples older than 10 minutes go (a clock jump back keeps them: now - t is then negative)
    const Sample &record(qint64 now, double cpu, double gpu, const QList<double> &fanSpeeds)
    {
        Sample s{ now, cpu, gpu, std::numeric_limits<double>::quiet_NaN() };
        if (!fanSpeeds.isEmpty()) {
            double sum = 0;
            for (double v : fanSpeeds) sum += v;
            s.fan = sum / fanSpeeds.size();
        }
        m_samples.removeIf([now](const Sample &x) { return now - x.t > spanMs; });
        m_samples.append(s);
        return m_samples.last();
    }
    const QList<Sample> &samples() const { return m_samples; }

private:
    QList<Sample> m_samples;
};
