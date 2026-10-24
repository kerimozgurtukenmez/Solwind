#include "DaemonApi.h"

#include "core/Paths.h"
#include "core/Settings.h"

#include <QCoreApplication>
#include <QTimer>

using namespace Qt::StringLiterals;

DaemonApi::DaemonApi(const Paths &paths, Settings &settings, bool settingsLoaded, QObject *parent)
    : QObject(parent), m_paths(paths), m_settings(settings), m_settingsLoaded(settingsLoaded)
{
}

QString DaemonApi::version() const { return QStringLiteral(SOLWIND_VERSION); }

QVariantMap DaemonApi::Status() const
{
    return {
        { u"version"_s, version() },
        { u"pid"_s, QCoreApplication::applicationPid() },
        { u"settingsPath"_s, m_settings.path() },
        { u"settingsLoaded"_s, m_settingsLoaded },
        { u"sysRoot"_s, m_paths.sysRoot },
    };
}

void DaemonApi::Quit()
{
    // Queued: the D-Bus reply to this call goes out first
    QTimer::singleShot(0, this, &DaemonApi::quitRequested);
}
