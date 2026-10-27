#include "PluginTool.h"

#include "core/I18n.h"
#include "plugins/SolwindPlugin.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QPluginLoader>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <csignal>
#include <optional>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace PluginTool {

// ---------- the catalog ----------

const Entry *Catalog::find(const QString &id) const
{
    for (const Entry &e : plugins) if (e.id == id) return &e;
    return nullptr;
}

bool validId(const QString &id)
{
    static const QRegularExpression re(u"\\A[A-Za-z0-9][A-Za-z0-9_.+-]*/[A-Za-z0-9][A-Za-z0-9_.+-]*\\z"_s);
    return re.match(id).hasMatch() && !id.contains(u".."_s);
}

bool validCommit(const QString &commit)
{
    // (SHA-1 ids only: a SHA-256 repository would need its own git init, and Solwind-plugins is SHA-1)
    static const QRegularExpression re(u"\\A[0-9a-f]{40}\\z"_s);
    return re.match(commit).hasMatch();
}

bool validRepository(const QString &url)
{
    static const QRegularExpression plain(u"\\A[A-Za-z0-9._~:/%+@-]+\\z"_s);
    if (!plain.match(url).hasMatch()) return false;            // (no spaces, quotes, options, ext::...)
    const QUrl u(url, QUrl::StrictMode);
    if (!u.isValid() || !u.userInfo().isEmpty() || u.hasQuery() || u.hasFragment()) return false;
    if (u.scheme() == u"https"_s) return !u.host().isEmpty() && u.path().size() > 1;
    if (u.scheme() == u"file"_s) return u.host().isEmpty() && u.path().startsWith(u'/') && u.path().size() > 1;
    return false;
}

namespace {
bool stringList(const QJsonValue &v, QStringList *out, const std::function<bool(const QString &)> &ok)
{
    if (!v.isArray()) return false;
    for (const QJsonValue &x : v.toArray()) {
        if (!x.isString() || !ok(x.toString())) return false;
        *out << x.toString();
    }
    return !out->isEmpty();
}

bool parseFits(const QJsonObject &o, Fits *f, QString *why)
{
    for (auto it = o.begin(); it != o.end(); ++it) {
        const QString k = it.key();
        bool ok = false;
        if (k == u"programs"_s)
            ok = stringList(*it, &f->programs, [](const QString &s) { return !s.isEmpty() && !s.contains(u'/'); });
        else if (k == u"paths"_s)
            ok = stringList(*it, &f->paths, [](const QString &s) { return s.startsWith(u"/sys/"_s) && !s.contains(u".."_s); });
        else if (k == u"vendors"_s)
            ok = stringList(*it, &f->vendors, [](const QString &s) { return !s.trimmed().isEmpty(); });
        else if (k == u"leds"_s && it->isArray()) {
            ok = !it->toArray().isEmpty();
            for (const QJsonValue &x : it->toArray()) {
                const QJsonObject l = x.toObject();
                const QString name = l.value(u"name"_s).toString(), driver = l.value(u"driver"_s).toString();
                if (!x.isObject() || name.isEmpty() || name.contains(u'/') || (l.contains(u"driver"_s) && !l.value(u"driver"_s).isString())
                    || l.size() > (l.contains(u"driver"_s) ? 2 : 1)) { ok = false; break; }
                f->leds.append({ name, driver });
            }
        }
        if (!ok) { *why = u"fits.%1 isn't valid"_s.arg(k); return false; }
    }
    return true;
}
} // namespace

