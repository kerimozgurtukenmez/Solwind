#pragma once

#include <QString>

// Where things are, from the environment (read once at start-up)
struct Paths {
    QString settingsDir;   // $XDG_CONFIG_HOME/solwind, or ~/.config/solwind (SPEC.md SE-1)
    QString sysRoot;       // "" = the real /sys; SOLWIND_SYS=<folder> = a fake tree for tests (SPEC.md K-1)

    static Paths fromEnvironment();
    // sys("/class/leds") -> /sys/class/leds, or <root>/sys/class/leds (as scripts/kbdinfo.sh does)
    QString sys(const QString &path) const { return sysRoot + QStringLiteral("/sys") + path; }
};
