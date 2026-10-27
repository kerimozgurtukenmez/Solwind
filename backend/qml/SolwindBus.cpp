#include "SolwindBus.h"

#include <QDBusArgument>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QDBusVariant>
#include <QJSEngine>

using namespace Qt::StringLiterals;

namespace {
const QString service = u"solwind.Daemon"_s;
const QString propsIface = u"org.freedesktop.DBus.Properties"_s;

QVariant fromArgument(const QDBusArgument &a)
{
    switch (a.currentType()) {
    case QDBusArgument::BasicType:
    case QDBusArgument::VariantType:
        return SolwindBus::toQml(a.asVariant());
    case QDBusArgument::ArrayType: {
        QVariantList l;
        a.beginArray();
        while (!a.atEnd()) l << fromArgument(a);
        a.endArray();
        return l;
    }
    case QDBusArgument::MapType: {
        QVariantMap m;
        a.beginMap();
        while (!a.atEnd()) {
            a.beginMapEntry();
            const QVariant k = fromArgument(a);
            const QVariant v = fromArgument(a);
            a.endMapEntry();
            m.insert(k.toString(), v);
        }
        a.endMap();
        return m;
    }
    case QDBusArgument::StructureType: {
        QVariantList l;
        a.beginStructure();
        while (!a.atEnd()) l << fromArgument(a);
        a.endStructure();
        return l;
    }
    default:
        return QVariant();
    }
}
} // namespace

SolwindBus::SolwindBus(QObject *parent)
    : QObject(parent), m_bus(QDBusConnection::sessionBus()),
      m_watcher(service, m_bus, QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration)
{
    m_objects = { { u"/solwind"_s, u"solwind.Daemon"_s, &m_daemon }, { u"/solwind/Fans"_s, u"solwind.Fans"_s, &m_fans },
                  { u"/solwind/Sensors"_s, u"solwind.Sensors"_s, &m_sensors },
                  { u"/solwind/Keyboard"_s, u"solwind.Keyboard"_s, &m_keyboard } };
    for (const Object &o : m_objects)
        m_bus.connect(service, o.path, propsIface, u"PropertiesChanged"_s, this,
                      SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));
    m_bus.connect(service, u"/solwind/Fans"_s, u"solwind.Fans"_s, u"Sampled"_s, this, SLOT(onSampled(QVariantMap)));
    connect(&m_watcher, &QDBusServiceWatcher::serviceRegistered, this, &SolwindBus::appeared);
    connect(&m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, &SolwindBus::gone);
    m_retry.setSingleShot(true);
    m_retry.setInterval(1000);
    connect(&m_retry, &QTimer::timeout, this, [this] {
        if (m_bus.isConnected() && m_bus.interface()->isServiceRegistered(service)) appeared();
    });
    if (m_bus.isConnected() && m_bus.interface()->isServiceRegistered(service)) appeared();
}

SolwindBus *SolwindBus::create(QQmlEngine *qml, QJSEngine *)
{
    auto *b = new SolwindBus(nullptr);
    b->m_engine = qml;
    if (qml) {
        connect(qml, &QQmlEngine::quit, b, &SolwindBus::close);
        connect(qml, &QQmlEngine::exit, b, &SolwindBus::close);
    }
    return b;
}

const SolwindBus::Object *SolwindBus::object(const QString &name) const
{
    static const QHash<QString, int> index{ { u"daemon"_s, 0 }, { u"fans"_s, 1 }, { u"sensors"_s, 2 }, { u"keyboard"_s, 3 } };
    const auto it = index.constFind(name);
    return it == index.cend() ? nullptr : &m_objects[*it];
}

void SolwindBus::appeared()
{
    if (m_closing) return;
    bool all = true;
    for (const Object &o : m_objects) all = readAll(o) && all;
    if (!all) {
        qWarning("[solwind] could not read solwindd's state; trying again in a second");
        m_retry.start();
        return;
    }
    for (const QString &t : std::as_const(m_topics)) call(u"daemon"_s, u"Watch"_s, u"s"_s, { t });
    if (!m_connected) { m_connected = true; emit connectedChanged(); }
}

void SolwindBus::gone()
{
    if (m_closing) return;
    m_retry.stop();
    if (m_connected) { m_connected = false; emit connectedChanged(); }
}

void SolwindBus::close()
{
    m_closing = true;
    m_retry.stop();
    qDeleteAll(findChildren<QDBusPendingCallWatcher *>(Qt::FindDirectChildrenOnly));
}

bool SolwindBus::readAll(const Object &o)
{
    QDBusMessage m = QDBusMessage::createMethodCall(service, o.path, propsIface, u"GetAll"_s);
    m << o.iface;
    const QDBusMessage r = m_bus.call(m, QDBus::Block, 3000);
    if (r.type() != QDBusMessage::ReplyMessage || r.arguments().isEmpty()) return false;
    const QVariantMap all = toQml(r.arguments().at(0)).toMap();
    for (auto it = all.begin(); it != all.end(); ++it) o.map->insert(it.key(), it.value());
    return true;
}

void SolwindBus::onPropertiesChanged(const QString &iface, const QVariantMap &changed, const QStringList &)
{
    if (m_closing) return;
    for (const Object &o : m_objects) {
        if (o.iface != iface) continue;
        for (auto it = changed.begin(); it != changed.end(); ++it) o.map->insert(it.key(), toQml(it.value()));
    }
}

void SolwindBus::onSampled(const QVariantMap &sample) { if (!m_closing) emit sampled(toQml(sample).toMap()); }

