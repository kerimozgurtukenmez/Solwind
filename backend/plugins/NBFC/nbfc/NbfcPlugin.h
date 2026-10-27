#pragma once

#include "NbfcMonitor.h"
#include "core/Cmd.h"
#include "plugins/FanPlugin.h"
#include "plugins/FanService.h"
#include "plugins/PluginHost.h"

#include <QList>
#include <QStringList>

// NBFC-Linux as a fan plugin: the read side (NbfcMonitor) plus speed commands (`nbfc set`, no root needed) and the
// service actions that need root (`pkexec nbfc start|stop|restart|config`, `pkexec systemctl enable|disable`).
// Ports services/Nbfc.qml (SPEC.md Q-1..Q-5, A-1..A-9, L-1..L-3, QT-3). Trust: WriteTested - the QML app has
// written NBFC speeds on the dev machine since 0.1.
//
// NBFC keeps target speeds and saves them when it stops, then sets them again at its next start - also in read-only
// mode, also for a new config by fan number (SPEC.md A-3). So before stopping NBFC or switching its config every fan
// goes back to automatic, and nothing is written until that action has finished (holding()).
class NbfcPlugin : public FanPlugin, public FanService
{
    Q_OBJECT
public:
    explicit NbfcPlugin(const PluginHost &host, QObject *parent = nullptr);

    NbfcMonitor &monitor() { return m_mon; }
    const NbfcMonitor &monitor() const { return m_mon; }
    void start() override { m_mon.start(); }
    void setFast(bool fast) override { m_mon.setFast(fast); }
    int interval() const override { return m_mon.interval(); }
    FanService *service() override { return this; }
    const FanService *service() const override { return this; }

    // ---------- FanPlugin ----------
    QString id() const override { return QStringLiteral("nbfc"); }
    Trust trust() const override { return Trust::WriteTested; }
    bool running() const override { return m_mon.running(); }
    bool readOnly() const override { return m_mon.readOnly(); }
    bool holding() const override { return m_holds > 0; }
    bool quiet() const override;
    QString configId() const override { return m_mon.configId(); }
    double temp() const override { return m_mon.temp(); }
    const QList<Fan> &fans() const override { return m_fans; }
    quint64 pollSeq() const override { return m_mon.pollSeq(); }
    quint64 statusSeq() const override { return m_mon.statusSeq(); }
    void setPercent(int fan, int pct, Done done) override;
    void setAuto(int fan, Done done) override;
    void releaseAll(Done done) override;
    // (NBFC keeps a fixed speed it was given until it is told otherwise, also when solwindd is gone)
    QStringList handBackCommand() const override { return { QStringLiteral("nbfc"), QStringLiteral("set"), QStringLiteral("-a") }; }
    void willRestoreFixedSpeeds(std::function<void(bool)> done) override;

    QString blockedReason(bool withHolding = true) const override;

    // ---------- FanService: service actions (A-4..A-9): run through pkexec, one after another, no time limit ----------
    void startService() override;
    void stopService() override;
    void restartService(bool readOnly) override;
    void setReadOnly(bool on) override;
    void setBoot(bool on) override;
    void applyConfig(const QString &name) override;
    void recommend(std::function<void(bool ok, const QStringList &found)> done) override;
    QString message() const override { return m_message; }        // what an action is waiting for / why it failed
    bool messageError() const override { return m_messageError; }
    bool adminBusy() const override { return m_admin.busy() || !m_adminQueue.isEmpty(); }
    // ---------- FanService: the configs ----------
    bool bootEnabled() const override { return m_mon.bootEnabled(); }
    QString fileConfigId() const override { return m_mon.fileConfigId(); }
    bool noConfig() const override { return m_mon.noConfig(); }
    bool configPending() const override { return m_mon.configPending(); }
    QStringList configs() const override { return m_mon.configs(); }
    QVariantMap configList() const override;
    QList<std::optional<Curve>> configCurves() const override;
    QStringList recommended() const override { return m_mon.recommended(); }

    // Set while quitting (L-2: a lost service isn't reported then)
    void setQuitting(bool on) override { m_quitting = on; }

    std::function<qint64()> now;                          // the clock (tests)

private:
    void sudo(const QStringList &args, const QString &label, std::function<void(int code)> then = nullptr);
    void handBackThen(const QStringList &args, std::function<void(int code)> then = nullptr);
    void pumpAdmin();
    void syncFans();
    void setMessage(const QString &text, bool error);

    Paths m_paths;
    const PluginHost &m_host;
    NbfcMonitor m_mon;
    QList<Fan> m_fans;

    struct AdminJob { QStringList cmd; std::function<void(int, const QByteArray &, const QByteArray &)> done; };
    Cmd m_admin;                           // no time limit: it waits for the password dialog
    QList<AdminJob> m_adminQueue;
    qint64 m_adminQuietUntil = 0;
    int m_holds = 0;
    bool m_quitting = false;
    QString m_message;
    bool m_messageError = false;
};
