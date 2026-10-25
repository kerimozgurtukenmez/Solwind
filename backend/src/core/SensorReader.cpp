#include "SensorReader.h"

#include <QByteArrayList>
#include <QProcess>
#include <QStandardPaths>

#include <algorithm>
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace SensorReader {

namespace {

// `[ -r file ]`
bool readable(const QByteArray &path) { return ::access(path.constData(), R_OK) == 0; }

// The whole (small) file, or nullopt-like empty with ok=false on an open/read error
struct Raw {
    bool ok = false;
    QByteArray bytes;
};
Raw slurp(const QByteArray &path, qsizetype limit = 1 << 16)
{
    Raw r;
    const int fd = ::open(path.constData(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return r;
    char buf[4096];
    for (;;) {
        const ssize_t n = ::read(fd, buf, sizeof buf);
        if (n < 0) { if (errno == EINTR) continue; ::close(fd); return r; }
        if (n == 0) break;
        r.bytes.append(buf, n);
        if (r.bytes.size() >= limit) break;
    }
    ::close(fd);
    r.ok = true;
    return r;
}

bool ifsSpace(char c) { return c == ' ' || c == '\t' || c == '\n'; }
// [[:space:]] in sed (C locale; the process locale is not used for these bytes)
bool posixSpace(char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }

// `read -r var < file` with the default IFS: the first line, without leading and trailing spaces/tabs.
// `complete` is read's exit status: false on a read error or when the file ends without a new line.
struct Line {
    bool complete = false;
    QByteArray value;
};
Line readLine(const QByteArray &path)
{
    Line l;
    const Raw r = slurp(path);
    if (!r.ok) return l;
    const qsizetype nl = r.bytes.indexOf('\n');
    QByteArray v = nl < 0 ? r.bytes : r.bytes.left(nl);
    v.replace('\0', QByteArray());                          // bash drops NUL bytes ("ignored null byte")
    qsizetype a = 0, b = v.size();
    while (a < b && ifsSpace(v[a])) ++a;
    while (b > a && ifsSpace(v[b - 1])) --b;
    l.value = v.mid(a, b - a);
    l.complete = nl >= 0;
    return l;
}

// `$(cat file)`: the content without its trailing new lines ("" if it can't be read)
QByteArray catFile(const QByteArray &path)
{
    QByteArray b = slurp(path).bytes;
    while (b.endsWith('\n')) b.chop(1);
    return b;
}

// A shell glob in one folder (prefix*suffix), sorted the way the old scripts saw it: byte order. Measured on the dev
// machine: bash's glob order under C and en_US.UTF-8 (the service's locale) is the same as byte order here
// (temp10_input < temp1_input < temp2_input).
QByteArrayList glob(const QByteArray &dir, const QByteArray &prefix, const QByteArray &suffix = QByteArray())
{
    QByteArrayList out;
    DIR *d = ::opendir(dir.constData());
    if (!d) return out;
    while (const dirent *e = ::readdir(d)) {
        const QByteArray n(e->d_name);
        if (n.startsWith('.') && !prefix.startsWith('.')) continue;          // a glob doesn't match dot files
        if (n.size() < prefix.size() + suffix.size() || !n.startsWith(prefix) || !n.endsWith(suffix)) continue;
        out << n;
    }
    ::closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

// $(prog args) with a time limit (SIGTERM, then SIGKILL): stdout only, or "" if it couldn't run. Waits in short steps
// so a shutdown (stop) doesn't wait for it.
QByteArray run(const QString &prog, const QStringList &args, int timeoutMs, int killMs, const std::atomic<bool> *stop)
{
    QProcess p;
    p.setStandardInputFile(QProcess::nullDevice());
    p.setStandardErrorFile(QProcess::nullDevice());           // 2>/dev/null
    p.start(prog, args);
    if (!p.waitForStarted(timeoutMs)) return QByteArray();
    auto stopped = [&] { return stop && stop->load(); };
    for (int waited = 0; !p.waitForFinished(100); waited += 100) {
        if (stopped()) { p.kill(); p.waitForFinished(-1); return QByteArray(); }
        if (waited + 100 >= timeoutMs) {
            p.terminate();
            for (int k = 0; !p.waitForFinished(100); k += 100)
                if (stopped() || k + 100 >= killMs) { p.kill(); p.waitForFinished(-1); break; }
            break;
        }
    }
    return p.readAllStandardOutput();
}

const QByteArrayList tempDrivers = { "coretemp", "k10temp", "zenpower", "acpitz", "i915", "xe", "amdgpu", "nouveau" };

} // namespace

QString round(const Paths &paths, bool gpu, const Options &opt)
{
    QByteArray out;
    const QByteArray hwmon = paths.sys(u"/class/hwmon"_s).toLocal8Bit();
    for (const QByteArray &e : glob(hwmon, "hwmon")) {
        const QByteArray h = hwmon + '/' + e;
        if (!readable(h + "/name")) continue;
        const QByteArray n = readLine(h + "/name").value;
        if (tempDrivers.contains(n)) {
            for (const QByteArray &t : glob(h, "temp", "_input")) {
                const QByteArray f = h + '/' + t;
                if (!readable(f)) continue;
                const Line v = readLine(f);
                if (!v.complete) continue;                       // read -r v < "$t" || continue
                const QByteArray label = f.left(f.size() - 6) + "_label";
                const QByteArray l = readable(label) ? readLine(label).value : QByteArray();
                out += n + '|' + l + '|' + v.value + '\n';
            }
        }
        for (const QByteArray &fn : glob(h, "fan", "_input")) {
            const QByteArray f = h + '/' + fn;
            if (!readable(f)) continue;
            const Line v = readLine(f);
            if (!v.complete) continue;
            out += "fan|" + n + '|' + fn.mid(3, fn.size() - 9) + '|' + v.value + '\n';   // ${f##*/fan}, %_input
        }
    }

    // NVIDIA: a card that is asleep is left alone (nvidia-smi would wake it). nvidia-smi runs at most once per round:
    // sensors.sh ran the same command once per awake card, which gives the same first temperature.
    const QByteArray pci = paths.sys(u"/bus/pci/devices"_s).toLocal8Bit();
    bool smiDone = false;
    QByteArray smiTemp;
    QString smi;
    bool smiLooked = false;
    for (const QByteArray &e : glob(pci, "")) {
        const QByteArray d = pci + '/' + e;
        if (!readable(d + "/vendor")) continue;
        if (readLine(d + "/vendor").value != "0x10de") continue;
        if (!readLine(d + "/class").value.startsWith("0x03")) continue;
        const QByteArray rs = readable(d + "/power/runtime_status") ? readLine(d + "/power/runtime_status").value : QByteArray();
        if (rs == "suspended") { out += "nvidia||asleep\n"; continue; }
        if (!gpu) { out += "nvidia||skip\n"; continue; }
        if (!smiLooked) {
            smiLooked = true;
            smi = opt.nvidiaSmi.isEmpty() ? QStandardPaths::findExecutable(u"nvidia-smi"_s) : opt.nvidiaSmi;
        }
        if (smi.isEmpty()) continue;                             // not installed: no line
        if (!smiDone) {
            smiDone = true;
            const QByteArray o = run(smi, { u"--query-gpu=temperature.gpu"_s, u"--format=csv,noheader,nounits"_s },
                                     opt.smiTimeoutMs, opt.smiKillMs, opt.stop);
            qsizetype k = 0;                                     // ${t%%[!0-9]*}: the leading digits
            while (k < o.size() && o[k] >= '0' && o[k] <= '9') ++k;
            smiTemp = o.left(k);
        }
        out += smiTemp.isEmpty() ? QByteArray("nvidia||fail\n") : "nvidia||" + smiTemp + "000\n";
    }
    return QString::fromUtf8(out);
}

QString hwInfo(const Paths &paths, const std::atomic<bool> *stop)
{
    QByteArray out;
    // m=$(grep -m1 "model name" /proc/cpuinfo | cut -d: -f2- | sed 's/^[[:space:]]*//')
    QByteArray model;
    for (const QByteArray &line : slurp("/proc/cpuinfo", 1 << 20).bytes.split('\n')) {
        if (!line.contains("model name")) continue;
        const qsizetype c = line.indexOf(':');
        model = c < 0 ? line : line.mid(c + 1);                  // cut prints a line without the delimiter whole
        qsizetype a = 0;
        while (a < model.size() && posixSpace(model[a])) ++a;
        model = model.mid(a);
        break;
    }
    QByteArray threads = run(u"nproc"_s, {}, 5000, 2000, stop);
    while (threads.endsWith('\n')) threads.chop(1);
    out += "cpu|" + model + '|' + threads + '\n';

    const QByteArray pci = paths.sys(u"/bus/pci/devices"_s).toLocal8Bit();
    QString lspci;
    bool lspciLooked = false;
    for (const QByteArray &e : glob(pci, "")) {
        const QByteArray d = pci + '/' + e;
        if (!catFile(d + "/class").startsWith("0x03")) continue;
        const QByteArray v = catFile(d + "/vendor");
        QByteArray n;
        if (v == "0x10de") {
            // The driver's information file doesn't wake the card
            for (const QByteArray &line : slurp("/proc/driver/nvidia/gpus/" + e + "/information").bytes.split('\n')) {
                if (!line.startsWith("Model:")) continue;
                qsizetype a = 6;
                while (a < line.size() && posixSpace(line[a])) ++a;
                n = line.mid(a);
                break;
            }
            // Asleep: lspci could wake it (reading config space)
            if (n.isEmpty() && catFile(d + "/power/runtime_status") == "suspended") n = "NVIDIA GPU";
        }
        if (n.isEmpty()) {
            if (!lspciLooked) { lspciLooked = true; lspci = QStandardPaths::findExecutable(u"lspci"_s); }
            if (!lspci.isEmpty()) {
                // awk -F'\t' '$1=="Device:"{print $2; exit}'
                for (const QByteArray &line : run(lspci, { u"-s"_s, QString::fromLocal8Bit(e), u"-vmm"_s }, 10000, 2000, stop).split('\n')) {
                    const QByteArrayList f = line.split('\t');
                    if (f[0] != "Device:") continue;
                    n = f.size() > 1 ? f[1] : QByteArray();
                    break;
                }
            }
        }
        out += "gpu|" + v + '|' + e + '|' + n + '\n';
    }
    return QString::fromUtf8(out);
}

} // namespace SensorReader
