#include "KbdIo.h"

#include "KeyboardScan.h"

#include <QFile>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

void KbdIo::scan(quint64 tag)
{
    emit scanned(tag, KeyboardScan::scan(m_paths));
}

void KbdIo::write(const QStringList &pv, quint64 tag)
{
    for (qsizetype i = 0; i + 1 < pv.size(); i += 2) {
        const QByteArray path = QFile::encodeName(pv[i]);
        const QByteArray value = pv[i + 1].toUtf8() + '\n';
        // (a signal can interrupt a call: this process also runs programs, unlike the QML's awk; tried again then)
        int fd;
        do fd = ::open(path.constData(), O_WRONLY | O_TRUNC | O_CLOEXEC);
        while (fd < 0 && errno == EINTR);
        int err = 0;
        if (fd < 0) err = errno;
        else {
            ssize_t n;
            do n = ::write(fd, value.constData(), size_t(value.size()));
            while (n < 0 && errno == EINTR);
            if (n < 0) err = errno;
            else if (n != value.size()) err = EIO;            // (a short write sets no errno)
            if (::close(fd) != 0 && !err && errno != EINTR) err = errno;   // (Linux closes it even on EINTR)
        }
        if (err) {
            emit written(tag, pv[i] + u": "_s + QString::fromLocal8Bit(std::strerror(err)));
            return;
        }
    }
    emit written(tag, QString());
}

void KbdIo::read(const QStringList &files, quint64 tag)
{
    QStringList out;
    QList<bool> ok;
    for (const QString &f : files) {
        QFile file(f);
        char buf[4096];
        const qint64 n = file.open(QIODevice::ReadOnly | QIODevice::Unbuffered) ? file.read(buf, sizeof buf) : -1;
        out << (n > 0 ? QString::fromUtf8(buf, n) : QString());
        ok << (n >= 0);
    }
    emit readDone(tag, out, ok);
}
