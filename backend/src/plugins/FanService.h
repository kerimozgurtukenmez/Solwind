#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <functional>
#include <optional>

// What a fan plugin may offer beyond the fans (SDK): a service it runs on (NBFC: its systemd service, read-only mode,
// start at boot, laptop configs). The fan controls (FanPlugin) work without it; solwindd's Fans D-Bus object shows
// these parts only when the active fan plugin has them (FanPlugin::service()). Admin actions ask for the password
// themselves (pkexec) and report through message().
class FanService
{
public:
    struct CurvePoint { int t = 0; int s = 0; };
    // A config's own fan curve for one fan, as the curve editor shows it
    struct Curve { QList<CurvePoint> points; int hyst = 3; };

    virtual ~FanService() = default;

    // ---------- the service ----------
    virtual bool bootEnabled() const = 0;
    virtual QString message() const = 0;          // what an action waits for, or why it failed
    virtual bool messageError() const = 0;
    virtual bool adminBusy() const = 0;           // an action waits for the password dialog or runs
    virtual void startService() = 0;
    virtual void stopService() = 0;
    virtual void restartService(bool readOnly) = 0;
    virtual void setReadOnly(bool on) = 0;
    virtual void setBoot(bool on) = 0;

    // ---------- configs ----------
    virtual QString fileConfigId() const = 0;     // the one selected (it may be used only after a restart)
    virtual bool noConfig() const = 0;            // none selected yet
    virtual bool configPending() const = 0;       // selected, not running yet
    virtual QStringList configs() const = 0;
    // Where the list came from: { dirs, fromDirs, fromCli, cliCode, cliLine } (Fans.ConfigList on D-Bus)
    virtual QVariantMap configList() const = 0;
    virtual QList<std::optional<Curve>> configCurves() const = 0;   // per fan; nullopt: the config has none
    virtual QStringList recommended() const = 0;
    virtual void recommend(std::function<void(bool ok, const QStringList &found)> done) = 0;
    virtual void applyConfig(const QString &name) = 0;
};
