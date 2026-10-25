#pragma once

#include "DBusUtil.h"

#include <QDBusContext>
#include <QObject>

class NbfcMonitor;
class Sensors;
class SampleHistory;

// D-Bus: /solwind/Fans, interface solwind.Fans. Phase 2: read only - the NBFC service's state, the configs, the
// graph's history. Changes come as PropertiesChanged; every new graph sample also as Sampled.
class FansApi : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "solwind.Fans")
    Q_PROPERTY(bool Running READ running)
    Q_PROPERTY(bool ReadOnly READ readOnly)
    Q_PROPERTY(double Temp READ temp)                        // NBFC's temperature (°C, NaN while it isn't running)
    // [{ name, temp, auto, critical, current, target (%), rpm (NaN unless the driver's fans match NBFC's) }]
    Q_PROPERTY(VariantMapList Fans READ fans)
    Q_PROPERTY(QString ConfigId READ configId)               // the config these fans belong to
    Q_PROPERTY(QString FileConfigId READ fileConfigId)       // selected in /etc/nbfc/nbfc.json
    Q_PROPERTY(bool NoConfig READ noConfig)                  // none selected yet: NBFC can't start
    Q_PROPERTY(bool ConfigPending READ configPending)        // selected, used after NBFC's next restart
    Q_PROPERTY(bool BootEnabled READ bootEnabled)
    Q_PROPERTY(QStringList Configs READ configs)
    // { dirs: folders that had configs, fromDirs, fromCli: counts, cliCode: nbfc config -l's exit code (-1 = not run
    //   yet), cliLine: its first output line when it failed }
    Q_PROPERTY(QVariantMap ConfigList READ configList)
    // One per fan of the selected config: {} = no threshold table, else { points: [{t, s}], hyst }
    Q_PROPERTY(VariantMapList ConfigCurves READ configCurves)
    Q_PROPERTY(QStringList Recommended READ recommended)

public:
    static constexpr const char *path = "/solwind/Fans";
    FansApi(NbfcMonitor &nbfc, const Sensors &sensors, const SampleHistory &history, QObject *parent = nullptr);

    bool running() const;
    bool readOnly() const;
    double temp() const;
    VariantMapList fans() const;
    QString configId() const;
    QString fileConfigId() const;
    bool noConfig() const;
    bool configPending() const;
    bool bootEnabled() const;
    QStringList configs() const;
    QVariantMap configList() const;
    VariantMapList configCurves() const;
    QStringList recommended() const;

    static QVariantMap sampleMap(qint64 t, double cpu, double gpu, double fan);

public slots:
    // `nbfc config -r`: (ok, the known configs it names). Only reads.
    Q_SCRIPTABLE bool Recommend(QStringList &found);
    // The last 10 minutes: [{ t: ms since the epoch, cpu, gpu, fan }]
    Q_SCRIPTABLE VariantMapList History() const;

signals:
    Q_SCRIPTABLE void Sampled(const QVariantMap &sample);

private:
    NbfcMonitor &m_nbfc;
    const Sensors &m_sensors;
    const SampleHistory &m_history;
};
