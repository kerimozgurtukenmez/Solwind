#pragma once

#include "Cmd.h"

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

// TRANSITION ONLY (phases 3-4 of ARCH-PLAN.md, removed when the QML app becomes a client in phase 5): the QML app
// still drives the fans itself, and two programs must never drive them at once (ARCH-PLAN rule 6). In control mode
// the daemon doesn't start while a QML Solwind / Ccenter runs, and steps aside (fans back to automatic, quit) when one
// starts. The running Quickshell instances come from `qs list --all --json` (as the solwind command, SPEC.md CLI-8);
// one is the QML app if its folder is named solwind / ccenter (any case), or its shell.qml starts with
// `//@ pragma AppId solwind` (older versions: ccenter) wherever it lives - unless its first lines have the line
// `// solwindd-client: ...` and its services/Nbfc.qml imports Solwind (the UI as solwindd's D-Bus client, phase 5:
// it drives nothing itself). A new Quickshell instance creates entries in
// $XDG_RUNTIME_DIR/quickshell/by-pid and by-id (measured), which are watched; checked every 60 s as well (every 30 s
// while neither folder exists yet). When it can't be told (qs list fails), it fails safe: no start, or step aside.
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
    void checkFailed(const QString &why);                // 3 checks in a row couldn't tell

private:
    void check();
    void watch();

    QFileSystemWatcher m_watcher;
    QTimer m_debounce, m_fallback, m_retry, m_periodic;
    Cmd m_cmd;
    QString m_runtime;
    int m_failures = 0;
};
