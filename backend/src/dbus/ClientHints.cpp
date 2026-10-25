#include "ClientHints.h"

#include <QDBusConnectionInterface>

using namespace Qt::StringLiterals;

ClientHints::ClientHints(QDBusConnection bus, QObject *parent)
    : QObject(parent), m_watcher(QString(), bus, QDBusServiceWatcher::WatchForUnregistration)
{
    connect(&m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, &ClientHints::drop);
}

bool ClientHints::validTopic(const QString &topic) { return topic == u"window"_s || topic == u"fans"_s; }

void ClientHints::watch(const QString &client, const QString &topic)
{
    if (!validTopic(topic) || m_byClient.contains(client, topic)) return;
    const bool before = has(topic);
    const bool fresh = !m_byClient.contains(client);
    if (fresh) m_watcher.addWatchedService(client);
    m_byClient.insert(client, topic);
    if (!before) emit changed();
    // Gone between its call and the watch: it would never be dropped
    if (fresh && m_watcher.connection().interface() && !m_watcher.connection().interface()->isServiceRegistered(client))
        drop(client);
}

void ClientHints::unwatch(const QString &client, const QString &topic)
{
    if (!m_byClient.contains(client, topic)) return;
    const bool before = has(topic);
    m_byClient.remove(client, topic);
    if (!m_byClient.contains(client)) m_watcher.removeWatchedService(client);
    if (before != has(topic)) emit changed();
}

void ClientHints::drop(const QString &client)
{
    if (!m_byClient.contains(client)) return;
    const bool window = has(u"window"_s), fans = has(u"fans"_s);
    m_byClient.remove(client);
    m_watcher.removeWatchedService(client);
    if (window != has(u"window"_s) || fans != has(u"fans"_s)) emit changed();
}

bool ClientHints::has(const QString &topic) const
{
    for (auto it = m_byClient.begin(); it != m_byClient.end(); ++it) if (it.value() == topic) return true;
    return false;
}
