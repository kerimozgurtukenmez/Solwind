#pragma once

#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QMultiHash>
#include <QObject>
#include <QSet>
#include <QString>

// What clients are showing, so the daemon reads as often as today's app did (SPEC.md P-2) and no more:
//   "window"  a window is open    -> NBFC every 2 s, sensors in fast mode
//   "fans"    the fan page is open -> sensors are read
// A client says so with Watch(topic) and stops with Unwatch(topic). If it goes away (closed, crashed), its topics
// are dropped by themselves: a crashed UI can't leave the daemon polling fast forever.
class ClientHints : public QObject
{
    Q_OBJECT
public:
    explicit ClientHints(QDBusConnection bus, QObject *parent = nullptr);

    static bool validTopic(const QString &topic);
    void watch(const QString &client, const QString &topic);
    void unwatch(const QString &client, const QString &topic);
    bool has(const QString &topic) const;

signals:
    void changed();

private:
    void drop(const QString &client);

    QDBusServiceWatcher m_watcher;
    QMultiHash<QString, QString> m_byClient;     // client bus name -> topic
};
