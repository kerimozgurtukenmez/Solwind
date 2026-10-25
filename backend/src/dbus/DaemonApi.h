#pragma once

#include <QDBusContext>
#include <QObject>
#include <QVariantMap>

#include <functional>

class ClientHints;
class Settings;
struct Paths;

// D-Bus: name solwind.Daemon, object /solwind, interface solwind.Daemon. Clients (the UI, the solwind command, other
// programs) talk to the daemon only through this: who is running, a clean quit, and what clients show.
class DaemonApi : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "solwind.Daemon")
    Q_PROPERTY(QString Version READ version CONSTANT SCRIPTABLE true)

public:
    static constexpr const char *serviceName = "solwind.Daemon";
    static constexpr const char *objectPath = "/solwind";

    DaemonApi(const Paths &paths, Settings &settings, bool settingsLoaded, ClientHints *hints = nullptr,
              QObject *parent = nullptr);
    QString version() const;
    // More fields for Status() (the reading pace), filled in by main
    std::function<QVariantMap()> extraStatus;

public slots:
    // { version, pid, settingsPath, settingsLoaded (the file existed and was valid JSON), sysRoot ("" = real /sys),
    //   mode: "observe" (phase 2: reads only, never writes to fans, lights or files),
    //   nbfcPollMs, sensorsActive, sensorsFast (how often it reads now) }
    Q_SCRIPTABLE QVariantMap Status() const;
    // What the calling client shows: "window" (a window is open) or "fans" (the fan page is open). The daemon reads
    // as often as that needs (SPEC.md P-2); a client that goes away is forgotten by itself.
    Q_SCRIPTABLE void Watch(const QString &topic);
    Q_SCRIPTABLE void Unwatch(const QString &topic);
    // Quits cleanly (pending settings written first); the reply is sent before the daemon goes away
    Q_SCRIPTABLE void Quit();

signals:
    void quitRequested();

private:
    const Paths &m_paths;
    Settings &m_settings;
    bool m_settingsLoaded;
    ClientHints *m_hints;
};
