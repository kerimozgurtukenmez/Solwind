#include "OldAppGuard.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

OldAppGuard::OldAppGuard(QObject *parent) : QObject(parent)
{
    m_runtime = qEnvironmentVariable("XDG_RUNTIME_DIR") + u"/quickshell"_s;
    m_cmd.setTimeout(5000);
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(300);
    m_retry.setSingleShot(true);
    m_retry.setInterval(1000);
    m_fallback.setInterval(30000);
    m_periodic.setInterval(60000);
    connect(&m_debounce, &QTimer::timeout, this, &OldAppGuard::check);
    connect(&m_periodic, &QTimer::timeout, this, &OldAppGuard::check);
    connect(&m_retry, &QTimer::timeout, this, &OldAppGuard::check);
    connect(&m_fallback, &QTimer::timeout, this, [this] { watch(); check(); });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { watch(); m_debounce.start(); });
}

namespace {
QString head(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.read(4096)) : QString();
}
} // namespace

bool OldAppGuard::isQmlApp(const QString &configPath)
{
    const QString shell = head(configPath);
    // The UI that is solwindd's client (phase 5) drives nothing itself: its shell.qml says so in its first lines, and
    // its services are the D-Bus adapters (services/Nbfc.qml imports Solwind). Both must hold: a new shell.qml next to
    // the old services (a half-copied install) is still the QML app.
    static const QRegularExpression client(u"^// solwindd-client: \\S"_s, QRegularExpression::MultilineOption);
    static const QRegularExpression adapter(u"^import Solwind\\s*$"_s, QRegularExpression::MultilineOption);
    if (client.match(shell).hasMatch()
        && adapter.match(head(QFileInfo(configPath).absolutePath() + u"/services/Nbfc.qml"_s)).hasMatch())
        return false;
    // Either one is enough (the safe side: rather step aside once too often)
    const QString l = configPath.toLower();
    if (l.endsWith(u"/solwind/shell.qml"_s) || l.endsWith(u"/ccenter/shell.qml"_s)) return true;
    // The pragma is in the first lines (Quickshell reads `//@ pragma` lines at the top of shell.qml)
    static const QRegularExpression appId(u"^\\s*//@\\s*pragma\\s+AppId\\s+(solwind|ccenter)\\s*$"_s,
                                          QRegularExpression::MultilineOption);
    return appId.match(shell).hasMatch();
}

OldAppGuard::Result OldAppGuard::match(const QByteArray &json)
{
    QJsonParseError e;
    const QJsonDocument d = QJsonDocument::fromJson(json, &e);
    if (e.error != QJsonParseError::NoError || !d.isArray()) return { false, QString(), u"qs list gave no list"_s };
    for (const QJsonValue &v : d.array()) {
        const QString p = v.toObject().value(u"config_path"_s).toString();
        if (!p.isEmpty() && isQmlApp(p)) return { true, p, QString() };
    }
    return {};
}

OldAppGuard::Result OldAppGuard::findNow(int timeoutMs)
{
    if (QStandardPaths::findExecutable(u"qs"_s).isEmpty()) return {};
    QProcess p;
    p.setStandardInputFile(QProcess::nullDevice());
    p.start(u"qs"_s, { u"list"_s, u"--all"_s, u"--json"_s });
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished();
        return { false, QString(), u"qs list did not answer within %1 ms"_s.arg(timeoutMs) };
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        return { false, QString(), u"qs list failed (exit %1)"_s.arg(p.exitCode()) };
    return match(p.readAllStandardOutput());
}

void OldAppGuard::start()
{
    watch();
    m_periodic.start();
}

// by-pid gets a link per instance, by-id a folder with an id of its own per launch: a new instance shows in by-id even
// when its pid's link was left over from an instance that crashed (seen: stale by-pid links)
void OldAppGuard::watch()
{
    bool any = false;
    for (const QString &d : { m_runtime + u"/by-pid"_s, m_runtime + u"/by-id"_s }) {
        if (!QFileInfo(d).isDir()) continue;
        any = true;
        if (!m_watcher.directories().contains(d)) m_watcher.addPath(d);
    }
    if (any) m_fallback.stop();
    else if (!m_fallback.isActive()) m_fallback.start();
}

void OldAppGuard::check()
{
    if (m_cmd.busy()) { m_debounce.start(); return; }
    if (QStandardPaths::findExecutable(u"qs"_s).isEmpty()) return;
    m_cmd.go({ u"qs"_s, u"list"_s, u"--all"_s, u"--json"_s }, [this](int code, const QByteArray &out, const QByteArray &) {
        const Result r = code == 0 ? match(out) : Result{ false, QString(), u"qs list failed (exit %1)"_s.arg(code) };
        if (!r.ok) {
            // A Quickshell instance started and it can't be told which: again in a second, at most 3 times
            if (++m_failures < 3) m_retry.start();
            else { m_failures = 0; emit checkFailed(r.error); }
            return;
        }
        m_failures = 0;
        if (!r.path.isEmpty()) emit found(r.path);
    });
}
