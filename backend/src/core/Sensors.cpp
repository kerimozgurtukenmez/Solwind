#include "Sensors.h"

#include "JsCompat.h"

#include <QDateTime>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace {
constexpr double NaN = std::numeric_limits<double>::quiet_NaN();
bool same(double a, double b) { return a == b || (std::isnan(a) && std::isnan(b)); }

QRegularExpression jsRe(const QString &pattern, bool caseless = false)
{
    return QRegularExpression(pattern, caseless ? QRegularExpression::CaseInsensitiveOption : QRegularExpression::NoPatternOption);
}
} // namespace

bool Reading::operator==(const Reading &o) const
{
    return label == o.label && model == o.model && threads == o.threads && same(value, o.value) && note == o.note
        && same(usage, o.usage);
}

// ---------- names (Sensors.qml cleanCpu / cleanGpu / parseHw) ----------

// "13th Gen Intel(R) Core(TM) i7-13700H" -> "Intel Core i7-13700H"
QString SensorParser::cleanCpu(const QString &n)
{
    const QString S = QString::fromUtf16(js::reSpace), D = QString::fromUtf16(js::reDot);
    static const QRegularExpression marks = jsRe(u"\\((R|TM|tm|r)\\)"_s);
    static const QRegularExpression gen = jsRe(u"^\\d+(st|nd|rd|th) Gen"_s + S + u"+"_s, true);
    static const QRegularExpression word = jsRe(S + u"+(CPU|Processor)\\b"_s + D + u"*\\z"_s, true);
    static const QRegularExpression at = jsRe(S + u"+@"_s + D + u"*\\z"_s);
    static const QRegularExpression spaces = jsRe(S + u"+"_s);
    QString s = n;
    s.replace(marks, QString());
    s.replace(gen, QString());                     // (no g flag: the pattern is anchored, at most one match anyway)
    s.replace(word, QString());
    s.replace(at, QString());
    s.replace(spaces, u" "_s);
    return js::trim(s);
}

// "Raptor Lake-P [Iris Xe Graphics]" -> "Iris Xe Graphics"; "NVIDIA GeForce RTX 3050 ..." -> "GeForce RTX 3050 ..."
QString SensorParser::cleanGpu(const QString &n)
{
    static const QRegularExpression bracket = jsRe(u"\\[([^\\]]+)\\]"_s);
    static const QRegularExpression nvidia = jsRe(u"^NVIDIA"_s + QString::fromUtf16(js::reSpace) + u"+"_s);
    const QRegularExpressionMatch m = bracket.match(n);
    QString s = m.hasMatch() ? m.captured(1) : n;
    s.replace(nvidia, QString());
    return js::trim(s);
}

void SensorParser::parseHw(const QString &txt)
{
    HwNames h;
    struct Gpu { QString vendor, name; };
    QList<Gpu> gpus;
    for (const QString &line : txt.split(u'\n')) {
        const QStringList p = line.split(u'|');
        if (p[0] == u"cpu"_s && p.size() >= 3) { h.cpu = cleanCpu(p[1]); h.cores = p[2]; }
        else if (p[0] == u"gpu"_s && p.size() >= 4) gpus.append({ p[1], cleanGpu(p.mid(3).join(u'|')) });
    }
    auto find = [&](auto pred) -> qsizetype {
        for (qsizetype i = 0; i < gpus.size(); ++i) if (pred(i)) return i;
        return -1;
    };
    const qsizetype dgpu = find([&](qsizetype i) { return gpus[i].vendor == u"0x10de"_s; });
    qsizetype igpu = find([&](qsizetype i) { return gpus[i].vendor == u"0x8086"_s; });
    if (igpu < 0 && dgpu >= 0) igpu = find([&](qsizetype i) { return gpus[i].vendor == u"0x1002"_s; });
    const qsizetype other = find([&](qsizetype i) { return i != dgpu && i != igpu; });
    h.gpu = dgpu >= 0 ? gpus[dgpu].name : (other >= 0 ? gpus[other].name : QString());
    h.igpu = igpu >= 0 ? gpus[igpu].name : QString();
    hw = h;
}

// ---------- one round (Sensors.qml parse) ----------

