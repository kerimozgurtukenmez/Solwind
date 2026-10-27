#pragma once

#include <QList>
#include <QString>
#include <QStringList>

class FanPlugin;
class KeyboardPlugin;
class PluginHost;
class QObject;
class QPluginLoader;
class SolwindPlugin;

// Loads the plugin files (SolwindPlugin.h) at start. Folders, in order: $SOLWIND_PLUGIN_PATH (colon separated; tests,
// development) when set; else <solwindd's own folder>/plugins and $XDG_DATA_HOME/solwind/plugins
// (~/.local/share/solwind/plugins). Every *.so below them (any depth: by brand) is tried; a plugin id seen before is
// skipped (the first folder wins). Not loaded, and reported: another interface version, no plugin metadata, built for
// another SDK (its "sdk": the Solwind version and a hash of the SDK headers, SOLWIND_SDK_ID), a link, a file another
// user could change, or one in a folder (up to the plugin folder) another user could change (writable by group /
// others, or owned by someone else than you or root).
class PluginLoader
{
public:
    struct Plugin {
        QString id, brand, module, kind, version, description, file;
        int priority = 100;
        bool used = false;                 // its fan plugin drives the fans (only one does) / it has keyboard plugins
        SolwindPlugin *instance = nullptr;
    };
    struct Failure { QString file, why; };

    static QStringList defaultFolders();
    ~PluginLoader();
    void load(const QStringList &folders);

    const QList<Plugin> &plugins() const { return m_plugins; }   // by priority, then id
    const QList<Failure> &failures() const { return m_failures; }
    // The keyboard plugins of every file, in the order they are asked (priority)
    QList<const KeyboardPlugin *> keyboardPlugins();
    // The fan plugin of the first fan plugin file (priority) that makes one; nullptr if none. `id` gets its plugin id.
    // Owned by the caller (parent, or none).
    FanPlugin *createFan(const PluginHost &host, QObject *parent, QString *id);

private:
    QList<Plugin> m_plugins;
    QList<Failure> m_failures;
    QList<QPluginLoader *> m_loaders;      // kept: a plugin is never unloaded while solwindd runs
};
