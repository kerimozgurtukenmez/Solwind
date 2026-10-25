#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>

#include <functional>

// Runs one command at a time without blocking: go({"nbfc", "status", "-a"}, [](int code, out, err) { ... }).
// SPEC.md X-1..X-5 (utils/Cmd.qml): the callback runs exactly once - also when the command can't be started
// (code 127, err "failed to start: <program>") or runs longer than the timeout (SIGTERM, SIGKILL 2 s later if it
// is still there, code 124). A program killed by a signal never reads as success. Standard input is /dev/null.
// Not while busy (the call gets 125 at once). A Cmd that is destroyed kills its program and runs no callback any more
// (shutdown): the callback must not delete the Cmd either.
class Cmd : public QObject
{
    Q_OBJECT
public:
    using Done = std::function<void(int code, const QByteArray &out, const QByteArray &err)>;

    explicit Cmd(QObject *parent = nullptr);
    ~Cmd() override;

    void setTimeout(int ms) { m_timeout = ms; }           // 0 = no limit
    void setKillDelay(int ms) { m_killer.setInterval(ms); }
    void setMergedChannels(bool on) { m_merged = on; }     // stderr into stdout, like `2>&1`
    bool busy() const { return m_busy; }

    // Starts cmd (cmd[0] = program, looked up in PATH). Must not be called while busy.
    void go(const QStringList &cmd, Done done);

private:
    void end(int code, const QByteArray &out, const QByteArray &err);

    QProcess *m_proc = nullptr;
    QTimer m_guard, m_killer;
    Done m_done;
    int m_timeout = 0;
    bool m_merged = false;
    bool m_busy = false;
    bool m_timedOut = false;
};
