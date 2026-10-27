#pragma once

#include "DBusUtil.h"
#include "core/Settings.h"

#include <QDBusContext>
#include <QObject>

class FanController;
class FanState;
class I18n;
class NbfcPlugin;
class Sensors;
class SampleHistory;

// D-Bus: /solwind/Fans, interface solwind.Fans - NBFC's state, the configs, the graph's history, and (control mode)
// the fans' settings, profiles, max fan, the safety limit and the NBFC service actions. Changes come as
// PropertiesChanged; every new graph sample also as Sampled.
// Errors: solwind.Error.Usage (a wrong argument: exit 2 in the terminal), solwind.Error.Failed (refused now: exit 1),
// solwind.Error.Observe (the daemon only reads, it doesn't drive fans). Messages are in the UI language.
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

    // ---------- control ----------
    // Per fan: { mode: 0 auto | 1 fixed | 2 curve, fixed: 0..1, curve: [{t: °C, s: %}], smooth, src: cpu|gpu|max, hyst }
    Q_PROPERTY(VariantMapList FanSettings READ fanSettings)
    Q_PROPERTY(QVariantMap Global READ global)               // the same, written to every fan while GlobalOn
    Q_PROPERTY(bool GlobalOn READ globalOn)
    Q_PROPERTY(bool Restored READ restored)                  // the saved settings of this config are applied
    // [{ name (the id for built-ins), label (in the UI language), builtin, icon }]
    Q_PROPERTY(VariantMapList Profiles READ profiles)
    Q_PROPERTY(QString ActiveProfile READ activeProfile)     // its name ("" = own settings)
    Q_PROPERTY(QStringList ReservedNames READ reservedNames)   // names a user profile can't take (lower case)
    Q_PROPERTY(qlonglong BoostUntil READ boostUntil)         // max fan: 0 off, -1 until turned off, else the end (ms)
    Q_PROPERTY(QString BoostBlocked READ boostBlocked)       // why max fan can't be turned on now ("" = it can)
    Q_PROPERTY(double Safety READ safety)                    // Fixed/Curve fans go to 100% at this temperature
    Q_PROPERTY(QList<int> SafetyHeld READ safetyHeld)        // fans held at 100% by the safety limit now
    Q_PROPERTY(bool Controlling READ controlling)            // Solwind drives fans (Fixed/Curve/max fan)
    Q_PROPERTY(bool Holding READ holding)                    // an NBFC action that handed the fans back is running
    Q_PROPERTY(bool AdminBusy READ adminBusy)                // an action waits for the password dialog / runs
    Q_PROPERTY(QString Message READ message)                 // what that action waits for, or why it failed
    Q_PROPERTY(bool MessageError READ messageError)

public:
    static constexpr const char *path = "/solwind/Fans";
    FansApi(NbfcPlugin &nbfc, const Sensors &sensors, const SampleHistory &history, FanController &control,
            FanState &state, const I18n &i18n, Settings &settings, bool controlMode, QObject *parent = nullptr);

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

    VariantMapList fanSettings() const;
    QVariantMap global() const;
    bool globalOn() const;
    bool restored() const;
    VariantMapList profiles() const;
    QString activeProfile() const;
    QStringList reservedNames() const;
    qlonglong boostUntil() const;
    QString boostBlocked() const;
    double safety() const;
    QList<int> safetyHeld() const;
    bool controlling() const;
    bool holding() const;
    bool adminBusy() const;
    QString message() const;
    bool messageError() const;

    static QVariantMap sampleMap(qint64 t, double cpu, double gpu, double fan);
    static QVariantMap stMap(const FanSt &st);
    // A fan setting from a client, checked as strictly as the UI's editor (SPEC.md UI-2, UI-3): mode 0..2, fixed 0..1,
    // 2..12 curve points with whole t 30..100 rising by at least 1 °C and whole s 0..100, src cpu|gpu|max, whole hyst
    // 0..10. Missing fields get the defaults; unknown fields are refused. nullopt + why if it isn't valid.
    static std::optional<FanSt> stFromClient(const QVariantMap &m, QString *why);

public slots:
    // `nbfc config -r`: (ok, the known configs it names). Only reads.
    Q_SCRIPTABLE bool Recommend(QStringList &found);
    // The last 10 minutes: [{ t: ms since the epoch, cpu, gpu, fan }]
    Q_SCRIPTABLE VariantMapList History() const;
    Q_SCRIPTABLE QString StatusText() const;                 // `solwind status`
    Q_SCRIPTABLE QString ProfileList() const;                // `solwind profiles` ("* " marks the active one)

    Q_SCRIPTABLE void SetFan(int fan, const QVariantMap &st);
    Q_SCRIPTABLE void SetGlobal(const QVariantMap &st);
    Q_SCRIPTABLE void SetGlobalOn(bool on);
    // By any name it answers to; notify: a terminal / keybind asked (notified if notify.profile). Returns "Applied: …"
    Q_SCRIPTABLE QString ApplyProfile(const QString &name, bool notify);
    Q_SCRIPTABLE void SaveProfile(const QString &name);      // the current settings under this name
    Q_SCRIPTABLE void RemoveProfile(const QString &name);    // a user profile, by its exact name
    // Max fan: minutes > 0 for that long, < 0 until turned off, 0 off (always possible)
    Q_SCRIPTABLE void SetBoost(qlonglong minutes);
    Q_SCRIPTABLE void SetSafety(int celsius);                // 70..100

    // NBFC service actions: they ask for the admin password (pkexec) and run in the background; Message / AdminBusy
    // follow them
    Q_SCRIPTABLE void StartService();
    Q_SCRIPTABLE void StopService();
    Q_SCRIPTABLE void RestartService();                      // with the current read-only mode
    Q_SCRIPTABLE void SetReadOnly(bool on);
    Q_SCRIPTABLE void SetBoot(bool on);
    Q_SCRIPTABLE void ApplyConfig(const QString &name);

signals:
    Q_SCRIPTABLE void Sampled(const QVariantMap &sample);

private:
    bool changeRefused();
    void usage(const QString &msg);
    void failed(const QString &msg);

    NbfcPlugin &m_nbfc;
    const Sensors &m_sensors;
    const SampleHistory &m_history;
    FanController &m_control;
    FanState &m_state;
    const I18n &m_i18n;
    Settings &m_settings;
    bool m_controlMode;
};
