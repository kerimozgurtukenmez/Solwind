#include "HandBack.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>

#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace {
// Yours (or root's) and nobody else can change it, never a link: what is in it is run
bool safe(const QString &path, bool dir)
{
    struct stat st{};
    if (::lstat(QFile::encodeName(path).constData(), &st) != 0) return false;
    if (dir ? !S_ISDIR(st.st_mode) : !S_ISREG(st.st_mode)) return false;
    return st.st_uid == ::getuid() && !(st.st_mode & (S_IWGRP | S_IWOTH));
}
}

QString HandBack::path()
{
    const QString own = qEnvironmentVariable("SOLWIND_RUNTIME_DIR");
    if (own.startsWith(u'/')) return own + u"/fan-hand-back"_s;
    const QString run = qEnvironmentVariable("XDG_RUNTIME_DIR");
    return run.startsWith(u'/') ? run + u"/solwind/fan-hand-back"_s : QString();
}

bool HandBack::record(const QStringList &command, QString *error)
{
    const QString file = path();
    auto fail = [&](const QString &why) { if (error) *error = why; return false; };
    if (file.isEmpty()) return fail(u"XDG_RUNTIME_DIR isn't set"_s);
    if (command.isEmpty()) {
        if (QFile::exists(file) && !QFile::remove(file)) return fail(u"can't remove %1"_s.arg(file));
        return true;
    }
    for (const QString &a : command)
        if (a.contains(u'\n') || a.contains(QChar(0))) return fail(u"an argument with a line break"_s);
    const QString dir = QFileInfo(file).path();
    if (!QDir().mkpath(dir) || !QFile::setPermissions(dir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner))
        return fail(u"can't make %1"_s.arg(dir));
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly)) return fail(f.errorString());
    f.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    f.write((command.join(u'\n') + u'\n').toUtf8());
    if (!f.commit()) return fail(f.errorString());
    return true;
}

int HandBack::run()
{
    if (qEnvironmentVariable("EXIT_STATUS") == u"3"_s) return 0;   // it didn't start: whatever drives the fans keeps them
    const QString file = path();
    if (file.isEmpty() || !QFileInfo::exists(file)) {
        std::fprintf(stderr, "solwindd: no fan hand-back recorded: nothing to do\n");
        return 0;
    }
    if (!safe(file, false) || !safe(QFileInfo(file).path(), true)) {
        std::fprintf(stderr, "solwindd: %s could have been changed by someone else: not run\n", qPrintable(file));
        return 1;
    }
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return 1;
    QStringList command = QString::fromUtf8(f.readAll()).split(u'\n');
    if (!command.isEmpty() && command.last().isEmpty()) command.removeLast();
    if (command.isEmpty() || command[0].isEmpty()) return 1;
    // systemd's ExecStopPost may have a short PATH: the usual folders too
    QString exe = QStandardPaths::findExecutable(command[0]);
    if (exe.isEmpty()) exe = QStandardPaths::findExecutable(command[0], { u"/usr/local/bin"_s, u"/usr/bin"_s });
    if (exe.isEmpty()) {
        std::fprintf(stderr, "solwindd: %s not found: fans not handed back\n", qPrintable(command[0]));
        return 1;
    }
    QProcess p;
    p.setProcessChannelMode(QProcess::ForwardedChannels);
    p.start(exe, command.mid(1));
    if (!p.waitForFinished(15000)) {
        p.kill();
        p.waitForFinished(2000);
        std::fprintf(stderr, "solwindd: %s didn't finish in 15 s\n", qPrintable(command.join(u' ')));
        return 1;
    }
    const bool ok = p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
    std::fprintf(stderr, "solwindd: fans handed back (%s): %s\n", qPrintable(command.join(u' ')), ok ? "ok" : "failed");
    return ok ? 0 : 1;
}
