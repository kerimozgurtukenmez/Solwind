#include "CallClient.h"

#include "dbus/DaemonApi.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QHash>

#include <cstdio>

using namespace Qt::StringLiterals;

namespace CallClient {
namespace {

struct Target { QString path, iface; };

bool target(const QString &name, Target *t)
{
    static const QHash<QString, Target> objects{
        { u"daemon"_s, { u"/solwind"_s, u"solwind.Daemon"_s } }, { u"fans"_s, { u"/solwind/Fans"_s, u"solwind.Fans"_s } },
        { u"sensors"_s, { u"/solwind/Sensors"_s, u"solwind.Sensors"_s } },
        { u"keyboard"_s, { u"/solwind/Keyboard"_s, u"solwind.Keyboard"_s } } };
    const auto it = objects.constFind(name);
    if (it == objects.cend()) return false;
    *t = *it;
    return true;
}

// (UTF-8, as stdout: the messages may be Turkish, and the terminal's locale may be C)
void printErr(const QString &s)
{
    const QByteArray b = s.toUtf8() + '\n';
    std::fwrite(b.constData(), 1, size_t(b.size()), stderr);
}

// One argument as the D-Bus type `type`; false if it isn't one
bool convert(const QString &type, const QString &text, QVariant *out)
{
    bool ok = true;
    if (type == u"s"_s) *out = text;
    else if (type == u"i"_s) *out = text.toInt(&ok);
    else if (type == u"u"_s) *out = text.toUInt(&ok);
    else if (type == u"x"_s) *out = text.toLongLong(&ok);
    else if (type == u"d"_s) *out = text.toDouble(&ok);
    else if (type == u"b"_s) {
        ok = text == u"true"_s || text == u"false"_s;
        *out = text == u"true"_s;
    } else ok = false;
    return ok;
}

QString text(const QVariant &v)
{
    const QVariant x = v.metaType() == QMetaType::fromType<QDBusVariant>() ? v.value<QDBusVariant>().variant() : v;
    if (x.metaType().id() == QMetaType::Bool) return x.toBool() ? u"true"_s : u"false"_s;
    return x.toString();
}

void print(const QString &s)
{
    if (s.isEmpty()) return;
    const QByteArray b = s.toUtf8();
    std::fwrite(b.constData(), 1, size_t(b.size()), stdout);
    if (!s.endsWith(u'\n')) std::fputc('\n', stdout);
}

int send(QDBusMessage m, int timeoutMs)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) { printErr(u"no D-Bus session bus: "_s + bus.lastError().message()); return Refused; }
    const QDBusMessage r = bus.call(m, QDBus::Block, timeoutMs);
    if (r.type() == QDBusMessage::ReplyMessage) {
        if (!r.arguments().isEmpty()) print(text(r.arguments().at(0)));
        return Ok;
    }
    const QString name = r.errorName();
    // Nobody holds the name: solwindd isn't running (bin/solwind says so in the user's language)
    if (name == u"org.freedesktop.DBus.Error.ServiceUnknown"_s || name == u"org.freedesktop.DBus.Error.NameHasNoOwner"_s)
        return NotRunning;
    printErr(r.errorMessage());
    return name == u"solwind.Error.Usage"_s || name == u"org.freedesktop.DBus.Error.InvalidArgs"_s ? Usage : Refused;
}

} // namespace

int call(const QStringList &args)
{
    Target t;
    if (args.size() < 2 || !target(args[0], &t)) {
        printErr(u"usage: solwindd --call daemon|fans|sensors|keyboard <method> [<signature> <argument>...]"_s);
        return Usage;
    }
    QVariantList values;
    if (args.size() > 2) {
        const QString sig = args[2];
        const QStringList rest = args.mid(3);
        if (sig.size() != rest.size()) { printErr(u"%1 arguments for the signature \"%2\""_s.arg(rest.size()).arg(sig)); return Usage; }
        for (qsizetype i = 0; i < rest.size(); ++i) {
            QVariant v;
            if (!convert(QString(sig[i]), rest[i], &v)) { printErr(u"not a %1: %2"_s.arg(sig[i]).arg(rest[i])); return Usage; }
            values << v;
        }
    }
    QDBusMessage m = QDBusMessage::createMethodCall(QString::fromLatin1(DaemonApi::serviceName), t.path, t.iface, args[1]);
    m.setArguments(values);
    // (no auto-start: there is no D-Bus activation file, and a terminal command never starts the daemon by itself)
    m.setAutoStartService(false);
    return send(m, 30000);                                // (an NBFC action can be queued behind others)
}

int get(const QStringList &args)
{
    Target t;
    if (args.size() != 2 || !target(args[0], &t)) {
        printErr(u"usage: solwindd --get daemon|fans|sensors|keyboard <property>"_s);
        return Usage;
    }
    QDBusMessage m = QDBusMessage::createMethodCall(QString::fromLatin1(DaemonApi::serviceName), t.path,
                                                    u"org.freedesktop.DBus.Properties"_s, u"Get"_s);
    m << t.iface << args[1];
    m.setAutoStartService(false);
    return send(m, 5000);                                 // (a property is answered at once)
}

} // namespace CallClient
