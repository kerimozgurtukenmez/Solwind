#pragma once

#include <QDBusConnection>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantMap>

using VariantMapList = QList<QVariantMap>;   // D-Bus aa{sv}
using DoubleList = QList<double>;            // D-Bus ad

namespace DBusUtil {

// Registers the D-Bus types above (before any object is registered)
void registerTypes();

// Equal for change signals: NaN equals NaN, lists and maps compared item by item
bool same(const QVariant &a, const QVariant &b);

// All readable properties of an exported object, by name
QVariantMap properties(const QObject *o);

} // namespace DBusUtil

// Sends org.freedesktop.DBus.Properties.PropertiesChanged for the properties of one object that changed since the
// last call (QtDBus doesn't send it by itself), so clients follow the state without polling.
class PropertyPublisher
{
public:
    PropertyPublisher(QDBusConnection bus, QString path, QString iface, const QObject *api);
    void publish();

private:
    QDBusConnection m_bus;
    QString m_path, m_iface;
    const QObject *m_api;
    QVariantMap m_last;
};
