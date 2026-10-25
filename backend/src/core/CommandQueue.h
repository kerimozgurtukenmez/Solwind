#pragma once

#include "Cmd.h"

#include <QList>
#include <QObject>
#include <QString>

// Commands run one after another on one Cmd (SPEC.md Q-1, Q-2). Each entry has a key: a new command removes the
// waiting ones with the same key and goes to the end (a newer speed for a fan replaces one that hasn't run yet).
// The key "" is never replaced. The command that is already running is never affected.
class CommandQueue : public QObject
{
    Q_OBJECT
public:
    explicit CommandQueue(int timeoutMs, QObject *parent = nullptr);

    void enqueue(const QString &key, const QStringList &cmd, Cmd::Done done = nullptr, bool merged = false);
    bool busy() const { return m_runner.busy(); }
    qsizetype waiting() const { return m_queue.size(); }

private:
    void pump();

    struct Entry {
        QString key;
        QStringList cmd;
        Cmd::Done done;
        bool merged;
    };
    Cmd m_runner;
    QList<Entry> m_queue;
};
