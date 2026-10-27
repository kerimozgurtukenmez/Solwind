#pragma once

#include "plugins/FanPlugin.h"
#include "plugins/PluginHost.h"

#include <limits>

// The fan side while no fan plugin is installed: no fans, nothing written, every change refused with that reason.
// The core (FanController, FanState, Alerts, FansApi) runs on it unchanged.
class NoFanPlugin : public FanPlugin
{
public:
    explicit NoFanPlugin(const PluginHost &host, QObject *parent = nullptr) : FanPlugin(parent), m_host(host) {}

    QString id() const override { return QString(); }
    Trust trust() const override { return Trust::ReadOnly; }
    void start() override {}
    void setFast(bool) override {}
    int interval() const override { return 0; }
    bool running() const override { return false; }
    bool readOnly() const override { return false; }
    bool holding() const override { return false; }
    bool quiet() const override { return false; }
    QString configId() const override { return QString(); }
    double temp() const override { return std::numeric_limits<double>::quiet_NaN(); }
    const QList<Fan> &fans() const override { return m_fans; }
    quint64 pollSeq() const override { return 0; }
    quint64 statusSeq() const override { return 0; }
    QString blockedReason(bool) const override { return m_host.t(QStringLiteral("No fan plugin is installed")); }
    void setPercent(int, int, Done done) override { if (done) done(false, blockedReason(true)); }
    void setAuto(int, Done done) override { if (done) done(false, blockedReason(true)); }
    // (nothing was ever driven: nothing to hand back, and quitting must not report a failure)
    void releaseAll(Done done) override { if (done) done(true, QString()); }
    void willRestoreFixedSpeeds(std::function<void(bool)> done) override { if (done) done(false); }

private:
    const PluginHost &m_host;
    QList<Fan> m_fans;
};
