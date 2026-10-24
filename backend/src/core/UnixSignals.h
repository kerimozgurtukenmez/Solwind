#pragma once

#include <QObject>

class QSocketNotifier;

// SIGINT, SIGTERM and SIGHUP become a Qt signal in the event loop, so the daemon can quit cleanly (settings written,
// later: fans handed back) instead of dying where it stands. SIGPIPE is ignored.
// The handler only writes the signal number to a socket; everything else happens in the event loop.
class UnixSignals : public QObject
{
    Q_OBJECT
public:
    explicit UnixSignals(QObject *parent = nullptr);
    ~UnixSignals() override;

signals:
    void received(int signo);

private:
    static void handler(int signo);
    static int s_fds[2];
    QSocketNotifier *m_notifier = nullptr;
};
