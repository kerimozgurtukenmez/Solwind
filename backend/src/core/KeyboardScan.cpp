#include "KeyboardScan.h"

#include <QByteArrayList>
#include <QFile>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

QByteArray enc(const QString &p) { return QFile::encodeName(p); }

// test -e / -d / -f (they follow links), -r / -w (access(2), as bash's test does for a normal user)
bool exists(const QByteArray &p) { struct stat st; return ::stat(p.constData(), &st) == 0; }
bool isDir(const QByteArray &p) { struct stat st; return ::stat(p.constData(), &st) == 0 && S_ISDIR(st.st_mode); }
bool isFile(const QByteArray &p) { struct stat st; return ::stat(p.constData(), &st) == 0 && S_ISREG(st.st_mode); }
bool readable(const QByteArray &p) { return ::access(p.constData(), R_OK) == 0; }
bool writable(const QByteArray &p) { return ::access(p.constData(), W_OK) == 0; }

// `read -r var < file`: false if the file can't be opened or the read fails before anything came (the variable keeps
// its value then, measured), else the first line without leading/trailing spaces and tabs (NUL bytes dropped)
bool readLine(const QByteArray &path, QByteArray *out)
{
    const int fd = ::open(path.constData(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    QByteArray line;
    char buf[512];
    bool failed = false;
    for (;;) {
        ssize_t n;
        do n = ::read(fd, buf, sizeof buf);
        while (n < 0 && errno == EINTR);
        if (n < 0) { failed = line.isEmpty(); break; }
        if (n == 0) break;
        const char *nl = static_cast<const char *>(std::memchr(buf, '\n', size_t(n)));
        line.append(buf, nl ? nl - buf : n);
        if (nl || line.size() > 65536) break;
    }
    ::close(fd);
    if (failed) return false;
    line.replace('\0', QByteArray());
    qsizetype a = 0, b = line.size();
    while (a < b && (line[a] == ' ' || line[a] == '\t')) ++a;
    while (b > a && (line[b - 1] == ' ' || line[b - 1] == '\t')) --b;
    *out = line.mid(a, b - a);
    return true;
}
// r(): `v=""; [ -r "$1" ] && read -r v < "$1"; printf '%s' "$v"` (in a $(...) subshell: no variable outlives it)
QByteArray r(const QByteArray &path)
{
    QByteArray v;
    if (readable(path)) readLine(path, &v);
    return v;
}

// "<dir>/*": the entries (not starting with "."), sorted as bash sorts a glob (strcoll); none -> the pattern itself,
// which then fails every test in the script, so it is simply left out here
QByteArrayList glob(const QByteArray &dir, const QByteArray &prefix = QByteArray())
{
    QByteArrayList out;
    DIR *d = ::opendir(dir.constData());
    if (!d) return out;
    while (const dirent *e = ::readdir(d)) {
        if (e->d_name[0] == '.') continue;
        if (!prefix.isEmpty() && std::strncmp(e->d_name, prefix.constData(), size_t(prefix.size())) != 0) continue;
        out << QByteArray(e->d_name);
    }
    ::closedir(d);
    std::sort(out.begin(), out.end(), [](const QByteArray &a, const QByteArray &b) { return std::strcoll(a.constData(), b.constData()) < 0; });
    return out;
}

// `readlink -f <path>`, then ${x##*/}
QByteArray driverOf(const QByteArray &link)
{
    char buf[PATH_MAX];
    if (!::realpath(link.constData(), buf)) return QByteArray();
    const QByteArray p(buf);
    return p.mid(p.lastIndexOf('/') + 1);
}

bool keyboardLight(const QByteArray &n)   // case "$n" in *::kbd_backlight|*::kbd_backlight_*|*::kbd_zoned_backlight-*
{
    return n.endsWith("::kbd_backlight") || n.contains("::kbd_backlight_") || n.contains("::kbd_zoned_backlight-");
}

// grep -q [-s] -e 'chmod 0666' -e '/bin/sh' <file>
bool legacyRule(const QByteArray &path)
{
    QFile f(QFile::decodeName(path));
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray t = f.read(1 << 20);
    return t.contains("chmod 0666") || t.contains("/bin/sh");
}

} // namespace

QString KeyboardScan::scan(const Paths &paths)
{
    QByteArrayList out;
    const QByteArray sys = enc(paths.sysRoot) + "/sys";
    const QByteArray dmi = sys + "/class/dmi/id";
    out << "dmi|" + r(dmi + "/sys_vendor") + '|' + r(dmi + "/product_name") + '|' + r(dmi + "/product_version") + '|'
               + r(dmi + "/product_family") + '|' + r(dmi + "/board_name");
    out << "kernel|" + r("/proc/sys/kernel/osrelease");

    QByteArray v;                                         // the script's $v: kept across lights and files
    const QByteArray leds = sys + "/class/leds";
    for (const QByteArray &n : glob(leds)) {
        const QByteArray d = leds + '/' + n;
        if (!keyboardLight(n)) continue;
        if (!exists(d + "/brightness")) continue;
        const QByteArray drv = exists(d + "/device/driver") ? driverOf(d + "/device/driver") : QByteArray();
        out << "led|" + n + '|' + drv + '|' + d;
        for (const char *f : { "brightness", "max_brightness", "multi_index", "multi_intensity", "multi_max_intensity" }) {
            const QByteArray p = d + '/' + f;
            if (!readable(p)) continue;
            readLine(p, &v);
            out << "v|" + n + '|' + f + '|' + v;
        }
        for (const char *f : { "brightness", "multi_intensity", "kbd_rgb_mode" }) {
            const QByteArray p = d + '/' + f;
            if (exists(p)) out << "x|" + n + '|' + f;
            if (writable(p)) out << "w|" + n + '|' + f;
        }
    }

    const QByteArray z = sys + "/devices/platform/hp-wmi";
    if (isDir(z + "/rgb_zones")) {
        const QByteArray drv = exists(z + "/driver") ? driverOf(z + "/driver") : QByteArray();
        for (const QByteArray &e : glob(z + "/rgb_zones", "zone")) {
            const QByteArray f = z + "/rgb_zones/" + e;
            if (!exists(f)) continue;
            const QByteArray n = "hp-wmi/rgb_zones/" + e;
            out << "zone|" + n + '|' + drv + '|' + f;
            out << "v|" + n + "|color|" + r(f);
            if (writable(f)) out << "w|" + n + "|color";
        }
    }

    QByteArray vid;
    const QByteArray usb = sys + "/bus/usb/devices";
    for (const QByteArray &e : glob(usb)) {
        const QByteArray u = usb + '/' + e;
        if (!readable(u + "/idVendor")) continue;
        readLine(u + "/idVendor", &vid);
        if (vid != "0b05" && vid != "048d") continue;
        out << "usb|" + vid + '|' + r(u + "/idProduct") + '|' + r(u + "/product");
    }

    const QByteArray f = enc(paths.udev(QStringLiteral("/etc/udev/rules.d/90-solwind.rules")));
    const QByteArray o = enc(paths.udev(QStringLiteral("/etc/udev/rules.d/90-ccenter.rules")));
    if (legacyRule(f) || legacyRule(o)) out << "rule|etc|legacy";
    else if (isFile(o)) out << "rule|etc|old";
    else if (isFile(f)) out << "rule|etc|current";
    for (const char *l : { "/usr/lib/udev/rules.d/90-solwind.rules", "/usr/lib/udev/rules.d/90-ccenter.rules",
                           "/usr/local/lib/udev/rules.d/90-ccenter.rules" }) {
        const QByteArray p = enc(paths.udev(QString::fromLatin1(l)));
        if (!isFile(p)) continue;
        out << (legacyRule(p) ? "rule|lib|legacy" : "rule|lib");
    }
    return QString::fromUtf8(out.join('\n') + '\n');
}
