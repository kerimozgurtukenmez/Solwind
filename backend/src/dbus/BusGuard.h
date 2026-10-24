#pragma once

#include <QDBusConnection>
#include <QObject>

// Watches the two things that make this copy "the" solwindd: the session bus connection and the D-Bus name.
// If either goes (the bus restarted, the name was taken away), another copy could claim the name and drive the fans
// too, so the daemon must quit (cleanly) rather than carry on unseen.
class BusGuard : public QObject
{
    Q_OBJECT
public:
    BusGuard(QDBusConnection bus, const QString &name, QObject *parent = nullptr);

signals:
    void lost(const QString &why);

private slots:
    void disconnected();
    void nameLost(const QString &name);

private:
    QDBusConnection m_bus;
    QString m_name;
};