void SolwindBus::watch(const QString &topic, bool on)
{
    if (on == m_topics.contains(topic)) return;
    if (on) m_topics.insert(topic);
    else m_topics.remove(topic);
    if (m_connected) call(u"daemon"_s, on ? u"Watch"_s : u"Unwatch"_s, u"s"_s, { topic });
}

// ---------- calls ----------

QStringList SolwindBus::splitSignature(const QString &sig)
{
    QStringList out;
    qsizetype i = 0;
    while (i < sig.size()) {
        qsizetype j = i;
        int depth = 0;
        do {
            const QChar c = sig[j];
            if (c == u'(' || c == u'{') ++depth;
            else if (c == u')' || c == u'}') --depth;
            ++j;
        } while (j < sig.size() && (depth > 0 || sig[j - 1] == u'a'));
        out << sig.mid(i, j - i);
        i = j;
    }
    return out;
}

QVariant SolwindBus::toDBus(const QVariant &in, const QString &type, bool *ok)
{
    *ok = true;
    const QVariant v = in.metaType() == QMetaType::fromType<QJSValue>() ? in.value<QJSValue>().toVariant() : in;
    if (type == u"i"_s) return v.toInt(ok);
    if (type == u"u"_s) return v.toUInt(ok);
    if (type == u"x"_s) return v.toLongLong(ok);
    if (type == u"d"_s) return v.toDouble(ok);
    if (type == u"b"_s) return v.toBool();
    if (type == u"s"_s) return v.toString();
    if (type == u"as"_s) return v.toStringList();
    if (type == u"ad"_s) {
        QList<double> l;
        for (const QVariant &x : v.toList()) l << x.toDouble();
        return QVariant::fromValue(l);
    }
    if (type == u"a{sv}"_s) return v.toMap();                  // (nested lists go out as av, maps as a{sv})
    if (type == u"v"_s) return QVariant::fromValue(QDBusVariant(v));
    *ok = false;
    return QVariant();
}

QDBusMessage SolwindBus::message(const QString &objName, const QString &method, const QString &signature,
                                 const QVariantList &args, QString *why) const
{
    const Object *o = object(objName);
    if (!o) { *why = u"no such object: "_s + objName; return {}; }
    const QStringList types = splitSignature(signature);
    if (types.size() != args.size()) { *why = u"%1 arguments for the signature \"%2\""_s.arg(args.size()).arg(signature); return {}; }
    QVariantList out;
    for (qsizetype i = 0; i < args.size(); ++i) {
        bool ok = false;
        out << toDBus(args[i], types[i], &ok);
        if (!ok) { *why = u"argument %1 isn't a %2"_s.arg(i + 1).arg(types[i]); return {}; }
    }
    QDBusMessage m = QDBusMessage::createMethodCall(service, o->path, o->iface, method);
    m.setArguments(out);
    return m;
}

QVariantMap SolwindBus::result(const QDBusMessage &r)
{
    if (r.type() == QDBusMessage::ReplyMessage) {
        QVariantList all;
        for (const QVariant &a : r.arguments()) all << toQml(a);
        return { { u"ok"_s, true }, { u"value"_s, all.value(0) }, { u"values"_s, all } };
    }
    return { { u"ok"_s, false }, { u"error"_s, r.errorMessage() }, { u"errorName"_s, r.errorName() } };
}

QVariantMap SolwindBus::call(const QString &object, const QString &method, const QString &signature, const QVariantList &args)
{
    QString why;
    const QDBusMessage m = message(object, method, signature, args, &why);
    if (!why.isEmpty()) return { { u"ok"_s, false }, { u"error"_s, why }, { u"errorName"_s, u"solwind.Error.Usage"_s } };
    return result(m_bus.call(m, QDBus::Block, 3000));
}

void SolwindBus::callLater(const QString &object, const QString &method, const QString &signature, const QVariantList &args,
                           const QJSValue &back)
{
    QString why;
    const QDBusMessage m = message(object, method, signature, args, &why);
    auto answer = [this, back](const QVariantMap &r) {
        if (m_closing || !back.isCallable() || !m_engine) return;
        const QJSValue x = QJSValue(back).call({ m_engine->toScriptValue(r) });
        if (x.isError()) qWarning("[solwind] callLater callback: %s", qPrintable(x.toString()));
    };
    if (!why.isEmpty()) { answer({ { u"ok"_s, false }, { u"error"_s, why }, { u"errorName"_s, u"solwind.Error.Usage"_s } }); return; }
    auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(m, 30000), this);   // (Recommend can take a while)
    connect(w, &QDBusPendingCallWatcher::finished, this, [answer, w] {
        answer(result(w->reply()));
        w->deleteLater();
    });
}

QVariant SolwindBus::toQml(const QVariant &v)
{
    if (v.metaType() == QMetaType::fromType<QDBusVariant>()) return toQml(v.value<QDBusVariant>().variant());
    if (v.metaType() == QMetaType::fromType<QDBusArgument>()) return fromArgument(v.value<QDBusArgument>());
    if (v.metaType() == QMetaType::fromType<QVariantList>()) {
        QVariantList l;
        for (const QVariant &x : v.toList()) l << toQml(x);
        return l;
    }
    if (v.metaType() == QMetaType::fromType<QVariantMap>()) {
        QVariantMap m;
        const QVariantMap in = v.toMap();
        for (auto it = in.begin(); it != in.end(); ++it) m.insert(it.key(), toQml(it.value()));
        return m;
    }
    if (v.metaType() == QMetaType::fromType<QStringList>()) return v.toStringList();
    return v;
}
