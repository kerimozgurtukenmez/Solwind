#include "KbdIo.h"

#include "KeyboardParse.h"
#include "KeyboardScan.h"

#include <QFile>
#include <QRegularExpression>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

void KbdIo::scan(quint64 tag)
{
    QString text = KeyboardScan::scan(m_paths);
    // Lights outside /sys, in the same lines, only if no plugin takes a light of /sys
    if (!KeyboardParse::parse(text, QString(), m_plugins).plugin) {
        static const QRegularExpression notInALine(u"[|\\r\\n]"_s);
        auto clean = [](QString s) { return s.replace(notInALine, u" "_s); };
        for (const KeyboardPlugin *p : m_plugins) {
            for (const KbdLight &l : p->externalLights()) {
                const QString name = clean(l.name);
                text += u"\nled|%1|%2|%3"_s.arg(name, clean(l.driver), clean(l.path));
                for (auto it = l.v.cbegin(); it != l.v.cend(); ++it) text += u"\nv|%1|%2|%3"_s.arg(name, it.key(), clean(it.value()));
                for (const QString &f : l.w) text += u"\nw|%1|%2"_s.arg(name, f);
            }
        }
    }
    emit scanned(tag, text);
}

void KbdIo::write(const QStringList &pv, quint64 tag)
{
    for (qsizetype i = 0; i + 1 < pv.size(); i += 2) {
        const auto owner = std::find_if(m_plugins.cbegin(), m_plugins.cend(), [&](const KeyboardPlugin *p) { return p->ownsPath(pv[i]); });
        if (owner != m_plugins.cend()) {
            if (const QString why = (*owner)->writeExternal(pv[i], pv[i + 1]); !why.isEmpty()) {
                emit written(tag, pv[i] + u": "_s + why);
                return;
            }
            continue;
        }
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
