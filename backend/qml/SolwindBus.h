#pragma once

#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QJSValue>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QQmlPropertyMap>
#include <QSet>
#include <QTimer>
#include <QVariant>

// A mirrored D-Bus object's properties (QQmlPropertyMap's own constructor for subclasses)
class DBusMirror : public QQmlPropertyMap
{
    Q_OBJECT
public:
    explicit DBusMirror(QObject *parent) : QQmlPropertyMap(this, parent) {}
};

// The QML app's way to solwindd (ARCH-PLAN.md phase 5): import Solwind, then Solwind.fans.Running,
// Solwind.call("fans", "SetFan", "ia{sv}", [0, st]) ... The daemon's D-Bus objects are mirrored as property maps
// (bindable in QML: every PropertiesChanged updates them), its methods are called with the D-Bus signature given, so
// each argument goes out as exactly that type. The daemon coming and going is followed: when it (re)appears every
// property is read again and the topics this UI watches are sent again; `connected` turns true only once all four
// objects were read (a read that fails is tried again every second while the daemon is there).
class SolwindBus : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Solwind)
    QML_SINGLETON
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QQmlPropertyMap *daemon READ daemon CONSTANT)     // /solwind, solwind.Daemon (+ Status())
    Q_PROPERTY(QQmlPropertyMap *fans READ fans CONSTANT)         // /solwind/Fans, solwind.Fans
    Q_PROPERTY(QQmlPropertyMap *sensors READ sensors CONSTANT)   // /solwind/Sensors, solwind.Sensors
    Q_PROPERTY(QQmlPropertyMap *keyboard READ keyboard CONSTANT) // /solwind/Keyboard, solwind.Keyboard

public:
    // Made by the engine through create() (Qt takes a default constructor over create(): qqmlprivate.h, so there is none)
    explicit SolwindBus(QObject *parent);
    static SolwindBus *create(QQmlEngine *qml, QJSEngine *js);   // Qt.quit() stops the updates, see close()

    bool connected() const { return m_connected; }
    QQmlPropertyMap *daemon() { return &m_daemon; }
    QQmlPropertyMap *fans() { return &m_fans; }
    QQmlPropertyMap *sensors() { return &m_sensors; }
    QQmlPropertyMap *keyboard() { return &m_keyboard; }

    // object: "daemon" | "fans" | "sensors" | "keyboard"; signature: the arguments' D-Bus types in order ("ia{sv}").
    // Answers { ok, value (the first answer, as QML values), values (all of them), error (message), errorName }.
    // call() waits (at most
    // 3 s: the daemon answers at once, its work runs behind it); callLater() calls back(result) instead.
    Q_INVOKABLE QVariantMap call(const QString &object, const QString &method, const QString &signature = QString(),
                                 const QVariantList &args = {});
    Q_INVOKABLE void callLater(const QString &object, const QString &method, const QString &signature,
                               const QVariantList &args, const QJSValue &back);
    // A topic this UI shows (Watch / Unwatch: "window" = a window is open, "fans" = the fan page): the daemon reads
    // faster / reads the sensors while it is on. Kept and sent again when the daemon comes back.
    Q_INVOKABLE void watch(const QString &topic, bool on);

    // D-Bus values as QML takes them: a{sv} -> object, arrays -> lists, variants unwrapped (also for tests)
    static QVariant toQml(const QVariant &v);
    // A QML value as the D-Bus type `type` (one complete type: i u x d b s as ad a{sv} v)
    static QVariant toDBus(const QVariant &v, const QString &type, bool *ok);
    static QStringList splitSignature(const QString &signature);

signals:
    void connectedChanged();
    void sampled(const QVariantMap &sample);      // solwind.Fans Sampled: a new graph sample

private slots:
    void onPropertiesChanged(const QString &iface, const QVariantMap &changed, const QStringList &invalidated);
    void onSampled(const QVariantMap &sample);

private:
    struct Object { QString path, iface; QQmlPropertyMap *map; };
    const Object *object(const QString &name) const;
    void appeared();
    void gone();
    // The QML asked to quit (Qt.quit): no more updates or callbacks. The daemon's answer to a last call (its Quit
    // hands the fans back) would otherwise reach bindings while the engine tears them down ("Unable to assign
    // [undefined]", measured); pending callLater()s are dropped with their QJSValue callbacks.
    void close();
    bool readAll(const Object &o);
    QDBusMessage message(const QString &object, const QString &method, const QString &signature,
                         const QVariantList &args, QString *why) const;
    static QVariantMap result(const QDBusMessage &reply);

    QDBusConnection m_bus;
    QDBusServiceWatcher m_watcher;
    QTimer m_retry;                               // a GetAll that failed: again in a second
    QPointer<QJSEngine> m_engine;                 // (from create(): for callLater's callbacks)
    DBusMirror m_daemon{ this }, m_fans{ this }, m_sensors{ this }, m_keyboard{ this };
    QList<Object> m_objects;
    QSet<QString> m_topics;
    bool m_connected = false;
    bool m_closing = false;
};
