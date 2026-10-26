#include "Paths.h"

#include <QDir>

Paths Paths::fromEnvironment()
{
    Paths p;
    // An empty variable counts as unset (Settings.qml: env("XDG_CONFIG_HOME") || HOME + "/.config")
    const QString xdg = qEnvironmentVariable("XDG_CONFIG_HOME");
    p.settingsDir = (xdg.isEmpty() ? QDir::homePath() + QStringLiteral("/.config") : xdg) + QStringLiteral("/solwind");
    const QString fake = qEnvironmentVariable("SOLWIND_SYS");
    p.sysRoot = fake.isEmpty() ? QString() : fake;
    p.nbfcRoot = qEnvironmentVariable("SOLWIND_NBFC_ROOT");
    p.udevRoot = qEnvironmentVariable("SOLWIND_UDEV_ROOT");
    const QString app = qEnvironmentVariable("SOLWIND_APP_DIR");
    p.appDir = app.isEmpty() ? QStringLiteral(SOLWIND_SOURCE_APP_DIR) : app;
    p.home = qEnvironmentVariable("HOME");      // (Colours.qml: Quickshell.env("HOME"))
    return p;
}
