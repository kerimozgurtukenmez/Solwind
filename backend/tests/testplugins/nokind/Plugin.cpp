// A plugin file whose metadata has no kind: solwindd must refuse it (tst_daemon)
#include "plugins/SolwindPlugin.h"

class NoKindPlugin : public QObject, public SolwindPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SolwindPlugin_iid FILE "plugin.json")
    Q_INTERFACES(SolwindPlugin)
};

#include "Plugin.moc"
