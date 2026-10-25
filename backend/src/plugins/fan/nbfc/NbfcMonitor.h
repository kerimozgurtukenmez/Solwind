#pragma once

#include "NbfcParse.h"
#include "core/Cmd.h"
#include "core/CommandQueue.h"
#include "core/Paths.h"

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

#include <functional>

// NBFC-Linux, read side only (phase 2 of ARCH-PLAN.md): the service's status every 2 s / 5 s, which config is in use,
// the config list and the selected config's curves, start-on-boot, recommendations. Ports services/Nbfc.qml's
// reading rules (SPEC.md N-9..N-14, A-8, A-10..A-12, L-1..L-3). Nothing here can change a fan speed, the service or
// a file: phase 3 adds writing, behind the safety rules.
class NbfcMonitor : public QObject
{
    Q_OBJECT
public:
    explicit NbfcMonitor(const Paths &paths, QObject *parent = nullptr);

    void start();                         // first status read now, config list, boot state, nbfc.json watch
    void setFast(bool on);                // 2 s while a window is open or Solwind drives fans, else 5 s
    void pollNow() { poll(); }            // a status read now (skipped if one is running)
    int interval() const { return m_timer.interval(); }

    // `nbfc config -r` (A-11): ok = it ran; found = the known configs it names (also kept in recommended())
    void recommend(std::function<void(bool ok, const QStringList &found)> done);

    // ---------- state ----------
    bool running() const { return m_running; }
    bool readOnly() const { return m_readOnly; }
    double temp() const { return m_temp; }
    const QList<Nbfc::FanStatus> &fans() const { return m_fans; }
    QString configId() const { return m_configId; }
    QString fileConfigId() const { return m_fileConfigId; }
    QString serviceConfigId() const { return m_serviceConfigId; }
    bool configRead() const { return m_configRead; }
    bool noConfig() const { return m_configRead && m_fileConfigId.isEmpty() && m_configId.isEmpty(); }
    bool configPending() const
    {
        return m_running && !m_fileConfigId.isEmpty() && !m_serviceConfigId.isEmpty() && m_fileConfigId != m_serviceConfigId;
    }
    bool bootEnabled() const { return m_bootEnabled; }
    const QStringList &configs() const { return m_configs; }
    const QList<std::optional<Nbfc::ConfigCurve>> &configCurves() const { return m_curves; }
    const QStringList &recommended() const { return m_recommended; }
    // Where the config list came from, for the client's note (A-10): the folders that had configs, and
    // `nbfc config -l`'s exit code (-1 = not run yet), first output line and number of configs
    const QStringList &configDirsUsed() const { return m_cfgDirs; }
    const QStringList &configsFromDirs() const { return m_cfgFromDir; }
    const QStringList &configsFromCli() const { return m_cfgFromCli; }
    int cliListCode() const { return m_cliCode; }
    QString cliListLine() const { return m_cliLine; }
    // "Did NBFC see it yet?" is judged by counting status reads (N-9)
    quint64 pollSeq() const { return m_pollSeq; }
    quint64 statusSeq() const { return m_statusSeq; }

signals:
    void statusRead(bool ok);             // after every status read
    void changed();                       // something above changed
    void configIdChanged();
    void serviceLost();                   // a running service stopped unexpectedly (L-2)

private:
    void poll();
    void onStatus(quint64 seq, int code, const QByteArray &out, const QByteArray &err);
    void updateConfigId();
    void readNbfcJson();
    void watchNbfcJson();
    void loadConfigs();
    void listConfigDirs();
    void mergeConfigs();
    void loadConfigCurves();
    void refreshBoot();

    Paths m_paths;
    QString m_nbfcJson;
    Cmd m_status;                         // its own runner: a poll while one runs is skipped (Q-4)
    CommandQueue m_queue;                 // other nbfc / systemctl commands, one after another (Q-1, Q-2)
    QTimer m_timer;
    QTimer m_jsonDebounce;
    QFileSystemWatcher m_watcher;

    bool m_running = false;
    bool m_readOnly = false;
    double m_temp = Nbfc::NaN;
    QList<Nbfc::FanStatus> m_fans;
    QString m_configId, m_fileConfigId, m_serviceConfigId;
    int m_failedPolls = 0;
    bool m_configRead = false;
    bool m_lostPending = false;
    bool m_userStopped = false;           // set by a successful stop (phase 3)
    bool m_logged = false;
    quint64 m_pollSeq = 0, m_statusSeq = 0;

    bool m_bootEnabled = false;
    QStringList m_cfgFromCli, m_cfgFromDir, m_cfgDirs, m_configs, m_recommended;
    int m_cliCode = -1;
    QString m_cliLine;
    QList<std::optional<Nbfc::ConfigCurve>> m_curves;
    QList<std::function<void(bool, const QStringList &)>> m_recWaiters;
};