bool parseCatalog(const QByteArray &json, Catalog *out, QString *why)
{
    QJsonParseError e;
    const QJsonDocument d = QJsonDocument::fromJson(json, &e);
    if (e.error != QJsonParseError::NoError) { *why = e.errorString(); return false; }
    const QJsonObject root = d.object();
    if (root.value(u"version"_s) != QJsonValue(1)) { *why = u"not a version 1 catalog"_s; return false; }
    Catalog c;
    c.repository = root.value(u"repository"_s).toString();
    if (!validRepository(c.repository)) { *why = u"the repository isn't an https:// address"_s; return false; }
    if (!root.value(u"plugins"_s).isArray()) { *why = u"no plugin list"_s; return false; }
    for (const QJsonValue &v : root.value(u"plugins"_s).toArray()) {
        const QJsonObject o = v.toObject();
        Entry en;
        en.id = o.value(u"id"_s).toString();
        en.kind = o.value(u"kind"_s).toString();
        en.description = o.value(u"description"_s).toString();
        en.tested = o.value(u"tested"_s).toString();
        en.commit = o.value(u"commit"_s).toString();
        auto bad = [&](const QString &what) { *why = u"%1: %2"_s.arg(en.id.isEmpty() ? u"a plugin"_s : en.id, what); return false; };
        if (!v.isObject() || !validId(en.id)) return bad(u"not a plugin id"_s);
        if (c.find(en.id)) return bad(u"listed twice"_s);
        if (en.kind != u"fan"_s && en.kind != u"keyboard"_s) return bad(u"kind is fan or keyboard"_s);
        if (!validCommit(en.commit)) return bad(u"the commit isn't a full commit id"_s);
        if (en.description.trimmed().isEmpty()) return bad(u"no description"_s);
        if (o.contains(u"fits"_s) && (!o.value(u"fits"_s).isObject() || !parseFits(o.value(u"fits"_s).toObject(), &en.fits, why)))
            return bad(why->isEmpty() ? u"fits isn't an object"_s : *why);
        c.plugins << en;
    }
    *out = c;
    return true;
}

Fit fits(const Fits &f, const QString &sysRoot)
{
    if (f.empty()) return Fit::Unknown;
    const QString sys = sysRoot + u"/sys"_s;
    if (!f.programs.isEmpty()
        && std::none_of(f.programs.begin(), f.programs.end(), [](const QString &p) { return !QStandardPaths::findExecutable(p).isEmpty(); }))
        return Fit::No;
    if (!f.paths.isEmpty()
        && std::none_of(f.paths.begin(), f.paths.end(), [&](const QString &p) { return QFileInfo::exists(sysRoot + p); }))
        return Fit::No;
    if (!f.vendors.isEmpty()) {
        QFile v(sys + u"/class/dmi/id/sys_vendor"_s);
        const QString vendor = v.open(QIODevice::ReadOnly) ? QString::fromUtf8(v.readAll()).trimmed() : QString();
        if (std::none_of(f.vendors.begin(), f.vendors.end(), [&](const QString &x) { return vendor.contains(x, Qt::CaseInsensitive); }))
            return Fit::No;
    }
    if (!f.leds.isEmpty()) {
        const QStringList lights = QDir(sys + u"/class/leds"_s).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System);
        bool any = false;
        for (const Fits::Led &l : f.leds) {
            const QRegularExpression glob(QRegularExpression::wildcardToRegularExpression(l.name, QRegularExpression::NonPathWildcardConversion));
            for (const QString &name : lights) {
                if (!glob.match(name).hasMatch()) continue;
                const QString driver = QFileInfo(QFileInfo(sys + u"/class/leds/"_s + name + u"/device/driver"_s).symLinkTarget()).fileName();
                if (l.driver.isEmpty() || driver == l.driver) { any = true; break; }
            }
            if (any) break;
        }
        if (!any) return Fit::No;
    }
    return Fit::Yes;
}

QList<Installed> installed(const QString &pluginDir)
{
    QList<Installed> out;
    QDirIterator it(pluginDir, { u"*.so"_s }, QDir::Files, QDirIterator::Subdirectories);
    QStringList files;
    while (it.hasNext()) files << it.next();
    files.sort();
    for (const QString &f : files) {
        Installed i;
        i.id = QDir(pluginDir).relativeFilePath(f).chopped(3);
        if (!validId(i.id)) continue;
        QFile r(pluginDir + u'/' + i.id + u".json"_s);
        if (r.open(QIODevice::ReadOnly)) {
            const QJsonObject o = QJsonDocument::fromJson(r.readAll()).object();
            i.commit = o.value(u"commit"_s).toString();
            i.repository = o.value(u"repository"_s).toString();
            i.sdk = o.value(u"sdk"_s).toString();
            i.source = o.value(u"source"_s).toString();
        }
        out << i;
    }
    return out;
}

// ---------- the commands ----------

namespace {
class Runner
{
public:
    Runner(const Options &o, const I18n &i18n, QTextStream &out, QTextStream &err) : o(o), i18n(i18n), out(out), err(err) {}

