// solwindd as a process: D-Bus name, Status/Quit, single instance, clean quit on signals. Runs only on a private
// session bus (ctest starts it under dbus-run-session); refuses to run on the user's own bus.
#include "dbus/DaemonApi.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
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
} // namespace

class TstDaemon : public QObject
{
    Q_OBJECT

    QTemporaryDir m_home;

    // A daemon with its own settings folder
    std::unique_ptr<QProcess> start(QProcessEnvironment env = QProcessEnvironment::systemEnvironment())
    {
        env.insert(u"XDG_CONFIG_HOME"_s, m_home.path());
        env.remove(u"SOLWIND_SYS"_s);
        auto p = std::make_unique<QProcess>();
        p->setProcessEnvironment(env);
        p->setProgram(QStringLiteral(SOLWINDD_PATH));
        p->start();
        return p;
    }
    void startAndWait(std::unique_ptr<QProcess> &p)
    {
        p = start();
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
    }

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
        QCOMPARE(st.value().value(u"sysRoot"_s).toString(), QString());
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
        ::kill(pid_t(first->processId()), SIGTERM);
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
        ::kill(pid_t(p->processId()), signo);
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
        ::kill(pid_t(p->processId()), SIGTERM);
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
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
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

    void noSessionBus()
    {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(u"DBUS_SESSION_BUS_ADDRESS"_s, u"unix:path="_s + m_home.path() + u"/no-bus"_s);
        auto p = start(env);
        QVERIFY(p->waitForFinished(5000));
        QCOMPARE(p->exitCode(), 1);
        QVERIFY(QString::fromUtf8(p->readAllStandardError()).contains(u"no D-Bus session bus"_s));
    }
};

QTEST_GUILESS_MAIN(TstDaemon)
#include "tst_daemon.moc"
