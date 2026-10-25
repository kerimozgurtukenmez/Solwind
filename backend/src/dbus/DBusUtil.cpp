#include "DBusUtil.h"

#include <QDBusMessage>
#include <QDBusMetaType>
#include <QMetaProperty>

#include <cmath>

using namespace Qt::StringLiterals;

namespace DBusUtil {

void registerTypes()
{
    qDBusRegisterMetaType<VariantMapList>();
    qDBusRegisterMetaType<DoubleList>();
}

bool same(const QVariant &a, const QVariant &b)
{
    if (a.metaType() != b.metaType()) return false;
    const int t = a.metaType().id();
    if (t == QMetaType::Double) {
        const double x = a.toDouble(), y = b.toDouble();
        return x == y || (std::isnan(x) && std::isnan(y));
    }
    if (t == QMetaType::QVariantMap) {
        const QVariantMap x = a.toMap(), y = b.toMap();
        if (x.keys() != y.keys()) return false;
        for (auto it = x.begin(); it != x.end(); ++it) if (!same(it.value(), y.value(it.key()))) return false;
        return true;
    }
    if (t == QMetaType::QVariantList) {
        const QVariantList x = a.toList(), y = b.toList();
        if (x.size() != y.size()) return false;
        for (qsizetype i = 0; i < x.size(); ++i) if (!same(x[i], y[i])) return false;
        return true;
    }
    if (t == QMetaType::fromType<VariantMapList>().id()) {
        const auto x = a.value<VariantMapList>(), y = b.value<VariantMapList>();
        if (x.size() != y.size()) return false;
        for (qsizetype i = 0; i < x.size(); ++i) if (!same(x[i], y[i])) return false;
        return true;
    }
    if (t == QMetaType::fromType<DoubleList>().id()) {
        const auto x = a.value<DoubleList>(), y = b.value<DoubleList>();
        if (x.size() != y.size()) return false;
        for (qsizetype i = 0; i < x.size(); ++i) if (!(x[i] == y[i] || (std::isnan(x[i]) && std::isnan(y[i])))) return false;
        return true;
    }
    return a == b;
}

QVariantMap properties(const QObject *o)
{
    QVariantMap m;
    const QMetaObject *mo = o->metaObject();
    for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
        const QMetaProperty p = mo->property(i);
        if (p.isReadable()) m.insert(QString::fromLatin1(p.name()), p.read(o));
    }
    return m;
}

} // namespace DBusUtil

PropertyPublisher::PropertyPublisher(QDBusConnection bus, QString path, QString iface, const QObject *api)
    : m_bus(bus), m_path(std::move(path)), m_iface(std::move(iface)), m_api(api), m_last(DBusUtil::properties(api))
{
}

void PropertyPublisher::publish()
{
    const QVariantMap now = DBusUtil::properties(m_api);
    QVariantMap changed;
    for (auto it = now.begin(); it != now.end(); ++it)
        if (!DBusUtil::same(it.value(), m_last.value(it.key()))) changed.insert(it.key(), it.value());
    m_last = now;
    if (changed.isEmpty()) return;
    QDBusMessage sig = QDBusMessage::createSignal(m_path, u"org.freedesktop.DBus.Properties"_s, u"PropertiesChanged"_s);
    sig << m_iface << changed << QStringList();
    m_bus.send(sig);
}