void SensorParser::parse(const QString &txt, qint64 nowMs)
{
    double pkg = NaN, coreMax = NaN, amdCpu = NaN, acpi = NaN;
    double nv = NaN, amdGpu = NaN, igpu = NaN;
    QString nvNote;
    bool hasNv = false, nvFailed = false;
    double cpuUsage = NaN, nvUsage = NaN;
    QString fanDrv;
    QList<double> fanRpm;                          // a JS array with holes: unset entries are NaN
    for (const QString &line : txt.split(u'\n')) {
        const QStringList p = line.split(u'|');
        if (p[0] == u"fan"_s && p.size() == 4) {
            if (fanDrv.isEmpty()) fanDrv = p[1];   // the first fan driver (hp, thinkpad, dell_smm, ...)
            if (p[1] == fanDrv) {
                const double idx = js::parseInt(p[2]) - 1;
                // fanRpm[idx] = ...: only a valid array index makes an element (negative/NaN become properties)
                if (std::isfinite(idx) && idx >= 0 && idx < (1 << 16)) {
                    const qsizetype k = qsizetype(idx);
                    while (fanRpm.size() <= k) fanRpm.append(NaN);
                    fanRpm[k] = js::parseInt(p[3]);
                }
            }
            continue;
        }
        if (p[0] == u"usage"_s) {
            if (p.size() == 4 && p[1] == u"cpu"_s) {
                const double total = p[2].toDouble(), idle = p[3].toDouble();
                const double dt = total - lastCpuTotal, di = idle - lastCpuIdle;
                if (lastCpuTotal > 0 && dt > 0 && di >= 0 && di <= dt) cpuUsage = 100 * (1 - di / dt);
                lastCpuTotal = total;
                lastCpuIdle = idle;
            } else if (p.size() == 3 && p[1] == u"gpu"_s) {
                bool ok = false;
                const double v = p[2].toDouble(&ok);
                if (ok && v >= 0 && v <= 100) nvUsage = v;
            }
            continue;
        }
        if (p.size() != 3) continue;
        const QString &n = p[0], &raw = p[2];
        const QString l = p[1].toLower();
        if (n == u"nvidia"_s || n == u"nouveau"_s) {
            hasNv = true;
            // not read this round: the last value (while backing off: the error, not an older "asleep")
            if (raw == u"skip"_s) { nv = lastNv; nvUsage = lastNvUsage; nvNote = nvFails > 0 ? u"error"_s : lastNvNote; }
            else if (raw == u"asleep"_s) nvNote = u"asleep"_s;
            else if (raw == u"fail"_s) { nvFailed = true; nvNote = u"error"_s; }
            else nv = js::max(std::isfinite(nv) ? nv : -1, js::parseInt(raw) / 1000);
            continue;
        }
        const double v = js::parseInt(raw) / 1000;
        if (!std::isfinite(v)) continue;
        if (n == u"coretemp"_s) {
            if (l.contains(u"package"_s)) pkg = v;
            else coreMax = std::isfinite(coreMax) ? js::max(coreMax, v) : v;
        } else if (n == u"k10temp"_s || n == u"zenpower"_s) {
            if (!std::isfinite(amdCpu) || l.contains(u"tctl"_s) || l.contains(u"tdie"_s)) amdCpu = v;
        } else if (n == u"acpitz"_s) {
            if (!std::isfinite(acpi)) acpi = v;
        } else if (n == u"i915"_s || n == u"xe"_s) {
            igpu = v;
        } else if (n == u"amdgpu"_s) {
            if (!std::isfinite(amdGpu) || l.contains(u"edge"_s)) amdGpu = v;
        }
    }
    const double cpu = std::isfinite(pkg) ? pkg : (std::isfinite(amdCpu) ? amdCpu : (std::isfinite(coreMax) ? coreMax : acpi));
    QList<Reading> out{ { u"CPU"_s, hw.cpu, hw.cores, cpu, QString(), cpuUsage } };
    double gpu = nv;
    if (std::isfinite(amdGpu)) { if (hasNv) igpu = amdGpu; else gpu = amdGpu; }
    if (hasNv || std::isfinite(gpu) || !hw.gpu.isEmpty()) out.append({ u"GPU"_s, hw.gpu, QString(), gpu, nvNote, std::isfinite(nv) ? nvUsage : NaN });
    if (std::isfinite(igpu) || !hw.igpu.isEmpty())
        out.append({ u"iGPU"_s, hw.igpu, QString(), igpu, std::isfinite(igpu) ? QString() : u"nosensor"_s });
    if (hasNv) { lastNv = nv; lastNvUsage = nvUsage; lastNvNote = nvNote; }   // used on rounds where the GPU is skipped
    if (nvFailed && !std::isfinite(nv)) {
        nvFails++;
        const double wait = nvFails < 2 ? 0 : std::min(600000.0, 30000 * std::pow(2.0, nvFails - 2));
        nvRetryAt = nowMs + qint64(wait);
        if ((nvFails == 2 || (wait == 600000 && nvFails % 6 == 0)) && log)
            log(u"[solwind] nvidia-smi gave no temperature (failures: %1), next try in %2 s. After an NVIDIA driver update a reboot usually fixes this."_s
                    .arg(nvFails).arg(qint64(js::round(wait / 1000))));
    } else if (std::isfinite(nv) && nvFails > 0) {
        if (log) log(u"[solwind] nvidia-smi works again"_s);
        nvFails = 0;
        nvRetryAt = 0;
    }
    readings = out;
    rpms = fanRpm;
    for (double v : rpms) if (v > 0) { rpmWorks = true; break; }
}

void SensorParser::deactivate()
{
    for (Reading &r : readings) { r.value = NaN; r.usage = NaN; }
    lastCpuTotal = lastCpuIdle = 0;                // the next round has nothing to compare with
    rpms.clear();
}

