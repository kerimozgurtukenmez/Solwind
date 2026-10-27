// A plugin file built for another SDK (its metadata: sdk 0.0.0): solwindd must refuse it (tst_daemon)
#include "plugins/SolwindPlugin.h"

class OldSdkPlugin : public QObject, public SolwindPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SolwindPlugin_iid FILE "plugin.json")
    Q_INTERFACES(SolwindPlugin)
};

#include "Plugin.moc"
