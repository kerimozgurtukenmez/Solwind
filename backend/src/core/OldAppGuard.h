#pragma once

#include "Cmd.h"

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

// Solwind 0.5.x and older is a QML app that drives the fans itself, and two programs must never drive them at once
// (ARCH-PLAN rule 6). In control mode the daemon doesn't start while such an app runs (exit 3), and steps aside (fans
// back to automatic, quit with 3) when one starts later: an old copy started by hand, a stale install in a home
// folder, an old checkout. (0.5.x's own launcher only looks for other window apps, so it can't see solwindd.) The
// running Quickshell instances come from `qs list --all --json` (as the solwind command, SPEC.md CLI-8); one is the old
// app if its folder is named solwind / ccenter (any case), or its shell.qml starts with `//@ pragma AppId solwind`
// (older versions: ccenter) wherever it lives - unless its first lines have the line `// solwindd-client: ...` and its
// services/Nbfc.qml imports Solwind (the UI as solwindd's D-Bus client: it drives nothing itself). A new Quickshell
// instance creates entries in $XDG_RUNTIME_DIR/quickshell/by-pid and by-id (measured), which are watched; checked every
// 60 s as well (every 30 s while neither folder exists yet). If it can't be told at start, the daemon doesn't start; if
// it can't be told later (qs list failing three times in a row), that is only reported: fan control doesn't stop by
// itself because a tool failed (since 1.0; before, it stepped aside).
class OldAppGuard : public QObject
{
    Q_OBJECT
public:
    explicit OldAppGuard(QObject *parent = nullptr);

    struct Result {
        bool ok = true;           // false: couldn't tell (qs list failed / gave no list)
        QString path;             // the QML app's config path, "" if none runs
        QString error;
    };
    // At start. No qs at all: nothing can run, ok with no path
    static Result findNow(int timeoutMs = 5000);
    static Result match(const QByteArray &qsListJson);   // the first QML app in qs list's output
    static bool isQmlApp(const QString &configPath);
    void start();

signals:
    void found(const QString &configPath);
    void checkFailed(const QString &why);                // 3 checks in a row couldn't tell (reported, nothing more)

private:
    void check();
    void watch();

    QFileSystemWatcher m_watcher;
    QTimer m_debounce, m_fallback, m_retry, m_periodic;
    Cmd m_cmd;
    QString m_runtime;
    int m_failures = 0;
};
