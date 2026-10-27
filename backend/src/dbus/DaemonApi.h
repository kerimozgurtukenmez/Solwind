#pragma once

#include <QDBusContext>
#include <QObject>
#include <QVariantMap>

#include "DBusUtil.h"

#include <functional>

class ClientHints;
class I18n;
class PluginLoader;
class Settings;
struct Paths;

// D-Bus: name solwind.Daemon, object /solwind, interface solwind.Daemon. Clients (the UI, the solwind command, other
// programs) talk to the daemon only through this: who is running, a clean quit, and what clients show.
class DaemonApi : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "solwind.Daemon")
    Q_PROPERTY(QString Version READ version CONSTANT SCRIPTABLE true)
    // The settings clients show (the daemon owns settings.json; clients don't read it)
    Q_PROPERTY(QString Language READ language)               // the language texts are in: "tr" | "en"
    Q_PROPERTY(QString SavedLanguage READ savedLanguage)     // the saved choice: "tr" | "en" | "" (the system's)
    Q_PROPERTY(bool NotifyEnabled READ notifyEnabled)        // notify.enabled
    Q_PROPERTY(bool NotifyProfile READ notifyProfile)        // notify.profile
    Q_PROPERTY(bool TrayEnabled READ trayEnabled)            // ui.tray (the UI's tray icon)
    Q_PROPERTY(QString Mode READ mode)                       // "control" | "observe" (only reads: changes are refused)
    // The loaded plugins: { id, brand, module, kind (fan | keyboard), version, description, file, used (it drives the
    // fans - one fan plugin does -, or it has keyboard plugins) }; the files that weren't loaded: { file, why }
    Q_PROPERTY(VariantMapList Plugins READ plugins)
    Q_PROPERTY(VariantMapList PluginFailures READ pluginFailures)

public:
    static constexpr const char *serviceName = "solwind.Daemon";
    static constexpr const char *objectPath = "/solwind";

    DaemonApi(const Paths &paths, Settings &settings, const I18n &i18n, bool settingsLoaded,
              ClientHints *hints = nullptr, QObject *parent = nullptr);
    QString version() const;
    QString language() const;
    QString savedLanguage() const;
    bool notifyEnabled() const;
    bool notifyProfile() const;
    bool trayEnabled() const;
    QString mode() const { return controlMode ? QStringLiteral("control") : QStringLiteral("observe"); }
    VariantMapList plugins() const;
    VariantMapList pluginFailures() const;
    const PluginLoader *loader = nullptr;   // (main)
    // More fields for Status() (the reading pace), filled in by main
    std::function<QVariantMap()> extraStatus;
    bool controlMode = false;             // false: observe mode, nothing is written

public slots:
    // { version, pid, settingsPath, settingsLoaded (the file existed and was valid JSON), sysRoot ("" = real /sys),
    //   mode: "control" | "observe" (reads only, never writes to fans, lights or files),
    //   nbfcPollMs, sensorsActive, sensorsFast (how often it reads now) }
    Q_SCRIPTABLE QVariantMap Status() const;
    // What the calling client shows: "window" (a window is open) or "fans" (the fan page is open). The daemon reads
    // as often as that needs (SPEC.md P-2); a client that goes away is forgotten by itself.
    Q_SCRIPTABLE void Watch(const QString &topic);
    Q_SCRIPTABLE void Unwatch(const QString &topic);
    // Notifications (settings notify.<key>): "enabled" (all of them) or "profile" (a profile changed from a terminal)
    Q_SCRIPTABLE void SetNotify(const QString &key, bool on);
    // The UI language: "tr", "en", or "" (follow the system); notifications and messages use it
    Q_SCRIPTABLE void SetLanguage(const QString &lang);
    // UI options (settings ui.<key>): "tray" (the UI shows a tray icon)
    Q_SCRIPTABLE void SetUi(const QString &key, bool on);
    // Quits cleanly (pending settings written first); the reply is sent before the daemon goes away
    Q_SCRIPTABLE void Quit();

signals:
    void quitRequested();

private:
    const Paths &m_paths;
    Settings &m_settings;
    const I18n &m_i18n;
    bool m_settingsLoaded;
    ClientHints *m_hints;
};