    int list(bool json);
    int install(const QString &id, const QString &sourceDir);
    int update(const QStringList &ids);
    int remove(const QString &id);

private:
    const Options &o;
    const I18n &i18n;
    QTextStream &out, &err;
    Catalog catalog;
    QString log;                          // the build log of the plugin being installed

    QString t(const QString &s) const { return i18n.tIn(o.lang, s); }
    int fail(const QString &msg) { err << msg << Qt::endl; return 1; }
    bool loadCatalog();
    bool notRoot();
    int exec(const QString &program, const QStringList &args, const QString &dir, int timeoutMs);
    QString tailOfLog() const;
    QString buildAndInstall(const QString &srcDir, const QString &id, const QString &kind, const QString &work,
                            QJsonObject record);
    QString fetch(const Entry &e, const QString &work);
    void restartHint();
    bool lock(QLockFile &l);
    int install(const QString &id, const QString &sourceDir, bool hint);
};

bool Runner::loadCatalog()
{
    QFile f(o.catalog);
    QString why;
    if (!f.open(QIODevice::ReadOnly)) why = f.errorString();
    else if (!parseCatalog(f.readAll(), &catalog, &why)) {}
    else return true;
    err << t(u"The plugin catalog can't be read (%1): %2"_s).arg(o.catalog, why) << Qt::endl;
    return false;
}

bool Runner::notRoot()
{
    if (::getuid() != 0) return true;
    err << t(u"Without sudo: plugins are installed into your own home folder"_s) << Qt::endl;
    return false;
}

// For git and cmake: nothing of your shell or your git configuration steers git (GIT_DIR and the like, url.*.insteadOf,
// filter drivers, fsmonitor); never ask for a password (the repository is public); only https or a local path,
// nothing that runs a command (ext::). (A proxy still works through http_proxy / https_proxy.)
QProcessEnvironment toolEnvironment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const QString &k : env.keys()) if (k.startsWith(u"GIT_"_s)) env.remove(k);
    env.insert(u"GIT_CONFIG_NOSYSTEM"_s, u"1"_s);
    env.insert(u"GIT_CONFIG_GLOBAL"_s, u"/dev/null"_s);
    env.insert(u"GIT_TERMINAL_PROMPT"_s, u"0"_s);
    env.insert(u"GIT_ALLOW_PROTOCOL"_s, u"https:file"_s);
    env.insert(u"LC_ALL"_s, u"C"_s);
    return env;
}

// Runs a program, its output into the log; the exit code, -1 if it didn't start or took too long
int Runner::exec(const QString &program, const QStringList &args, const QString &dir, int timeoutMs)
{
    QFile l(log);
    if (l.open(QIODevice::Append)) l.write((u"$ "_s + program + u' ' + args.join(u' ') + u'\n').toUtf8());
    l.close();
    QProcess p;
    p.setProcessEnvironment(toolEnvironment());
    // A group of its own: on a time-out the whole build goes (make / ninja and the compilers), not only cmake
    p.setChildProcessModifier([] { ::setpgid(0, 0); });
    p.setWorkingDirectory(dir);
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.setStandardOutputFile(log, QIODevice::Append);
    p.setStandardInputFile(QProcess::nullDevice());
    p.start(program, args);
    if (!p.waitForStarted(10000)) return -1;
    if (!p.waitForFinished(timeoutMs)) {
        if (p.processId() > 0) ::kill(-pid_t(p.processId()), SIGKILL);
        p.waitForFinished(5000);
        return -1;
    }
    return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
}

