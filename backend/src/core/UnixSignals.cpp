#include "UnixSignals.h"

#include <QSocketNotifier>

#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

int UnixSignals::s_fds[2] = { -1, -1 };

UnixSignals::UnixSignals(QObject *parent) : QObject(parent)
{
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, s_fds) != 0) {
        qWarning("solwindd: socketpair failed, signals will end the process at once");
        return;
    }
    ::fcntl(s_fds[0], F_SETFL, O_NONBLOCK);
    ::fcntl(s_fds[1], F_SETFL, O_NONBLOCK);
    m_notifier = new QSocketNotifier(s_fds[1], QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, [this] {
        unsigned char s;
        while (::read(s_fds[1], &s, 1) == 1) emit received(s);
    });

    struct sigaction sa = {};
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    for (int s : { SIGINT, SIGTERM, SIGHUP }) ::sigaction(s, &sa, nullptr);
    ::signal(SIGPIPE, SIG_IGN);
}

UnixSignals::~UnixSignals()
{
    for (int s : { SIGINT, SIGTERM, SIGHUP }) ::signal(s, SIG_DFL);
    for (int &fd : s_fds) if (fd >= 0) { ::close(fd); fd = -1; }
}

void UnixSignals::handler(int signo)
{
    const int saved = errno;
    const unsigned char s = static_cast<unsigned char>(signo);
    [[maybe_unused]] const ssize_t n = ::write(s_fds[0], &s, 1);   // async-signal-safe
    errno = saved;
}
