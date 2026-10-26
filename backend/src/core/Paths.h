#pragma once

#include <QString>
#include <QStringList>

// Where things are, from the environment (read once at start-up)
struct Paths {
    QString settingsDir;   // $XDG_CONFIG_HOME/solwind, or ~/.config/solwind (SPEC.md SE-1)
    QString sysRoot;       // "" = the real /sys; SOLWIND_SYS=<folder> = a fake tree for tests (SPEC.md K-1)
    QString nbfcRoot;      // "" = the real NBFC files; SOLWIND_NBFC_ROOT=<folder> = a fake tree for tests (only read)
    QString udevRoot;      // "" = the real /etc/udev and /usr/lib/udev; SOLWIND_UDEV_ROOT=<folder> in tests (only read)
    // The app's own files the keyboard permission uses (dist/90-solwind.rules, scripts/kbd-rule-check.sh):
    // SOLWIND_APP_DIR, else the source tree solwindd was built from (until phase 6 installs it next to them)
    QString appDir;
    QString home;          // $HOME: Caelestia's colour scheme is ~/.local/state/caelestia/scheme.json (Colours.qml)

    static Paths fromEnvironment();
    // sys("/class/leds") -> /sys/class/leds, or <root>/sys/class/leds (as scripts/kbdinfo.sh does)
    QString sys(const QString &path) const { return sysRoot + QStringLiteral("/sys") + path; }
    // nbfc("/etc/nbfc/nbfc.json"): an NBFC file, under nbfcRoot in tests
    QString nbfc(const QString &path) const { return nbfcRoot + path; }
    // udev("/etc/udev/rules.d/90-solwind.rules"): a udev rule file, under udevRoot in tests
    QString udev(const QString &path) const { return udevRoot + path; }
    QString caelestiaScheme() const { return home + QStringLiteral("/.local/state/caelestia/scheme.json"); }
    // The folders NBFC 0.4.1 loads configs from, in its order (src/nbfc.h: the mutable /var/lib one, then DATADIR
    // for /usr and /usr/local builds; SPEC.md A-10)
    QStringList nbfcConfigDirs() const
    {
        return { nbfc(QStringLiteral("/var/lib/nbfc/configs")), nbfc(QStringLiteral("/usr/share/nbfc/configs")),
                 nbfc(QStringLiteral("/usr/local/share/nbfc/configs")) };
    }
};
