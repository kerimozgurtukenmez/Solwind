// solwind-plugin: Solwind's plugin catalog, downloading and building plugins (PluginTool.h). Run through the solwind
// command (solwind plugins, solwind plugin ...), which gives it the catalog and the message language.
//   solwind-plugin [--catalog <plugin-catalog.json>] [--sdk <folder of SolwindSdkConfig.cmake>] <command> ...
#include "PluginTool.h"
#include "core/I18n.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>

#include <cstdio>

using namespace Qt::StringLiterals;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStringList args = QCoreApplication::arguments().mid(1);
    const QString here = QCoreApplication::applicationDirPath();
    PluginTool::Options o;
    // Installed next to solwindd (<prefix>/lib/solwind): the SDK package in <prefix>/lib/cmake/SolwindSdk, the catalog
    // with the app in <prefix>/share/solwind. In a build folder: its sdk/ and the source tree's catalog.
    o.sdk = QFileInfo::exists(here + u"/../cmake/SolwindSdk/SolwindSdkConfig.cmake"_s) ? here + u"/../cmake/SolwindSdk"_s
                                                                                        : here + u"/sdk"_s;
    o.catalog = QFileInfo::exists(here + u"/../../share/solwind/plugin-catalog.json"_s)
                    ? here + u"/../../share/solwind/plugin-catalog.json"_s
                    : QStringLiteral(SOLWIND_SOURCE_APP_DIR) + u"/plugin-catalog.json"_s;
    while (args.size() >= 2 && (args[0] == u"--catalog"_s || args[0] == u"--sdk"_s)) {
        (args[0] == u"--catalog"_s ? o.catalog : o.sdk) = args[1];
        args = args.mid(2);
    }
    // Where solwindd looks (its own folder in your home: PluginLoader) and where plugins are built
    o.pluginDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u"/solwind/plugins"_s;
    o.cacheDir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + u"/solwind/plugin-build"_s;
    o.sysRoot = qEnvironmentVariable("SOLWIND_SYS");
    const QString lang = qEnvironmentVariable("SOLWIND_LANG");
    o.lang = lang == u"tr"_s ? lang : u"en"_s;

    I18n i18n;
    QTextStream out(stdout), err(stderr);
    return PluginTool::run(args, o, i18n, out, err);
}
