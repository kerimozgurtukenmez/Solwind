#include "BusGuard.h"

#include <QDBusConnectionInterface>

using namespace Qt::StringLiterals;

BusGuard::BusGuard(QDBusConnection bus, const QString &name, QObject *parent)
    : QObject(parent), m_bus(bus), m_name(name)
{
    // libdbus reports a broken connection as this local signal
    m_bus.connect(QString(), u"/org/freedesktop/DBus/Local"_s, u"org.freedesktop.DBus.Local"_s, u"Disconnected"_s,
                  this, SLOT(disconnected()));
    // NameLost: this connection no longer owns the name
    connect(m_bus.interface(), &QDBusConnectionInterface::serviceUnregistered, this, &BusGuard::nameLost);
}

void BusGuard::disconnected() { emit lost(u"the D-Bus session bus went away"_s); }

void BusGuard::nameLost(const QString &name)
{
    if (name == m_name) emit lost(u"lost the D-Bus name "_s + name);
}
