#pragma once

#include "DBusUtil.h"

#include <QObject>

class Sensors;

// D-Bus: /solwind/Sensors, interface solwind.Sensors (read only). Changes come as PropertiesChanged.
class SensorsApi : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "solwind.Sensors")
    // [{ label: CPU|GPU|iGPU, model, threads (CPU), value: °C or NaN, note: ""|asleep|error|nosensor }]
    Q_PROPERTY(VariantMapList Readings READ readings)
    Q_PROPERTY(QVariantMap Hardware READ hardware)           // { cpu, cores, gpu, igpu }
    Q_PROPERTY(double Cpu READ cpu)
    Q_PROPERTY(double Gpu READ gpu)                          // the discrete GPU, NaN while asleep / absent
    Q_PROPERTY(bool HasGpu READ hasGpu)
    Q_PROPERTY(DoubleList Rpms READ rpms)                    // the first fan driver's fans, NaN = not reported
    Q_PROPERTY(bool RpmWorks READ rpmWorks)                  // the driver has reported a speed above 0 at least once

public:
    static constexpr const char *path = "/solwind/Sensors";
    explicit SensorsApi(const Sensors &s, QObject *parent = nullptr);

    VariantMapList readings() const;
    QVariantMap hardware() const;
    double cpu() const;
    double gpu() const;
    bool hasGpu() const;
    DoubleList rpms() const;
    bool rpmWorks() const;

private:
    const Sensors &m_s;
};