QString Runner::tailOfLog() const
{
    QFile l(log);
    if (!l.open(QIODevice::ReadOnly)) return QString();
    QStringList lines = QString::fromUtf8(l.readAll()).split(u'\n');
    while (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
    return u"    "_s + lines.mid(qMax(0, int(lines.size()) - 12)).join(u"\n    "_s);
}

// The pinned commit of the catalog's repository, only the plugin's folder: <work>/src/<id>. "" if it failed (said why)
QString Runner::fetch(const Entry &e, const QString &work)
{
    const QString git = QStandardPaths::findExecutable(u"git"_s);
    if (git.isEmpty()) { fail(t(u"git is needed to download plugins"_s)); return QString(); }
    const QString src = work + u"/src"_s;
    out << t(u"Downloading %1 (commit %2) from %3"_s).arg(e.id, e.commit.left(12), catalog.repository) << Qt::endl;
    const QStringList g{ u"-c"_s, u"core.hooksPath=/dev/null"_s, u"-C"_s, src };
    if (exec(git, { u"init"_s, u"-q"_s, src }, work, 30000) != 0
        || exec(git, g + QStringList{ u"remote"_s, u"add"_s, u"origin"_s, catalog.repository }, work, 30000) != 0
        // only that commit, without the file contents (they come with the checkout: only this folder's)
        || exec(git, g + QStringList{ u"-c"_s, u"fetch.fsckObjects=true"_s, u"fetch"_s, u"-q"_s, u"--depth=1"_s,
                                      u"--filter=blob:none"_s, u"origin"_s, e.commit }, work, 300000) != 0
        || exec(git, g + QStringList{ u"sparse-checkout"_s, u"set"_s, u"--no-cone"_s, u'/' + e.id + u'/' }, work, 30000) != 0
        || exec(git, g + QStringList{ u"checkout"_s, u"-q"_s, u"--detach"_s, e.commit }, work, 300000) != 0) {
        fail(t(u"Downloading failed (log: %1):"_s).arg(log) + u'\n' + tailOfLog());
        return QString();
    }
    // The commit id is a checksum of everything in it: what is checked out is exactly what the catalog names
    QProcess rp;
    rp.setProcessEnvironment(toolEnvironment());
    rp.start(git, g + QStringList{ u"rev-parse"_s, u"--verify"_s, u"HEAD^{commit}"_s });
    rp.waitForFinished(30000);
    const QString head = QString::fromUtf8(rp.readAllStandardOutput()).trimmed();
    if (head != e.commit) {
        fail(t(u"The download isn't the commit the catalog names (%1, not %2): not installed"_s).arg(head, e.commit));
        return QString();
    }
    return src + u'/' + e.id;
}

// Builds <srcDir> against the SDK and installs the file; "" when done, else why (already said)
QString Runner::buildAndInstall(const QString &srcDir, const QString &id, const QString &kind, const QString &work,
                                QJsonObject record)
{
    auto bad = [&](const QString &msg) { fail(msg); return msg; };
    QFile pj(srcDir + u"/plugin.json.in"_s);
    const QJsonObject meta = pj.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(pj.readAll()).object() : QJsonObject();
    if (!QFileInfo::exists(srcDir + u"/CMakeLists.txt"_s) || meta.value(u"id"_s).toString() != id)
        return bad(t(u"%1 has no plugin %2 (CMakeLists.txt, plugin.json.in with that id)"_s).arg(QDir(srcDir).absolutePath(), id));
    const QString wantKind = kind.isEmpty() ? meta.value(u"kind"_s).toString() : kind;
    const QString cmake = QStandardPaths::findExecutable(u"cmake"_s);
    if (cmake.isEmpty()) return bad(t(u"CMake is needed to build plugins"_s));
    if (!QFileInfo::exists(o.sdk + u"/SolwindSdkConfig.cmake"_s))
        return bad(t(u"Solwind's plugin SDK isn't there (%1): install Solwind with make install first"_s).arg(o.sdk));

    out << t(u"Building %1"_s).arg(id) << Qt::endl;
    const QString build = work + u"/build"_s;
    QStringList conf{ u"-S"_s, srcDir, u"-B"_s, build, u"-DCMAKE_BUILD_TYPE=Release"_s, u"-DSolwindSdk_DIR="_s + o.sdk };
    if (!QStandardPaths::findExecutable(u"ninja"_s).isEmpty()) conf << u"-G"_s << u"Ninja"_s;
    if (exec(cmake, conf, work, 300000) != 0 || exec(cmake, { u"--build"_s, build }, work, 1800000) != 0)
        return bad(t(u"Building failed (log: %1):"_s).arg(log) + u'\n' + tailOfLog());
    const QString built = build + u"/plugins/"_s + id + u".so"_s;

    // What solwindd will check, checked now (read from the file, it isn't loaded here)
    const QJsonObject m = QPluginLoader(built).metaData();
    const QJsonObject md = m.value(u"MetaData"_s).toObject();
    if (m.value(u"IID"_s).toString() != QLatin1String(SolwindPlugin_iid) || md.value(u"id"_s).toString() != id
        || md.value(u"kind"_s).toString() != wantKind || md.value(u"sdk"_s).toString() != QLatin1String(SOLWIND_SDK_ID))
        return bad(t(u"The built file isn't the plugin %1 for this Solwind (%2): not installed"_s).arg(id, built));

    // Into the plugin folder: only you can change it and its folders (solwindd refuses it otherwise); swapped in whole
    const QString brandDir = o.pluginDir + u'/' + id.section(u'/', 0, 0);
    const QFile::Permissions dirMode = QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup
                                     | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther;
    if (!QDir().mkpath(brandDir)) return bad(t(u"Can't make %1"_s).arg(brandDir));
    for (const QString &d : { o.pluginDir, brandDir }) QFile::setPermissions(d, dirMode);
    const QString dest = o.pluginDir + u'/' + id + u".so"_s;
    const QString recordFile = o.pluginDir + u'/' + id + u".json"_s;
    // The old record goes first: a file without a record counts as installed by hand, never as the old one
    if (QFileInfo::exists(recordFile) && !QFile::remove(recordFile)) return bad(t(u"Can't remove %1"_s).arg(recordFile));
    QFile::remove(dest + u".new"_s);
    if (!QFile::copy(built, dest + u".new"_s)
        || !QFile::setPermissions(dest + u".new"_s, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther)
        || ::rename(QFile::encodeName(dest + u".new"_s).constData(), QFile::encodeName(dest).constData()) != 0) {
        QFile::remove(dest + u".new"_s);
        return bad(t(u"Can't write %1"_s).arg(dest));
    }
    record.insert(u"id"_s, id);
    record.insert(u"sdk"_s, QLatin1String(SOLWIND_SDK_ID));
    record.insert(u"installed"_s, QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    // (what solwind plugin update compares with the catalog later)
    QSaveFile r(recordFile);
    if (!r.open(QIODevice::WriteOnly) || r.write(QJsonDocument(record).toJson()) < 0 || !r.commit())
        return bad(t(u"Can't write %1"_s).arg(recordFile));
    out << t(u"Installed %1 into %2"_s).arg(id, o.pluginDir) << Qt::endl;
    return QString();
}

void Runner::restartHint()
{
    // solwindd as a service has the systemd user manager's environment: another XDG_DATA_HOME means another folder
    QProcess p;
    p.start(u"systemctl"_s, { u"--user"_s, u"show-environment"_s });
    if (p.waitForFinished(5000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0) {
        QString data;
        for (const QString &line : QString::fromUtf8(p.readAllStandardOutput()).split(u'\n'))
            if (line.startsWith(u"XDG_DATA_HOME="_s)) data = line.mid(14);
        if (!data.startsWith(u'/')) data = QDir::homePath() + u"/.local/share"_s;
        if (QDir::cleanPath(data + u"/solwind/plugins"_s) != QDir::cleanPath(o.pluginDir))
            out << t(u"Note: Solwind's background service looks for plugins in %1 (its XDG_DATA_HOME differs from this shell's)"_s)
                       .arg(data + u"/solwind/plugins"_s) << Qt::endl;
    }
    out << t(u"solwindd loads plugins when it starts: solwind quit, then solwind"_s) << Qt::endl;
}

// The state of a catalog plugin here: none | current | update (another commit in the catalog) | sdk (built for another
// Solwind) | hand (no record) | local (from a local folder)
static QString stateOf(const std::optional<Installed> &i, const Entry &e)
{
    return !i ? u"none"_s : i->source.isEmpty() ? u"hand"_s : i->source == u"local"_s ? u"local"_s
         : i->sdk != QLatin1String(SOLWIND_SDK_ID) ? u"sdk"_s : i->commit != e.commit ? u"update"_s : u"current"_s;
}

int Runner::list(bool json)
{
    if (!loadCatalog()) return 1;
    const QList<Installed> have = installed(o.pluginDir);
    auto mine = [&](const QString &id) -> std::optional<Installed> {
        for (const Installed &i : have) if (i.id == id) return i;
        return std::nullopt;
    };
    if (json) {
        // For the window's Plugins page: { plugins: [{ id, kind, description, tested, state, fits: yes|no|"" }],
        // others: [ids installed but not in the catalog] }
        QJsonArray list, others;
        for (const Entry &e : catalog.plugins) {
            const Fit f = fits(e.fits, o.sysRoot);
            list.append(QJsonObject{ { u"id"_s, e.id }, { u"kind"_s, e.kind }, { u"description"_s, e.description },
                                     { u"tested"_s, e.tested }, { u"state"_s, stateOf(mine(e.id), e) },
                                     { u"fits"_s, f == Fit::Yes ? u"yes"_s : f == Fit::No ? u"no"_s : QString() } });
        }
        for (const Installed &i : have) if (!catalog.find(i.id)) others.append(i.id);
        out << QJsonDocument(QJsonObject{ { u"plugins"_s, list }, { u"others"_s, others } }).toJson(QJsonDocument::Compact) << Qt::endl;
        return 0;
    }
    for (const Entry &e : catalog.plugins) {
        const auto i = mine(e.id);
        QString state = !i ? t(u"not installed"_s)
                      : i->source.isEmpty() ? t(u"installed by hand (solwind plugin install %1 puts the catalog's in)"_s).arg(e.id)
                      : i->source == u"local"_s ? t(u"installed from a local folder"_s)
                      : i->sdk != QLatin1String(SOLWIND_SDK_ID) ? t(u"installed, built for another Solwind: solwind plugin update"_s)
                      : i->commit != e.commit ? t(u"installed, an update is in the catalog: solwind plugin update"_s)
                      : t(u"installed, up to date"_s);
        const Fit f = fits(e.fits, o.sysRoot);
        const QString fit = f == Fit::Yes ? t(u"fits this computer"_s) : f == Fit::No ? t(u"doesn't fit this computer"_s) : QString();
        out << e.id << u"  ("_s << t(e.kind == u"fan"_s ? u"fans"_s : u"keyboard light"_s) << u")\n"_s
            << u"    "_s << e.description << u'\n'
            << u"    "_s << state << (fit.isEmpty() ? QString() : u" · "_s + fit)
            << (e.tested.isEmpty() ? QString() : u" · "_s + t(u"tested on: %1"_s).arg(e.tested)) << Qt::endl;
    }
    for (const Installed &i : have)
        if (!catalog.find(i.id))
            out << i.id << u"\n    "_s << t(u"installed, not in the catalog (solwind plugin remove %1)"_s).arg(i.id) << Qt::endl;
    out << Qt::endl << t(u"Install one: solwind plugin install <name>"_s) << Qt::endl;
    return 0;
}

// One solwind plugin command that changes something at a time
bool Runner::lock(QLockFile &l)
{
    if (!QDir().mkpath(o.cacheDir)) { fail(t(u"Can't make %1"_s).arg(o.cacheDir)); return false; }
    QFile::setPermissions(o.cacheDir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    if (l.tryLock(0)) return true;
    fail(t(u"Another solwind plugin command is running"_s));
    return false;
}

int Runner::install(const QString &id, const QString &sourceDir) { return install(id, sourceDir, true); }

int Runner::install(const QString &id, const QString &sourceDir, bool hint)
{
    if (!notRoot()) return 1;
    if (!validId(id)) { err << t(u"Not a plugin name: %1 (solwind plugins lists them)"_s).arg(id) << Qt::endl; return 2; }
    const Entry *e = nullptr;
    if (sourceDir.isEmpty()) {
        if (!loadCatalog()) return 1;
        e = catalog.find(id);
        if (!e) { err << t(u"%1 isn't in the catalog (solwind plugins lists them)"_s).arg(id) << Qt::endl; return 2; }
    }
    // One at a time; everything of it under the cache folder, only yours
    QLockFile lk(o.cacheDir + u"/lock"_s);
    if (!lock(lk)) return 1;
    const QString work = o.cacheDir + u'/' + QString(id).replace(u'/', u'-');
    QDir(work).removeRecursively();
    if (!QDir().mkpath(work)) return fail(t(u"Can't make %1"_s).arg(work));
    log = work + u"/log.txt"_s;

    QJsonObject record;
    QString src;
    if (e) {
        src = fetch(*e, work);
        if (src.isEmpty()) return 1;
        record = { { u"source"_s, u"catalog"_s }, { u"commit"_s, e->commit }, { u"repository"_s, catalog.repository } };
    } else {
        src = QDir(sourceDir).absoluteFilePath(id);
        out << t(u"From a local folder, not checked against the catalog: %1"_s).arg(src) << Qt::endl;
        record = { { u"source"_s, u"local"_s }, { u"path"_s, src } };
    }
    if (!buildAndInstall(src, id, e ? e->kind : QString(), work, record).isEmpty()) return 1;
    QDir(work).removeRecursively();
    if (hint) restartHint();
    return 0;
}

int Runner::update(const QStringList &ids)
{
    if (!notRoot() || !loadCatalog()) return 1;
    for (const QString &id : ids)
        if (!validId(id)) { err << t(u"Not a plugin name: %1 (solwind plugins lists them)"_s).arg(id) << Qt::endl; return 2; }
    int done = 0, failed = 0;
    for (const Installed &i : installed(o.pluginDir)) {
        if (!ids.isEmpty() && !ids.contains(i.id)) continue;
        const Entry *e = catalog.find(i.id);
        if (!e) { out << t(u"%1: not in the catalog, left as it is"_s).arg(i.id) << Qt::endl; continue; }
        if (i.source == u"local"_s && !ids.contains(i.id)) {
            out << t(u"%1: installed from a local folder, left as it is (solwind plugin update %1 puts the catalog's in)"_s).arg(i.id) << Qt::endl;
            continue;
        }
        if (i.source == u"catalog"_s && i.commit == e->commit && i.sdk == QLatin1String(SOLWIND_SDK_ID)) continue;
        // a new commit, another Solwind, or not installed by this tool: the catalog's, built for this Solwind
        if (i.source.isEmpty()) out << t(u"%1: installed by hand, replaced by the catalog's"_s).arg(i.id) << Qt::endl;
        Runner one(o, i18n, out, err);
        if (one.install(i.id, QString(), false) == 0) ++done; else ++failed;
    }
    for (const QString &id : ids)
        if (!QFileInfo::exists(o.pluginDir + u'/' + id + u".so"_s)) {
            err << t(u"%1 isn't installed"_s).arg(id) << Qt::endl;
            ++failed;
        }
    if (!done && !failed) out << t(u"Everything is up to date"_s) << Qt::endl;
    if (done) restartHint();
    return failed ? 1 : 0;
}

int Runner::remove(const QString &id)
{
    if (!notRoot()) return 1;
    if (!validId(id)) { err << t(u"Not a plugin name: %1 (solwind plugins lists them)"_s).arg(id) << Qt::endl; return 2; }
    QLockFile lk(o.cacheDir + u"/lock"_s);
    if (!lock(lk)) return 1;
    const QString so = o.pluginDir + u'/' + id + u".so"_s;
    if (!QFileInfo::exists(so)) return fail(t(u"%1 isn't installed"_s).arg(id));
    if (!QFile::remove(so)) return fail(t(u"Can't remove %1"_s).arg(so));
    QFile::remove(o.pluginDir + u'/' + id + u".json"_s);
    QDir(o.pluginDir).rmdir(id.section(u'/', 0, 0));     // (only if empty)
    out << t(u"Removed %1. It stays loaded until solwindd starts again: solwind quit, then solwind"_s).arg(id) << Qt::endl;
    return 0;
}
} // namespace

int run(const QStringList &args, const Options &options, const I18n &i18n, QTextStream &out, QTextStream &err)
{
    Runner r(options, i18n, out, err);
    const QString cmd = args.value(0);
    const QStringList rest = args.mid(1);
    auto usage = [&] {
        err << i18n.tIn(options.lang, u"Usage: solwind plugin list | install <name> | install --source <folder> <name> | update [<name>...] | remove <name>"_s)
            << Qt::endl;
        return 2;
    };
    if (cmd == u"list"_s && rest.isEmpty()) return r.list(false);
    if (cmd == u"list"_s && rest == QStringList{ u"--json"_s }) return r.list(true);
    if (cmd == u"install"_s && rest.size() == 1) return r.install(rest[0], QString());
    if (cmd == u"install"_s && rest.size() == 3 && rest[0] == u"--source"_s && !rest[1].isEmpty()) return r.install(rest[2], rest[1]);
    if (cmd == u"update"_s) return r.update(rest);
    if (cmd == u"remove"_s && rest.size() == 1) return r.remove(rest[0]);
    return usage();
}
}
