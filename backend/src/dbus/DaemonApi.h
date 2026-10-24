#pragma once

#include <QObject>
#include <QVariantMap>

class Settings;
struct Paths;

// D-Bus: name solwind.Daemon, object /solwind, interface solwind.Daemon. Clients (the UI, the solwind command, other
// programs) talk to the daemon only through this. Phase 1: who is running and a clean quit.
class DaemonApi : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "solwind.Daemon")
    Q_PROPERTY(QString Version READ version CONSTANT SCRIPTABLE true)

public:
    static constexpr const char *serviceName = "solwind.Daemon";
    static constexpr const char *objectPath = "/solwind";

    DaemonApi(const Paths &paths, Settings &settings, bool settingsLoaded, QObject *parent = nullptr);
    QString version() const;

public slots:
    // { version, pid, settingsPath, settingsLoaded (the file existed and was valid JSON), sysRoot ("" = real /sys) }
    Q_SCRIPTABLE QVariantMap Status() const;
    // Quits cleanly (pending settings written first); the reply is sent before the daemon goes away
    Q_SCRIPTABLE void Quit();

signals:
    void quitRequested();

private:
    const Paths &m_paths;
    Settings &m_settings;
    bool m_settingsLoaded;
};
