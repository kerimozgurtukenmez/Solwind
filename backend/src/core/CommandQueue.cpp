#include "CommandQueue.h"

#include <QTimer>

CommandQueue::CommandQueue(int timeoutMs, QObject *parent) : QObject(parent)
{
    m_runner.setTimeout(timeoutMs);
}

void CommandQueue::enqueue(const QString &key, const QStringList &cmd, Cmd::Done done, bool merged)
{
    if (!key.isEmpty()) m_queue.removeIf([&](const Entry &e) { return e.key == key; });
    m_queue.append({ key, cmd, std::move(done), merged });
    pump();
}

void CommandQueue::pump()
{
    if (m_runner.busy() || m_queue.isEmpty()) return;
    Entry e = m_queue.takeFirst();
    m_runner.setMergedChannels(e.merged);
    m_runner.go(e.cmd, [this, done = std::move(e.done)](int code, const QByteArray &out, const QByteArray &err) {
        if (done) done(code, out, err);
        // Qt.callLater(pump): the next one starts from the event loop, not inside this callback
        QTimer::singleShot(0, this, &CommandQueue::pump);
    });
}
