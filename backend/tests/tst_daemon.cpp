// solwindd as a process: D-Bus name, Status/Quit, single instance, clean quit on signals, and (phase 2) the read-only
// Sensors / Fans objects, client hints and change signals. Runs only on a private session bus (ctest starts it
// under dbus-run-session) and in a fake world (fake nbfc, /sys, NBFC files); refuses to run on the user's own bus.
#include "TestEnv.h"
#include "dbus/DBusUtil.h"
#include "dbus/DaemonApi.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusArgument>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QDir>
#include <QMap>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <csignal>

using namespace Qt::StringLiterals;

namespace {
const QString service = QString::fromLatin1(DaemonApi::serviceName);
const QString path = QString::fromLatin1(DaemonApi::objectPath);

bool nameOwned() { return QDBusConnection::sessionBus().interface()->isServiceRegistered(service); }

// A property's value as it arrives over D-Bus, with its signature
QVariant getProperty(const QString &objPath, const QString &iface, const QString &name, QString *signature = nullptr)
{
    QDBusMessage m = QDBusMessage::createMethodCall(service, objPath, u"org.freedesktop.DBus.Properties"_s, u"Get"_s);
    m << iface << name;
    const QDBusMessage r = QDBusConnection::sessionBus().call(m);
    if (r.type() != QDBusMessage::ReplyMessage || r.arguments().isEmpty()) return {};
    const QVariant v = r.arguments().first().value<QDBusVariant>().variant();
    if (v.canConvert<QDBusArgument>()) {
        const QDBusArgument a = v.value<QDBusArgument>();
        if (signature) *signature = a.currentSignature();
        if (a.currentSignature() == u"aa{sv}"_s) return QVariant::fromValue(qdbus_cast<VariantMapList>(a));
        if (a.currentSignature() == u"a{sv}"_s) return qdbus_cast<QVariantMap>(a);
        if (a.currentSignature() == u"ad"_s) return QVariant::fromValue(qdbus_cast<DoubleList>(a));
    }
    if (signature) *signature = QString::fromLatin1(QDBusMetaType::typeToSignature(v.metaType()));
    return v;
}
} // namespace

class TstDaemon : public QObject
{
    Q_OBJECT

    QTemporaryDir m_home;
    FakeWorld m_world;
    QStringList m_changed;                     // properties named in PropertiesChanged of /solwind/Fans
    int m_sampled = 0;

