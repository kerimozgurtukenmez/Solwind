// The plugin file solwindd loads (NBFC/nbfc.so)
#include "NbfcPlugin.h"
#include "plugins/SolwindPlugin.h"

class NbfcPluginFile : public QObject, public SolwindPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SolwindPlugin_iid FILE "plugin.json")
    Q_INTERFACES(SolwindPlugin)
public:
    FanPlugin *createFan(const PluginHost &host, QObject *parent) override { return new NbfcPlugin(host, parent); }
};

#include "Plugin.moc"
