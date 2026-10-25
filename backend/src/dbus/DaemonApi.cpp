#include "DaemonApi.h"

#include "ClientHints.h"

#include "core/Paths.h"
#include "core/Settings.h"

#include <QCoreApplication>
#include <QDBusMessage>
#include <QTimer>

using namespace Qt::StringLiterals;

DaemonApi::DaemonApi(const Paths &paths, Settings &settings, bool settingsLoaded, ClientHints *hints, QObject *parent)
    : QObject(parent), m_paths(paths), m_settings(settings), m_settingsLoaded(settingsLoaded), m_hints(hints)
{
}

QString DaemonApi::version() const { return QStringLiteral(SOLWIND_VERSION); }

QVariantMap DaemonApi::Status() const
{
    QVariantMap m{
        { u"version"_s, version() },
        { u"pid"_s, QCoreApplication::applicationPid() },
        { u"settingsPath"_s, m_settings.path() },
        { u"settingsLoaded"_s, m_settingsLoaded },
        { u"sysRoot"_s, m_paths.sysRoot },
        { u"mode"_s, u"observe"_s },
    };
    if (extraStatus) m.insert(extraStatus());
    return m;
}

void DaemonApi::Watch(const QString &topic)
{
    if (!calledFromDBus() || !m_hints) return;
    if (!ClientHints::validTopic(topic)) {
        sendErrorReply(QDBusError::InvalidArgs, u"unknown topic: "_s + topic + u" (window | fans)"_s);
        return;
    }
    m_hints->watch(message().service(), topic);
}

void DaemonApi::Unwatch(const QString &topic)
{
    if (!calledFromDBus() || !m_hints) return;
    if (!ClientHints::validTopic(topic)) {
        sendErrorReply(QDBusError::InvalidArgs, u"unknown topic: "_s + topic + u" (window | fans)"_s);
        return;
    }
    m_hints->unwatch(message().service(), topic);
}

void DaemonApi::Quit()
{
    // Queued: the D-Bus reply to this call goes out first
    QTimer::singleShot(0, this, &DaemonApi::quitRequested);
}