    // A daemon in the fake world, with its own settings folder; `extra` overrides variables
    std::unique_ptr<QProcess> start(const QProcessEnvironment &extra = {}, const QStringList &args = {})
    {
        QProcessEnvironment env = m_world.env();
        env.insert(u"XDG_CONFIG_HOME"_s, m_home.path());
        for (const QString &k : extra.keys()) env.insert(k, extra.value(k));
        auto p = std::make_unique<QProcess>();
        p->setProcessEnvironment(env);
        p->setProgram(QStringLiteral(SOLWINDD_PATH));
        p->setArguments(args);
        p->start();
        return p;
    }
    void startAndWait(std::unique_ptr<QProcess> &p, const QStringList &args = {})
    {
        p = start({}, args);
        QVERIFY(p->waitForStarted(3000));
        QTRY_VERIFY_WITH_TIMEOUT(nameOwned(), 5000);
    }
    void expectCleanExit(QProcess &p)
    {
        QVERIFY(p.waitForFinished(5000));
        QCOMPARE(p.exitStatus(), QProcess::NormalExit);
        QCOMPARE(p.exitCode(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(!nameOwned(), 3000);
    }

private slots:
    void initTestCase()
    {
        // Never the user's own bus: started by ctest under dbus-run-session, whose bus isn't the login one
        const QString addr = qEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS");
        const QString loginBus = qEnvironmentVariable("XDG_RUNTIME_DIR") + u"/bus"_s;
        if (qEnvironmentVariable("SOLWIND_TEST_PRIVATE_BUS") != u"1"_s || addr.isEmpty() || addr.contains(loginBus))
            QSKIP("run through ctest (dbus-run-session): these tests must not touch the user's session bus");
        QVERIFY(QDBusConnection::sessionBus().isConnected());
        QVERIFY(m_home.isValid());
        QVERIFY(!nameOwned());
        DBusUtil::registerTypes();
        m_world.hwmon(0, "coretemp", { { 1, "Package id 0", "52000\n" } });
        m_world.hwmon(1, "hp", {}, { { 1, "3100\n" }, { 2, "3400\n" } });
        m_world.state(true, false, u"TestA"_s, 50, { { u"CPU Fan"_s, false, 44 }, { u"GPU Fan"_s } });
    }

public slots:
    void onFansChanged(const QString &, const QVariantMap &changed, const QStringList &) { m_changed << changed.keys(); }
    void onSampled(const QVariantMap &) { ++m_sampled; }

private slots:

    void versionFlag()
    {
        QProcess p;
        p.start(QStringLiteral(SOLWINDD_PATH), { u"--version"_s });
        QVERIFY(p.waitForFinished(3000));
        QCOMPARE(p.exitCode(), 0);
        QCOMPARE(QString::fromUtf8(p.readAllStandardOutput()).trimmed(), u"solwindd "_s + QStringLiteral(SOLWIND_VERSION));
    }

    void statusAndQuit()
    {
        std::unique_ptr<QProcess> p;
        startAndWait(p);
        QDBusInterface api(service, path, service);
        QVERIFY(api.isValid());
        const QDBusReply<QVariantMap> st = api.call(u"Status"_s);
        QVERIFY2(st.isValid(), qPrintable(st.error().message()));
        QCOMPARE(st.value().value(u"version"_s).toString(), QStringLiteral(SOLWIND_VERSION));
        QCOMPARE(st.value().value(u"pid"_s).toLongLong(), p->processId());
        QCOMPARE(st.value().value(u"settingsPath"_s).toString(), m_home.path() + u"/solwind/settings.json"_s);
        QCOMPARE(st.value().value(u"settingsLoaded"_s).toBool(), false);
        QCOMPARE(st.value().value(u"sysRoot"_s).toString(), m_world.sysRoot());
        QCOMPARE(api.property("Version").toString(), QStringLiteral(SOLWIND_VERSION));

        const QDBusMessage r = api.call(u"Quit"_s);
        QCOMPARE(r.type(), QDBusMessage::ReplyMessage);       // answered before it went away
        expectCleanExit(*p);
        QVERIFY(!QFile::exists(m_home.path() + u"/solwind/settings.json"_s));   // nothing changed, nothing written
    }

    void secondCopyRefused()
    {
        std::unique_ptr<QProcess> first;
        startAndWait(first);
        auto second = start();
        QVERIFY(second->waitForFinished(5000));
        QCOMPARE(second->exitCode(), 3);
        QVERIFY(QString::fromUtf8(second->readAllStandardError()).contains(u"already running"_s));
        QVERIFY(nameOwned());                                  // the first one is untouched
        QVERIFY(signalProcess(*first, SIGTERM));
        expectCleanExit(*first);
    }

    void signalsQuitCleanly_data()
    {
        QTest::addColumn<int>("signo");
        QTest::newRow("SIGTERM") << int(SIGTERM);
        QTest::newRow("SIGINT") << int(SIGINT);
        QTest::newRow("SIGHUP") << int(SIGHUP);
    }
    void signalsQuitCleanly()
    {
        QFETCH(int, signo);
        std::unique_ptr<QProcess> p;
        startAndWait(p);
        QVERIFY(signalProcess(*p, signo));
        expectCleanExit(*p);
        QVERIFY(QString::fromUtf8(p->readAllStandardError()).contains(u"quitting"_s));
    }

    void existingSettingsUntouched()
    {
        const QString file = m_home.path() + u"/solwind/settings.json"_s;
        QDir().mkpath(m_home.path() + u"/solwind"_s);
        const QByteArray text = "{\"version\": 1, \"safety\": 80, \"configs\": {}}";
        { QFile f(file); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(text); }
        std::unique_ptr<QProcess> p;
        startAndWait(p);
        QDBusInterface api(service, path, service);
        QCOMPARE(QDBusReply<QVariantMap>(api.call(u"Status"_s)).value().value(u"settingsLoaded"_s).toBool(), true);
        QVERIFY(signalProcess(*p, SIGTERM));
        expectCleanExit(*p);
        QFile f(file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), text);                           // read only: byte for byte the same
        f.remove();
    }

    void busGoneQuits()
    {
        // A bus of its own that the test can take away: the daemon must quit (cleanly, exit 1) rather than run on
        // without its name, where a second copy could take over the fans too
        QProcess bus;
        bus.start(u"dbus-daemon"_s, { u"--session"_s, u"--nofork"_s, u"--print-address=1"_s });
        QVERIFY(bus.waitForStarted(3000));
        QVERIFY(bus.waitForReadyRead(5000));
        const QString addr = QString::fromUtf8(bus.readLine()).trimmed();
        QVERIFY(!addr.isEmpty());
        QProcessEnvironment env;
        env.insert(u"DBUS_SESSION_BUS_ADDRESS"_s, addr);
        auto p = start(env);
        QVERIFY(p->waitForStarted(3000));
        {
            QDBusConnection c = QDBusConnection::connectToBus(addr, u"own-bus"_s);
            QVERIFY(c.isConnected());
            QTRY_VERIFY_WITH_TIMEOUT(c.interface()->isServiceRegistered(service), 5000);
        }
        QDBusConnection::disconnectFromBus(u"own-bus"_s);
        bus.terminate();
        QVERIFY(bus.waitForFinished(3000));
        QVERIFY(p->waitForFinished(5000));
        QCOMPARE(p->exitStatus(), QProcess::NormalExit);
        QCOMPARE(p->exitCode(), 1);
        QVERIFY(QString::fromUtf8(p->readAllStandardError()).contains(u"went away"_s));
    }

    // ---------- phase 2: reading ----------
    void readingsOverDBus()
    {
        std::unique_ptr<QProcess> p;
        startAndWait(p);
        const QString fans = u"/solwind/Fans"_s, sens = u"/solwind/Sensors"_s;
        QString sig;
        QTRY_VERIFY_WITH_TIMEOUT(getProperty(fans, u"solwind.Fans"_s, u"Running"_s).toBool(), 5000);
        const auto f = getProperty(fans, u"solwind.Fans"_s, u"Fans"_s, &sig).value<VariantMapList>();
        QCOMPARE(sig, u"aa{sv}"_s);
        QCOMPARE(f.size(), 2);
        QCOMPARE(f[0].value(u"name"_s).toString(), u"CPU Fan"_s);
        QCOMPARE(f[0].value(u"target"_s).toDouble(), 44.0);
        QCOMPARE(f[0].value(u"auto"_s).toBool(), false);
        QCOMPARE(getProperty(fans, u"solwind.Fans"_s, u"ConfigId"_s).toString(), u"TestA"_s);
        QCOMPARE(getProperty(fans, u"solwind.Fans"_s, u"Temp"_s).toDouble(), 50.0);
        // Notifications are on by default, so the sensors are read even with no client (the critical warning)
        QTRY_COMPARE_WITH_TIMEOUT(getProperty(sens, u"solwind.Sensors"_s, u"Cpu"_s).toDouble(), 52.0, 8000);
        const auto r = getProperty(sens, u"solwind.Sensors"_s, u"Readings"_s, &sig).value<VariantMapList>();
        QCOMPARE(sig, u"aa{sv}"_s);
        QCOMPARE(r[0].value(u"label"_s).toString(), u"CPU"_s);
        QVERIFY(r[0].contains(u"usage"_s));
        QCOMPARE(getProperty(sens, u"solwind.Sensors"_s, u"Rpms"_s, &sig).value<DoubleList>(), (DoubleList{ 3100, 3400 }));
        QCOMPARE(sig, u"ad"_s);
        // RPM next to NBFC's fans (the driver has as many fans as NBFC)
        QTRY_COMPARE_WITH_TIMEOUT(getProperty(fans, u"solwind.Fans"_s, u"Fans"_s).value<VariantMapList>()[1].value(u"rpm"_s).toDouble(), 3400.0, 8000);
        QVERIFY(signalProcess(*p, SIGTERM));
        expectCleanExit(*p);
    }

    void changesAreSignalled()
    {
        std::unique_ptr<QProcess> p;
        startAndWait(p);
        QDBusConnection bus = QDBusConnection::sessionBus();
        QVERIFY(bus.connect(service, u"/solwind/Fans"_s, u"org.freedesktop.DBus.Properties"_s, u"PropertiesChanged"_s,
                            this, SLOT(onFansChanged(QString,QVariantMap,QStringList))));
        QVERIFY(bus.connect(service, u"/solwind/Fans"_s, u"solwind.Fans"_s, u"Sampled"_s, this, SLOT(onSampled(QVariantMap))));
        QDBusInterface api(service, path, service);
        QVERIFY(api.call(u"Watch"_s, u"window"_s).type() == QDBusMessage::ReplyMessage);   // NBFC every 2 s
        m_changed.clear();
        m_world.state(true, false, u"TestA"_s, 50, { { u"CPU Fan"_s, false, 61 }, { u"GPU Fan"_s } });
        QTRY_VERIFY_WITH_TIMEOUT(m_changed.contains(u"Fans"_s), 6000);
        QVERIFY(!m_changed.contains(u"ConfigId"_s));          // only what changed
        QTRY_VERIFY_WITH_TIMEOUT(m_sampled >= 1, 5000);
        const QDBusReply<VariantMapList> h = QDBusInterface(service, u"/solwind/Fans"_s, u"solwind.Fans"_s).call(u"History"_s);
        QVERIFY2(h.isValid(), qPrintable(h.error().message()));
        QVERIFY(h.value().size() >= 1);
        QCOMPARE(h.value().last().value(u"cpu"_s).toDouble(), 50.0);
        QVERIFY(signalProcess(*p, SIGTERM));
        expectCleanExit(*p);
        bus.disconnect(service, u"/solwind/Fans"_s, u"org.freedesktop.DBus.Properties"_s, u"PropertiesChanged"_s,
                       this, SLOT(onFansChanged(QString,QVariantMap,QStringList)));
        bus.disconnect(service, u"/solwind/Fans"_s, u"solwind.Fans"_s, u"Sampled"_s, this, SLOT(onSampled(QVariantMap)));
    }

    void watchTopics()
    {
        std::unique_ptr<QProcess> p;
        startAndWait(p, { u"--observe"_s });
        QDBusInterface api(service, path, service);
        auto status = [&] { return QDBusReply<QVariantMap>(api.call(u"Status"_s)).value(); };
        QCOMPARE(status().value(u"mode"_s).toString(), u"observe"_s);
        QCOMPARE(status().value(u"nbfcPollMs"_s).toInt(), 5000);
        QCOMPARE(status().value(u"sensorsFast"_s).toBool(), false);
        const QDBusMessage bad = api.call(u"Watch"_s, u"bogus"_s);
        QCOMPARE(bad.type(), QDBusMessage::ErrorMessage);
        QCOMPARE(bad.errorName(), u"org.freedesktop.DBus.Error.InvalidArgs"_s);
        {
            // A second client asks for the window pace, then goes away without saying so
            QDBusConnection other = QDBusConnection::connectToBus(QDBusConnection::SessionBus, u"other-client"_s);
            QDBusInterface api2(service, path, service, other);
            QCOMPARE(api2.call(u"Watch"_s, u"window"_s).type(), QDBusMessage::ReplyMessage);
            QCOMPARE(status().value(u"nbfcPollMs"_s).toInt(), 2000);
            QCOMPARE(status().value(u"sensorsFast"_s).toBool(), true);
        }
        QDBusConnection::disconnectFromBus(u"other-client"_s);
        QTRY_COMPARE_WITH_TIMEOUT(status().value(u"nbfcPollMs"_s).toInt(), 5000, 3000);    // forgotten by itself
        QCOMPARE(api.call(u"Watch"_s, u"window"_s).type(), QDBusMessage::ReplyMessage);
        QCOMPARE(status().value(u"nbfcPollMs"_s).toInt(), 2000);
        QCOMPARE(api.call(u"Unwatch"_s, u"window"_s).type(), QDBusMessage::ReplyMessage);
        QCOMPARE(status().value(u"nbfcPollMs"_s).toInt(), 5000);
        QVERIFY(signalProcess(*p, SIGTERM));
        expectCleanExit(*p);
    }

    void recommendOverDBus()
    {
        m_world.write(u"recommend.txt"_s, "TestB  x\n");
        FakeWorld::writeAt(m_world.nbfcRoot() + u"/usr/share/nbfc/configs/TestB.json"_s, "{}");
        std::unique_ptr<QProcess> p;
        startAndWait(p);
        QDBusInterface fans(service, u"/solwind/Fans"_s, u"solwind.Fans"_s);
        QTRY_VERIFY_WITH_TIMEOUT(getProperty(u"/solwind/Fans"_s, u"solwind.Fans"_s, u"Configs"_s).toStringList().contains(u"TestB"_s), 5000);
        const QDBusMessage r = fans.call(u"Recommend"_s);
        QCOMPARE(r.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(r.arguments().value(0).toBool(), true);
        QCOMPARE(r.arguments().value(1).toStringList(), QStringList{ u"TestB"_s });
        QVERIFY(signalProcess(*p, SIGTERM));
        expectCleanExit(*p);
    }

    // ---------- plugins (PLUGIN-PLAN.md P1, P2) ----------
    void pluginsOfThisBuild()
    {
        std::unique_ptr<QProcess> p;
        startAndWait(p);
        const VariantMapList list = qvariant_cast<VariantMapList>(getProperty(path, service, u"Plugins"_s));
        QStringList ids;
        for (const QVariantMap &m : list) ids << m.value(u"id"_s).toString();
        // (by priority: HP's zone plugins are asked before the single light, as the old built-in order)
        QCOMPARE(ids, (QStringList{ u"HP/hp-wmi-keyboard-zones"_s, u"HP/hp-wmi-rgb-zones"_s, u"HP/hp-wmi-keyboard"_s, u"NBFC/nbfc"_s, u"HP/hp-wmi-fan"_s }));
        QCOMPARE(list[3].value(u"kind"_s).toString(), u"fan"_s);
        QCOMPARE(list[3].value(u"brand"_s).toString(), u"NBFC"_s);
        QCOMPARE(list[3].value(u"file"_s).toString(), QStringLiteral(PLUGINS_DIR) + u"/NBFC/nbfc.so"_s);
        QCOMPARE(list[0].value(u"kind"_s).toString(), u"keyboard"_s);
        // (only one fan plugin drives the fans: NBFC's priority comes first)
        for (const QVariantMap &m : list)
            QCOMPARE(m.value(u"used"_s).toBool(), m.value(u"id"_s).toString() != u"HP/hp-wmi-fan"_s);
        QCOMPARE(qvariant_cast<VariantMapList>(getProperty(path, service, u"PluginFailures"_s)).size(), 0);
        QCOMPARE(getProperty(u"/solwind/Fans"_s, u"solwind.Fans"_s, u"Plugin"_s).toString(), u"NBFC/nbfc"_s);
        QCOMPARE(getProperty(u"/solwind/Fans"_s, u"solwind.Fans"_s, u"HasService"_s).toBool(), true);
        QVERIFY(signalProcess(*p, SIGTERM));
        expectCleanExit(*p);
    }

    // No plugin installed: it runs, reads the sensors, refuses every fan change and says why; NBFC is never run
    void withoutPlugins()
    {
        QTemporaryDir none;
        const QString led = m_world.sysRoot() + u"/sys/class/leds/hp::kbd_backlight"_s;
        FakeWorld::writeAt(led + u"/brightness"_s, "200\n");
        FakeWorld::writeAt(led + u"/max_brightness"_s, "255\n");
        FakeWorld::writeAt(led + u"/multi_index"_s, "red green blue\n");
        FakeWorld::writeAt(led + u"/multi_intensity"_s, "10 20 30\n");
        const QString drv = m_world.sysRoot() + u"/sys/bus/platform/drivers/hp-wmi"_s;
        QDir().mkpath(drv);
        QDir().mkpath(led + u"/device"_s);
        QFile::link(drv, led + u"/device/driver"_s);
        const qsizetype nbfcRuns = m_world.calls().count("nbfc ");

        QProcessEnvironment env;
        env.insert(u"SOLWIND_PLUGIN_PATH"_s, none.path());
        auto p = start(env);
        QVERIFY(p->waitForStarted(3000));
        QTRY_VERIFY_WITH_TIMEOUT(nameOwned(), 5000);
        const QString fansPath = u"/solwind/Fans"_s, fansIface = u"solwind.Fans"_s;
        QCOMPARE(qvariant_cast<VariantMapList>(getProperty(path, service, u"Plugins"_s)).size(), 0);
        QCOMPARE(getProperty(fansPath, fansIface, u"Plugin"_s).toString(), QString());
        QCOMPARE(getProperty(fansPath, fansIface, u"HasService"_s).toBool(), false);
        QCOMPARE(getProperty(fansPath, fansIface, u"Running"_s).toBool(), false);
        QCOMPARE(qvariant_cast<VariantMapList>(getProperty(fansPath, fansIface, u"Fans"_s)).size(), 0);
        QCOMPARE(getProperty(fansPath, fansIface, u"BoostBlocked"_s).toString(), u"No fan plugin is installed"_s);
        QDBusInterface fans(service, fansPath, fansIface);
        QDBusMessage r = fans.call(u"SetBoost"_s, qlonglong(5));
        QCOMPARE(r.errorName(), u"solwind.Error.Failed"_s);
        QCOMPARE(r.errorMessage(), u"No fan plugin is installed"_s);
        r = fans.call(u"StartService"_s);
        QCOMPARE(r.errorName(), u"solwind.Error.Failed"_s);
        QCOMPARE(r.errorMessage(), u"This fan plugin has no service to control"_s);
        r = fans.call(u"Recommend"_s);
        QCOMPARE(r.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(r.arguments().value(0).toBool(), false);
        r = fans.call(u"StatusText"_s);
        QCOMPARE(r.arguments().value(0).toString(), u"No fan plugin is installed"_s);
        // The light is there, but nothing takes it
        const QString kbdPath = u"/solwind/Keyboard"_s, kbdIface = u"solwind.Keyboard"_s;
        QTRY_VERIFY_WITH_TIMEOUT(getProperty(kbdPath, kbdIface, u"Ready"_s).toBool(), 5000);
        QCOMPARE(getProperty(kbdPath, kbdIface, u"Available"_s).toBool(), false);
        QCOMPARE(getProperty(kbdPath, kbdIface, u"Notes"_s).toStringList(), QStringList{ u"no-plugin"_s });
        r = QDBusInterface(service, kbdPath, kbdIface).call(u"StatusText"_s);
        QCOMPARE(r.arguments().value(0).toString(),
                 u"Found the keyboard light hp::kbd_backlight (driver hp-wmi), but no installed plugin supports it."_s);
        // (readings go on without a fan plugin)
        QDBusInterface daemon(service, path, service);
        QVERIFY(daemon.call(u"Watch"_s, u"fans"_s).type() == QDBusMessage::ReplyMessage);
        QTRY_VERIFY_WITH_TIMEOUT(getProperty(u"/solwind/Sensors"_s, u"solwind.Sensors"_s, u"Cpu"_s).toDouble() == 52, 8000);

        QCOMPARE(daemon.call(u"Quit"_s).type(), QDBusMessage::ReplyMessage);
        expectCleanExit(*p);
        QCOMPARE(m_world.calls().count("nbfc "), nbfcRuns);     // NBFC never run
        QFile light(led + u"/multi_intensity"_s);
        QVERIFY(light.open(QIODevice::ReadOnly));
        QCOMPARE(light.readAll(), QByteArray("10 20 30\n"));    // nothing written to it
        QVERIFY(QDir(m_world.sysRoot() + u"/sys/class/leds"_s).removeRecursively());
    }

    // What isn't loaded, and why: not a plugin, built for another SDK, no kind, a link, a file or a folder others can
    // change, the same plugin a second time (the first folder wins); a fan plugin file that makes no fan plugin
    void pluginFolderChecks()
    {
        QTemporaryDir d;
        const QString root = d.path();
        FakeWorld::writeAt(root + u"/HP/notes.so"_s, "hello\n");
        QDir().mkpath(root + u"/Test"_s);
        for (const QString &t : { u"oldsdk"_s, u"nokind"_s, u"emptyfan"_s })
            QVERIFY(QFile::copy(QStringLiteral(TEST_PLUGINS) + u'/' + t + u".so"_s, root + u"/Test/"_s + t + u".so"_s));
        QDir().mkpath(root + u"/Links"_s);
        QVERIFY(QFile::link(QStringLiteral(PLUGINS_DIR) + u"/HP/hp-wmi-keyboard-zones.so"_s, root + u"/Links/zones.so"_s));
        QDir().mkpath(root + u"/GW"_s);
        QVERIFY(QFile::copy(QStringLiteral(PLUGINS_DIR) + u"/HP/hp-wmi-keyboard-zones.so"_s, root + u"/GW/zones.so"_s));
        QVERIFY(QFile::setPermissions(root + u"/GW/zones.so"_s, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::WriteGroup));
        QDir().mkpath(root + u"/Open"_s);
        QVERIFY(QFile::copy(QStringLiteral(PLUGINS_DIR) + u"/NBFC/nbfc.so"_s, root + u"/Open/nbfc.so"_s));
        QVERIFY(QFile::setPermissions(root + u"/Open"_s, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
                                                         | QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup));
        // (a folder others can change, above the plugin's own folder)
        QDir().mkpath(root + u"/Shared/HP"_s);
        QVERIFY(QFile::copy(QStringLiteral(PLUGINS_DIR) + u"/HP/hp-wmi-rgb-zones.so"_s, root + u"/Shared/HP/hp-wmi-rgb-zones.so"_s));
        QVERIFY(QFile::setPermissions(root + u"/Shared"_s, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
                                                           | QFile::ReadOther | QFile::WriteOther | QFile::ExeOther));
        for (const QString &sub : { u"A"_s, u"B"_s }) {
            QDir().mkpath(root + u'/' + sub);
            QVERIFY(QFile::copy(QStringLiteral(PLUGINS_DIR) + u"/HP/hp-wmi-keyboard.so"_s, root + u'/' + sub + u"/hp-wmi-keyboard.so"_s));
        }
        QProcessEnvironment env;
        env.insert(u"SOLWIND_PLUGIN_PATH"_s, root);
        auto p = start(env);
        QVERIFY(p->waitForStarted(3000));
        QTRY_VERIFY_WITH_TIMEOUT(nameOwned(), 5000);
        const VariantMapList list = qvariant_cast<VariantMapList>(getProperty(path, service, u"Plugins"_s));
        QCOMPARE(list.size(), 2);
        QCOMPARE(list[0].value(u"id"_s).toString(), u"Test/empty-fan"_s);       // (priority 1)
        QCOMPARE(list[0].value(u"used"_s).toBool(), false);
        QCOMPARE(list[1].value(u"id"_s).toString(), u"HP/hp-wmi-keyboard"_s);
        QCOMPARE(list[1].value(u"file"_s).toString(), root + u"/A/hp-wmi-keyboard.so"_s);
        QCOMPARE(list[1].value(u"used"_s).toBool(), true);
        QMap<QString, QString> why;
        for (const QVariantMap &m : qvariant_cast<VariantMapList>(getProperty(path, service, u"PluginFailures"_s)))
            why.insert(m.value(u"file"_s).toString().mid(root.size() + 1), m.value(u"why"_s).toString());
        QCOMPARE(why.value(u"HP/notes.so"_s), u"not a Solwind plugin"_s);
        QCOMPARE(why.value(u"Test/oldsdk.so"_s), u"built for Solwind 0.0.0, this is "_s + QStringLiteral(SOLWIND_SDK_ID) + u": build it again"_s);
        QVERIFY(QStringLiteral(SOLWIND_SDK_ID).startsWith(QStringLiteral(SOLWIND_VERSION) + u'+'));
        QCOMPARE(why.value(u"Test/nokind.so"_s), u"no id or kind in its metadata"_s);
        QCOMPARE(why.value(u"Open/nbfc.so"_s), u"others can change its folder "_s + root + u"/Open"_s);
        QCOMPARE(why.value(u"B/hp-wmi-keyboard.so"_s), u"HP/hp-wmi-keyboard is loaded already from another folder"_s);
        QCOMPARE(why.value(u"Shared/HP/hp-wmi-rgb-zones.so"_s), u"others can change its folder "_s + root + u"/Shared"_s);
        QCOMPARE(why.value(u"Links/zones.so"_s), u"a link (install the file itself)"_s);
        QCOMPARE(why.value(u"GW/zones.so"_s), u"others can change it"_s);
        QCOMPARE(why.size(), 8);
        QCOMPARE(getProperty(u"/solwind/Fans"_s, u"solwind.Fans"_s, u"Plugin"_s).toString(), QString());
        QVERIFY(signalProcess(*p, SIGTERM));
        expectCleanExit(*p);
    }

    void observeModeWritesNothing()
    {
        // Whatever ran in these tests: no command that changes NBFC, and no settings file
        const QByteArray c = m_world.calls();
        QVERIFY(c.contains("systemctl is-enabled nbfc_service"));   // the daemons' calls are in this log
        QVERIFY2(!c.contains("nbfc set") && !c.contains("nbfc start") && !c.contains("nbfc stop")
                 && !c.contains("nbfc restart") && !c.contains("config -s"), c.constData());
        QVERIFY(!QFile::exists(m_home.path() + u"/solwind/settings.json"_s));
    }

    void noSessionBus()
    {
        QProcessEnvironment env;
        env.insert(u"DBUS_SESSION_BUS_ADDRESS"_s, u"unix:path="_s + m_home.path() + u"/no-bus"_s);
        auto p = start(env);
        QVERIFY(p->waitForFinished(5000));
        QCOMPARE(p->exitCode(), 1);
        QVERIFY(QString::fromUtf8(p->readAllStandardError()).contains(u"no D-Bus session bus"_s));
    }
};

QTEST_GUILESS_MAIN(TstDaemon)
#include "tst_daemon.moc"
