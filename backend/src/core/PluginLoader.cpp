#include "PluginLoader.h"

#include "plugins/SolwindPlugin.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonObject>
#include <QPluginLoader>
#include <QStandardPaths>

#include <algorithm>
#include <sys/stat.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace {
// A plugin runs inside solwindd: nobody but you (or root, for an installed one) may be able to change it or a folder
// it is in. `what`: "it" for the file, the folder's path for a folder.
QString unsafe(const QString &path, const QString &what)
{
    struct stat st{};
    if (::stat(QFile::encodeName(path).constData(), &st) != 0) return u"%1 can't be read"_s.arg(what);
    if (st.st_uid != ::getuid() && st.st_uid != 0) return u"%1 belongs to another user"_s.arg(what);
    if (st.st_mode & (S_IWGRP | S_IWOTH)) return u"others can change %1"_s.arg(what);
    return QString();
}
// The file itself, never a link: the folders a link points into aren't checked
bool isLink(const QString &path)
{
    struct stat st{};
    return ::lstat(QFile::encodeName(path).constData(), &st) == 0 && S_ISLNK(st.st_mode);
}
}

QStringList PluginLoader::defaultFolders()
{
    const QString env = qEnvironmentVariable("SOLWIND_PLUGIN_PATH");
    if (!env.isEmpty()) return env.split(u':', Qt::SkipEmptyParts);
    return { QCoreApplication::applicationDirPath() + u"/plugins"_s,
             QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u"/solwind/plugins"_s };
}

PluginLoader::~PluginLoader() = default;   // (the loaders are never unloaded: the plugins' objects live until exit)

void PluginLoader::load(const QStringList &folders)
{
    QStringList seen;
    for (const QString &folder : folders) {
        if (!QFileInfo(folder).isDir()) continue;
        const QString root = QDir(folder).absolutePath();
        QStringList files;
        QDirIterator it(root, { u"*.so"_s }, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) files << it.next();
        files.sort();
        for (const QString &file : files) {
            // the file, its folder and every folder above it up to the plugin folder
            QString why = isLink(file) ? u"a link (install the file itself)"_s : unsafe(file, u"it"_s);
            for (QString dir = QFileInfo(file).absolutePath(); why.isEmpty() && dir.startsWith(root); dir = QFileInfo(dir).path()) {
                why = unsafe(dir, u"its folder "_s + dir);
                if (dir == root) break;
            }
            if (!why.isEmpty()) { m_failures.append({ file, why }); continue; }
            auto *loader = new QPluginLoader(file);
            const QJsonObject meta = loader->metaData();
            if (meta.value(u"IID"_s).toString() != QLatin1String(SolwindPlugin_iid)) {
                m_failures.append({ file, meta.isEmpty() ? u"not a Solwind plugin"_s
                                                         : u"made for another plugin interface (%1)"_s.arg(meta.value(u"IID"_s).toString()) });
                delete loader;
                continue;
            }
            const QJsonObject md = meta.value(u"MetaData"_s).toObject();
            Plugin p;
            p.id = md.value(u"id"_s).toString();
            p.brand = md.value(u"brand"_s).toString();
            p.module = md.value(u"module"_s).toString();
            p.kind = md.value(u"kind"_s).toString();
            p.version = md.value(u"version"_s).toString();
            p.description = md.value(u"description"_s).toString();
            p.priority = md.value(u"priority"_s).toInt(100);
            p.file = file;
            if (p.id.isEmpty() || (p.kind != u"fan"_s && p.kind != u"keyboard"_s)) {
                m_failures.append({ file, u"no id or kind in its metadata"_s });
                delete loader;
                continue;
            }
            // Built against another SDK (another version, or changed headers: its classes may differ): never loaded,
            // it must be built again
            const QString sdk = md.value(u"sdk"_s).toString();
            if (sdk != QLatin1String(SOLWIND_SDK_ID)) {
                m_failures.append({ file, u"built for Solwind %1, this is %2: build it again"_s
                                              .arg(sdk.isEmpty() ? u"?"_s : sdk, QLatin1String(SOLWIND_SDK_ID)) });
                delete loader;
                continue;
            }
            if (seen.contains(p.id)) { m_failures.append({ file, u"%1 is loaded already from another folder"_s.arg(p.id) }); delete loader; continue; }
            p.instance = qobject_cast<SolwindPlugin *>(loader->instance());
            if (!p.instance) {
                m_failures.append({ file, loader->errorString() });
                delete loader;
                continue;
            }
            seen << p.id;
            m_loaders << loader;
            m_plugins << p;
        }
    }
    std::stable_sort(m_plugins.begin(), m_plugins.end(), [](const Plugin &a, const Plugin &b) {
        return a.priority != b.priority ? a.priority < b.priority : a.id < b.id;
    });
}

QList<const KeyboardPlugin *> PluginLoader::keyboardPlugins()
{
    QList<const KeyboardPlugin *> out;
    for (Plugin &p : m_plugins) {
        if (p.kind != u"keyboard"_s) continue;
        const QList<const KeyboardPlugin *> mine = p.instance->keyboardPlugins();
        p.used = !mine.isEmpty();
        out << mine;
    }
    return out;
}

FanPlugin *PluginLoader::createFan(const PluginHost &host, QObject *parent, QString *id)
{
    for (Plugin &p : m_plugins) {
        if (p.kind != u"fan"_s) continue;
        if (FanPlugin *f = p.instance->createFan(host, parent)) {
            p.used = true;
            if (id) *id = p.id;
            return f;
        }
    }
    return nullptr;
}
