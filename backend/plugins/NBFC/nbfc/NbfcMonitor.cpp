#include "NbfcMonitor.h"

#include "core/JsCompat.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>

#include <cmath>

using namespace Qt::StringLiterals;

NbfcMonitor::NbfcMonitor(const Paths &paths, QObject *parent)
    : QObject(parent), m_paths(paths), m_nbfcJson(paths.nbfc(u"/etc/nbfc/nbfc.json"_s)), m_queue(15000)
{
    m_status.setTimeout(10000);
    m_timer.setInterval(5000);
    connect(&m_timer, &QTimer::timeout, this, &NbfcMonitor::poll);
    // nbfc.json may be rewritten in steps (empty for a moment): read it once things settle
    m_jsonDebounce.setSingleShot(true);
    m_jsonDebounce.setInterval(50);
    connect(&m_jsonDebounce, &QTimer::timeout, this, [this] { readNbfcJson(); watchNbfcJson(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_jsonDebounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_jsonDebounce.start(); });
    connect(this, &NbfcMonitor::configIdChanged, this, [this] { mergeConfigs(); loadConfigCurves(); });
}

void NbfcMonitor::start()
{
    watchNbfcJson();
    readNbfcJson();
    loadConfigs();
    refreshBoot();
    m_timer.start();
    poll();                                           // triggeredOnStart
}

void NbfcMonitor::setFast(bool on)
{
    const int ms = on ? 2000 : 5000;
    if (m_timer.interval() != ms) m_timer.setInterval(ms);
}

// ---------- status (N-9..N-12, L-1..L-3) ----------

void NbfcMonitor::poll()
{
    if (m_status.busy()) return;
    const quint64 seq = ++m_pollSeq;
    m_status.go({ u"nbfc"_s, u"status"_s, u"-a"_s }, [this, seq](int code, const QByteArray &out, const QByteArray &err) {
        onStatus(seq, code, out, err);
    });
}

namespace {
bool same(double a, double b) { return a == b || (std::isnan(a) && std::isnan(b)); }
bool sameFans(const QList<Nbfc::FanStatus> &a, const QList<Nbfc::FanStatus> &b)
{
    if (a.size() != b.size()) return false;
    for (qsizetype i = 0; i < a.size(); ++i)
        if (a[i].name != b[i].name || !same(a[i].temp, b[i].temp) || a[i].autoControl != b[i].autoControl
            || a[i].critical != b[i].critical || !same(a[i].current, b[i].current) || !same(a[i].target, b[i].target))
            return false;
    return true;
}
} // namespace

void NbfcMonitor::onStatus(quint64 seq, int code, const QByteArray &out, const QByteArray &err)
{
    const bool wasRunning = m_running, wasReadOnly = m_readOnly;
    const double wasTemp = m_temp;
    const QList<Nbfc::FanStatus> wasFans = m_fans;
    if (!m_logged) {
        m_logged = true;
        qInfo("[solwind] nbfc status -a (exit %d):\n%s%s", code, out.constData(), err.constData());
    }
    m_statusSeq = seq;
    const bool was = m_running;
    m_running = code == 0;
    // A running service went away: not reported while an admin action (restart, config) explains it or while
    // quitting, but afterwards if it didn't come back and the user didn't stop it
    if (was && !m_running) m_lostPending = true;
    if (m_running) m_lostPending = false;
    if (m_lostPending && !m_userStopped && !(lostHeld && lostHeld())) {
        m_lostPending = false;
        emit serviceLost();
    }
    // Two failed reads in a row: NBFC is really stopped, so the config selected in the file is the one again (one
    // could be a timeout while NBFC still runs the old config)
    m_failedPolls = code == 0 ? 0 : m_failedPolls + 1;
    if (m_failedPolls >= 2 && !m_serviceConfigId.isEmpty()) {
        m_serviceConfigId.clear();
        updateConfigId();
    }
    if (code == 0) {
        if (!was) m_userStopped = false;              // only a service that came back clears "stopped by the user"
        const Nbfc::Status st = Nbfc::parseStatus(QString::fromUtf8(out));
        // The config first: a switch must happen before these fans' settings are looked at
        if (!st.config.isEmpty()) {
            m_serviceConfigId = st.config;
            updateConfigId();
        }
        if (st.readOnly) m_readOnly = *st.readOnly;
        m_temp = st.temp;
        m_fans = st.fans;
    } else {
        m_temp = Nbfc::NaN;
    }
    // /etc/nbfc (or nbfc.json) may have appeared since the last look: watch it then
    const QString dir = QFileInfo(m_nbfcJson).absolutePath();
    if ((!m_watcher.directories().contains(dir) && QFileInfo(dir).isDir())
        || (!m_watcher.files().contains(m_nbfcJson) && QFileInfo::exists(m_nbfcJson)))
        m_jsonDebounce.start();
    if (m_running != wasRunning || m_readOnly != wasReadOnly || !same(m_temp, wasTemp) || !sameFans(m_fans, wasFans))
        emit changed();
    emit statusRead(code == 0);
}

void NbfcMonitor::setReadOnlyWanted(bool on)
{
    if (m_readOnly == on) return;
    m_readOnly = on;
    emit changed();
}

void NbfcMonitor::updateConfigId()
{
    const QString id = !m_serviceConfigId.isEmpty() ? m_serviceConfigId : m_fileConfigId;
    if (id == m_configId) return;
    m_configId = id;
    emit configIdChanged();
    emit changed();
}

// ---------- /etc/nbfc/nbfc.json (N-13) ----------

void NbfcMonitor::watchNbfcJson()
{
    // The folder (the file may be created, deleted or replaced), and the file itself - watched again every time:
    // after a replace (rename, delete + create) the old watch still follows the old, gone file
    const QString dir = QFileInfo(m_nbfcJson).absolutePath();
    if (QFileInfo(dir).isDir() && !m_watcher.directories().contains(dir)) m_watcher.addPath(dir);
    if (m_watcher.files().contains(m_nbfcJson)) m_watcher.removePath(m_nbfcJson);
    if (QFileInfo::exists(m_nbfcJson)) m_watcher.addPath(m_nbfcJson);
}

void NbfcMonitor::readNbfcJson()
{
    QFile f(m_nbfcJson);
    if (!f.open(QIODevice::ReadOnly)) {
        // Missing (no config was ever selected): the id stays as it was ("" on a fresh install)
        if (!m_configRead) { m_configRead = true; emit changed(); }
        else m_configRead = true;
        return;
    }
    // A half-written file (empty while NBFC rewrites it) keeps the current id instead of switching configs
    QString why;
    const auto id = Nbfc::selectedConfigId(f.read(1 << 20), &why);
    if (!id) {
        qInfo("[solwind] %s could not be read: %s", qPrintable(m_nbfcJson), qPrintable(why));
    } else if (*id != m_fileConfigId) {
        m_fileConfigId = *id;
        mergeConfigs();                                // onFileConfigIdChanged
    }
    m_configRead = true;
    // While NBFC runs, the config it reports stays the one until it reports another; once it is stopped the file's
    updateConfigId();
    emit changed();
}

// ---------- config list (A-10) ----------

void NbfcMonitor::loadConfigs()
{
    // stdout + stderr together: some versions print the list to stderr or in another format
    m_queue.enqueue(u"cfg:list"_s, { u"nbfc"_s, u"config"_s, u"-l"_s }, [this](int code, const QByteArray &out, const QByteArray &) {
        const QString o = QString::fromUtf8(out);
        const QStringList ls = Nbfc::parseConfigList(o);
        qInfo("[solwind] nbfc config -l (exit %d): %d configs available; selected: %s", code, int(ls.size()),
              qPrintable(m_configId.isEmpty() ? u"-"_s : m_configId));
        m_cfgFromCli = code == 0 ? ls : QStringList();
        m_cliCode = code;
        m_cliLine = code == 0 ? QString() : Nbfc::firstLine(o);
        mergeConfigs();
    }, true);
    listConfigDirs();
}

void NbfcMonitor::listConfigDirs()
{
    // find "$d" -maxdepth 1 -name "*.json" for each folder that exists: any entry type, hidden ones too, case-sensitive
    QStringList names, dirs;
    for (const QString &d : m_paths.nbfcConfigDirs()) {
        if (!QFileInfo(d).isDir()) continue;
        const QStringList found = QDir(d).entryList({ u"*.json"_s },
            QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot | QDir::CaseSensitive, QDir::Unsorted);
        for (const QString &f : found) {
            if (!dirs.contains(d)) dirs << d;
            names << f.chopped(5);
        }
    }
    m_cfgFromDir = names;
    m_cfgDirs = dirs;
    mergeConfigs();
}

void NbfcMonitor::mergeConfigs()
{
    const QStringList c = Nbfc::mergeConfigs(m_cfgFromDir, m_cfgFromCli, m_configId, m_fileConfigId);
    if (c == m_configs) return;
    m_configs = c;
    emit changed();
}

// ---------- the selected config's curves (A-12) ----------

void NbfcMonitor::loadConfigCurves()
{
    QList<std::optional<Nbfc::ConfigCurve>> curves;
    const QString id = m_configId;
    if (!id.isEmpty()) {
        // The same lookup as NBFC (ModelConfig_FindAndLoad): the id as a path (NBFC runs in "/"), then each folder
        // with and without ".json" - so the preview shows the file NBFC really uses. The file is only read.
        QStringList candidates{ id.startsWith(u'/') ? m_paths.nbfc(id) : m_paths.nbfc(u"/"_s + id) };
        for (const QString &d : m_paths.nbfcConfigDirs()) candidates << d + u'/' + id << d + u'/' + id + u".json"_s;
        for (const QString &c : candidates) {
            if (!QFileInfo(c).isFile()) continue;
            QFile f(c);
            if (f.open(QIODevice::ReadOnly)) curves = Nbfc::configCurves(f.read(16 << 20));
            break;                                     // the first existing file is the one (a read error: none)
        }
    }
    if (curves == m_curves) return;
    m_curves = curves;
    emit changed();
}

// ---------- start on boot (A-8), recommendations (A-11) ----------

void NbfcMonitor::refreshBoot()
{
    m_queue.enqueue(u"boot"_s, { u"systemctl"_s, u"is-enabled"_s, u"nbfc_service"_s },
                    [this](int, const QByteArray &out, const QByteArray &) {
        const bool on = js::trim(QString::fromUtf8(out)) == u"enabled"_s;
        if (on == m_bootEnabled) return;
        m_bootEnabled = on;
        emit changed();
    });
}

void NbfcMonitor::recommend(std::function<void(bool, const QStringList &)> done)
{
    // Asked again while one waits: it gets the same answer (a replaced queue entry would never answer)
    m_recWaiters.append(std::move(done));
    if (m_recWaiters.size() > 1) return;
    m_queue.enqueue(u"cfg:rec"_s, { u"nbfc"_s, u"config"_s, u"-r"_s }, [this](int code, const QByteArray &out, const QByteArray &) {
        const bool ok = code == 0;
        if (ok) {
            m_recommended = Nbfc::matchRecommendations(QString::fromUtf8(out), m_configs);
            emit changed();
        }
        const auto waiters = std::exchange(m_recWaiters, {});
        for (const auto &w : waiters) if (w) w(ok, m_recommended);
    });
}
