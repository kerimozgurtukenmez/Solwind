#pragma once

#include <QList>
#include <QObject>
#include <QtPlugin>

class FanPlugin;
class KeyboardPlugin;
class PluginHost;

// A Solwind plugin file (SDK): a shared library solwindd loads at start from its plugin folders
// (<solwindd's folder>/plugins and ~/.local/share/solwind/plugins, by brand: HP/hp-wmi-keyboard.so ...). Its class is a
// QObject implementing this interface:
//
//     class MyPlugin : public QObject, public SolwindPlugin {
//         Q_OBJECT
//         Q_PLUGIN_METADATA(IID SolwindPlugin_iid FILE "plugin.json")
//         Q_INTERFACES(SolwindPlugin)
//     ...
//
// plugin.json: { "id": "HP/hp-wmi-keyboard", "brand": "HP", "module": "<the kernel module / service it is for>",
//                "kind": "fan" | "keyboard", "version": "1.0.0", "priority": <lower = asked first>,
//                "description": "<one line>" }
// The IID carries the interface version: a plugin built for another one isn't loaded. Plugins run inside solwindd;
// every safety rule (the safety limit, checking a speed took, handing fans back, trust levels) stays in the core.
class SolwindPlugin
{
public:
    virtual ~SolwindPlugin() = default;
    // Its fan plugin, made once and owned by the caller (parent); nullptr if this file has none
    virtual FanPlugin *createFan(const PluginHost &host, QObject *parent)
    {
        Q_UNUSED(host)
        Q_UNUSED(parent)
        return nullptr;
    }
    // Its keyboard light plugins (owned by this object, alive while it is loaded)
    virtual QList<const KeyboardPlugin *> keyboardPlugins() const { return {}; }
};

#define SolwindPlugin_iid "solwind.Plugin/1"
Q_DECLARE_INTERFACE(SolwindPlugin, SolwindPlugin_iid)
