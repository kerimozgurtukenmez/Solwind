#pragma once

#include "NbfcMonitor.h"
#include "core/Cmd.h"
#include "plugins/FanPlugin.h"

#include <QList>
#include <QStringList>

class I18n;

// NBFC-Linux as a fan plugin: the read side (NbfcMonitor) plus speed commands (`nbfc set`, no root needed) and the
// service actions that need root (`pkexec nbfc start|stop|restart|config`, `pkexec systemctl enable|disable`).
// Ports services/Nbfc.qml (SPEC.md Q-1..Q-5, A-1..A-9, L-1..L-3, QT-3). Trust: WriteTested - the QML app has
// written NBFC speeds on the dev machine since 0.1.
//
// NBFC keeps target speeds and saves them when it stops, then sets them again at its next start - also in read-only
// mode, also for a new config by fan number (SPEC.md A-3). So before stopping NBFC or switching its config every fan
// goes back to automatic, and nothing is written until that action has finished (holding()).
class NbfcPlugin : public FanPlugin
{
    Q_OBJECT
public:
    NbfcPlugin(const Paths &paths, const I18n &i18n, QObject *parent = nullptr);

    NbfcMonitor &monitor() { return m_mon; }
    const NbfcMonitor &monitor() const { return m_mon; }
    void start() { m_mon.start(); }

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
    void willRestoreFixedSpeeds(std::function<void(bool)> done) override;

    QString blockedReason(bool withHolding = true) const override;

    // ---------- service actions (A-4..A-9): run through pkexec, one after another, no time limit ----------
    void startService();
    void stopService();
    void restartService(bool readOnly);
    void setReadOnly(bool on);
    void setBoot(bool on);
    void applyConfig(const QString &name);
    void recommend(std::function<void(bool ok, const QStringList &found)> done);
    QString message() const { return m_message; }        // what an action is waiting for / why it failed
    bool messageError() const { return m_messageError; }
    bool adminBusy() const { return m_admin.busy() || !m_adminQueue.isEmpty(); }

    // Set while quitting (L-2: a lost service isn't reported then)
    void setQuitting(bool on) { m_quitting = on; }

    std::function<qint64()> now;                          // the clock (tests)

private:
    void sudo(const QStringList &args, const QString &label, std::function<void(int code)> then = nullptr);
    void handBackThen(const QStringList &args, std::function<void(int code)> then = nullptr);
    void pumpAdmin();
    void syncFans();
    void setMessage(const QString &text, bool error);

    Paths m_paths;
    const I18n &m_i18n;
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
