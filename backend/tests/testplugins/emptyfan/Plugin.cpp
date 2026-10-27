// A fan plugin file that makes no fan plugin (createFan: nullptr): loaded, not used, the fans stay without one
// (tst_daemon)
#include "plugins/SolwindPlugin.h"

class EmptyFanPlugin : public QObject, public SolwindPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SolwindPlugin_iid FILE "plugin.json")
    Q_INTERFACES(SolwindPlugin)
};

#include "Plugin.moc"
