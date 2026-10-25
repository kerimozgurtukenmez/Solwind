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
