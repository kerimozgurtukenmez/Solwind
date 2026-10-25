#include "Cmd.h"

Cmd::Cmd(QObject *parent) : QObject(parent)
{
    m_guard.setSingleShot(true);
    m_killer.setSingleShot(true);
    m_killer.setInterval(2000);
    connect(&m_guard, &QTimer::timeout, this, [this] {
        if (!m_busy || !m_proc) return;
        m_timedOut = true;
        m_proc->terminate();                 // SIGTERM
        m_killer.start();
    });
    connect(&m_killer, &QTimer::timeout, this, [this] {
        if (m_busy && m_proc && m_proc->state() != QProcess::NotRunning) m_proc->kill();   // SIGKILL
    });
}

Cmd::~Cmd()
{
    // Quitting while a command runs: don't leave it behind (and the callback is not run any more)
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->disconnect(this);
        m_proc->kill();
        m_proc->waitForFinished(1000);
    }
}

void Cmd::go(const QStringList &cmd, Done done)
{
    if (m_busy) {                             // a caller bug: answer it, never drop the running one's callback
        qWarning("solwindd: Cmd::go while busy: %s", qPrintable(cmd.join(u' ')));
        if (done) done(125, QByteArray(), "busy");
        return;
    }
    m_done = std::move(done);
    m_busy = true;
    m_timedOut = false;
    if (m_proc) m_proc->deleteLater();
    // A new QProcess per command: no output, state or signal of the previous one can reach this callback
    m_proc = new QProcess(this);
    QProcess *p = m_proc;
    p->setStandardInputFile(QProcess::nullDevice());
    if (m_merged) p->setProcessChannelMode(QProcess::MergedChannels);
    connect(p, &QProcess::errorOccurred, this, [this, p, cmd](QProcess::ProcessError e) {
        if (p != m_proc || e != QProcess::FailedToStart) return;
        end(127, QByteArray(), "failed to start: " + (cmd.isEmpty() ? QByteArray() : cmd.first().toUtf8()));
    });
    connect(p, &QProcess::finished, this, [this, p](int exitCode, QProcess::ExitStatus status) {
        if (p != m_proc) return;
        // Killed by a signal: Qt 6.12 gives the signal number (measured: SEGV 11, KILL 9), which Quickshell's Process
        // passed on too. Qt calls it undefined, so never let it read as success.
        const int code = m_timedOut ? 124 : status == QProcess::CrashExit && exitCode == 0 ? 255 : exitCode;
        end(code, p->readAllStandardOutput(), m_merged ? QByteArray() : p->readAllStandardError());
    });
    if (cmd.isEmpty()) { p->setProgram(QString()); p->start(); return; }   // fails to start -> 127
    p->start(cmd.first(), cmd.mid(1));
    if (m_timeout > 0 && m_busy) {
        m_guard.setInterval(m_timeout);
        m_guard.start();
    }
}

void Cmd::end(int code, const QByteArray &out, const QByteArray &err)
{
    if (!m_busy) return;
    Done cb = std::move(m_done);
    m_done = nullptr;
    m_busy = false;
    m_guard.stop();
    m_killer.stop();
    if (cb) cb(code, out, err);
}
