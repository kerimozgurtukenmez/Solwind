#pragma once

#include <QList>
#include <QString>
#include <QStringList>

class I18n;
class QTextStream;

// solwind-plugin: the plugins of Solwind's catalog (plugin-catalog.json, in Solwind's repository: each plugin with a
// pinned commit of the Solwind-plugins repository). Lists them, and installs one as the user: git fetches exactly
// that commit, only the plugin's folder (partial + sparse; the commit id is checked), CMake builds it against the
// installed SDK, the file goes into ~/.local/share/solwind/plugins. solwindd never downloads or builds anything.
namespace PluginTool {

// When a plugin fits this machine (read only). Every key given must hold; within a key, any one entry is enough.
struct Fits {
    QStringList programs;                 // on PATH (NBFC: nbfc)
    struct Led { QString name, driver; }; // /sys/class/leds/<name glob> whose device/driver is <driver> ("" = any)
    QList<Led> leds;
    QStringList paths;                    // exist (under /sys)
    QStringList vendors;                  // in the DMI sys_vendor (any case)
    bool empty() const { return programs.isEmpty() && leds.isEmpty() && paths.isEmpty() && vendors.isEmpty(); }
};

struct Entry {
    QString id, kind, description, tested, commit;
    Fits fits;
};

struct Catalog {
    QString repository;                   // https:// (file:// in tests)
    QList<Entry> plugins;
    const Entry *find(const QString &id) const;
};

bool validId(const QString &id);          // <Brand>/<name>: letters, digits, _ . + -; neither part starts with . or -
bool validCommit(const QString &commit);  // a full commit id: 40 lower-case hex digits (SHA-1)
bool validRepository(const QString &url); // https://host/path or file:///path, nothing that could run a command
// Reads a catalog (version 1); false and why if anything in it is wrong (nothing of it is used then)
bool parseCatalog(const QByteArray &json, Catalog *out, QString *why);

// Does the plugin fit this machine? (`sysRoot`: "" = the real /sys, as SOLWIND_SYS)
enum class Fit { Unknown, Yes, No };
Fit fits(const Fits &f, const QString &sysRoot);

// A plugin file in the plugin folder, with what solwind-plugin recorded when it installed it (<id>.json next to it)
struct Installed {
    QString id, commit, repository, sdk, source;   // source: catalog | local | "" (no record: installed by hand)
    bool enabled = true;                           // false: <id>.disabled next to it, solwindd doesn't load it
};
QList<Installed> installed(const QString &pluginDir);

struct Options {
    QString catalog;                      // plugin-catalog.json
    QString sdk;                          // the folder with SolwindSdkConfig.cmake
    QString pluginDir;                    // where plugins go (solwindd's folder in your home)
    QString cacheDir;                     // where they are downloaded and built
    QString sysRoot;                      // SOLWIND_SYS (tests)
    QString lang;                         // tr | en
};
// The command line: list | install [--source <dir>] <id> | update [<id>...] | remove <id> | enable <id> | disable <id>.
// Exit: 0 done, 1 failed,
// 2 wrong usage.
int run(const QStringList &args, const Options &options, const I18n &i18n, QTextStream &out, QTextStream &err);
}