bool SensorParser::nextRoundReadsGpu(bool fast, qint64 nowMs)
{
    if (nvRetryAt - nowMs > 600000) nvRetryAt = nowMs + 600000;   // the clock went back: never wait more than the cap
    const bool gpu = (fast || tick % 4 == 0) && nowMs >= nvRetryAt;
    tick++;
    return gpu;
}

double SensorParser::gpu() const
{
    for (const Reading &r : readings) if (r.label == u"GPU"_s) return r.value;
    return NaN;
}
double SensorParser::cpu() const
{
    for (const Reading &r : readings) if (r.label == u"CPU"_s) return r.value;
    return NaN;
}
bool SensorParser::hasGpu() const
{
    for (const Reading &r : readings) if (r.label == u"GPU"_s) return true;
    return false;
}
double SensorParser::rpm(int i, int nbfcFans) const
{
    return rpmWorks && rpms.size() == nbfcFans && i >= 0 && i < rpms.size() ? rpms[i] : NaN;
}

// ---------- the service ----------

// Lives in the worker thread: only reads files and runs nvidia-smi / nproc / lspci, never touches the service's state
class SensorWorker : public QObject
{
    Q_OBJECT
public:
    SensorWorker(const Paths &paths, const std::atomic<bool> *stop) : m_paths(paths) { m_opt.stop = stop; }
    void setOptions(SensorReader::Options o) { o.stop = m_opt.stop; m_opt = o; }
public slots:
    void readRound(bool gpu, quint64 generation) { emit roundRead(SensorReader::round(m_paths, gpu, m_opt), generation); }
    void readHw() { emit hwRead(SensorReader::hwInfo(m_paths, m_opt.stop)); }
signals:
    void roundRead(const QString &txt, quint64 generation);
    void hwRead(const QString &txt);
private:
    Paths m_paths;
    SensorReader::Options m_opt;
};

Sensors::Sensors(const Paths &paths, QObject *parent) : QObject(parent)
{
    now = [] { return QDateTime::currentMSecsSinceEpoch(); };
    m_p.log = [](const QString &s) { qInfo("%s", qPrintable(s)); };
    m_worker = new SensorWorker(paths, &m_stop);
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &SensorWorker::roundRead, this, [this](const QString &txt, quint64 generation) {
        m_busy = false;
        if (!m_active) return;                    // turned off while it was reading: no fresh values after P-3
        // Started before the sensors were turned off (and on again): not a fresh round. Read again now instead.
        if (generation != m_generation) { poll(); return; }
        const SensorParser before = m_p;
        m_p.parse(txt, now());
        if (!(m_p.readings == before.readings) || m_p.rpms.size() != before.rpms.size() || m_p.rpmWorks != before.rpmWorks
            || !std::equal(m_p.rpms.begin(), m_p.rpms.end(), before.rpms.begin(), same))
            emit changed();
    });
    connect(m_worker, &SensorWorker::hwRead, this, [this](const QString &txt) {
        m_hwBusy = false;
        const HwNames before = m_p.hw;
        m_p.parseHw(txt);
        if (!(m_p.hw == before)) emit changed();
    });
    m_thread.setObjectName(u"sensors"_s);
    m_timer.setInterval(3000);
    connect(&m_timer, &QTimer::timeout, this, &Sensors::poll);
}

void Sensors::start()
{
    if (m_started) return;
    m_started = true;
    m_thread.start();
    // Hardware names once, before the first round (same thread, so they come first)
    m_hwBusy = true;
    QMetaObject::invokeMethod(m_worker, &SensorWorker::readHw, Qt::QueuedConnection);
    if (m_active) { m_timer.start(); poll(); }
}

Sensors::~Sensors()
{
    m_stop = true;                                // a program the worker runs (nvidia-smi, lspci) is killed now
    m_thread.quit();
    m_thread.wait();
    if (!m_started) delete m_worker;              // never moved into a running thread: finished() won't delete it
}

void Sensors::setReaderOptions(const SensorReader::Options &o)
{
    QMetaObject::invokeMethod(m_worker, [w = m_worker, o] { w->setOptions(o); }, Qt::QueuedConnection);
}

void Sensors::setActive(bool on)
{
    if (on == m_active) return;
    m_active = on;
    if (on) {
        if (!m_started) return;                   // start() begins the reading
        m_timer.start();
        poll();                                   // triggeredOnStart
    } else {
        m_timer.stop();
        ++m_generation;
        m_p.deactivate();                         // the last values would go stale in the tray, status and graph
        emit changed();
    }
}

void Sensors::setFast(bool on)
{
    if (on == m_fast) return;
    m_fast = on;
    m_timer.setInterval(on ? 3000 : 5000);       // (restarts a running timer)
}

void Sensors::poll()
{
    if (m_busy || !m_started || !m_active) return;
    const bool gpu = m_p.nextRoundReadsGpu(m_fast, now());
    m_busy = true;
    QMetaObject::invokeMethod(m_worker, [w = m_worker, gpu, g = m_generation] { w->readRound(gpu, g); }, Qt::QueuedConnection);
}

#include "Sensors.moc"
