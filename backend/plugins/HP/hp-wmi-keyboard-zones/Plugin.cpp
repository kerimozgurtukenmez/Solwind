// The plugin file solwindd loads (HP/hp-wmi-keyboard-zones.so)
#include "HpWmiKeyboardZones.h"
#include "plugins/SolwindPlugin.h"

class HpWmiKeyboardZonesPlugin : public QObject, public SolwindPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SolwindPlugin_iid FILE "plugin.json")
    Q_INTERFACES(SolwindPlugin)
public:
    QList<const KeyboardPlugin *> keyboardPlugins() const override { return { &m_light }; }

private:
    HpWmiKeyboardZones m_light;
};

#include "Plugin.moc"
