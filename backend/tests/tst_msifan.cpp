// MsiFan against fake sysfs folders: the fan curves of msi_wmi_platform (every point at the speed, a floor, the curves on
// only after they're written, off again on any failure), msi-ec alone (only max fan, reads), what it refuses, the hand-back.
#include "TestEnv.h"
#include "MsiFan.h"
#include "TestHost.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;

class TstMsiFan : public QObject
{
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> d;
    std::unique_ptr<TestHost> host;
    std::unique_ptr<MsiFan> p;
    QString hw, ec;

    static QByteArray read(const QString &path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll().trimmed() : QByteArray();
    }
    static void put(const QString &path, const QByteArray &v) { FakeWorld::writeAt(path, v + "\n"); }
    int file(const QString &n) const { return read(hw + u'/' + n).toInt(); }
    void world(bool curves, bool msiEc)
    {
        d = std::make_unique<QTemporaryDir>();
        hw = d->path() + u"/sys/class/hwmon/hwmon6"_s;
        ec = d->path() + u"/sys/devices/platform/msi-ec"_s;
        put(hw + u"/name"_s, "msi_wmi_platform");
        if (curves) {
            for (int fan = 1; fan <= 2; ++fan) {
                put(hw + u"/pwm%1_enable"_s.arg(fan), "2");
                for (int a = 1; a <= 6; ++a) put(hw + u"/pwm%1_auto_point%2_pwm"_s.arg(fan).arg(a), QByteArray::number(a * 10));
            }
        }
        if (msiEc) {
            put(ec + u"/cooler_boost"_s, "off");
            put(ec + u"/cpu/realtime_temperature"_s, "61");
            put(ec + u"/cpu/realtime_fan_speed"_s, "45");
            put(ec + u"/gpu/realtime_fan_speed"_s, "130");
        }
        put(d->path() + u"/sys/class/dmi/id/board_name"_s, "MS-1591");
        host = std::make_unique<TestHost>();
        host->p.sysRoot = d->path();
        host->p.nbfcRoot = d->path();
    }
    void plugin()
    {
        p = std::make_unique<MsiFan>(*host);
        p->start();
    }
    QString wait(const std::function<void(FanPlugin::Done)> &call)
    {
        bool got = false;
        QString error;
        call([&](bool ok, const QString &e) { got = true; error = ok ? QString() : e; });
        for (int i = 0; i < 200 && !got; ++i) QTest::qWait(10);
        return got ? error : u"no answer"_s;
    }
    QString percent(int fan, int pct) { return wait([&](FanPlugin::Done dn) { p->setPercent(fan, pct, dn); }); }

private slots:
    void floorAndTop()
    {
        QCOMPARE(MsiFan::pwmOf(0), 77);
        QCOMPARE(MsiFan::pwmOf(30), 77);
        QCOMPARE(MsiFan::pwmOf(60), 153);
        QCOMPARE(MsiFan::pwmOf(100), 255);
        QCOMPARE(MsiFan::pwmOf(500), 255);
    }
    void nothingHere()
    {
        world(false, false);
        QVERIFY(!MsiFan::find(host->p).found());
    }
    void curvesEveryPointAtTheSpeed()
    {
        world(true, false);
        plugin();
        QVERIFY(p->canSetSpeeds());
        QCOMPARE(p->configId(), u"MSI MS-1591"_s);
        QCOMPARE(percent(0, 60), QString());
        for (int a = 1; a <= 6; ++a) {
            QCOMPARE(file(u"pwm1_auto_point%1_pwm"_s.arg(a)), 153);
            QCOMPARE(file(u"pwm2_auto_point%1_pwm"_s.arg(a)), a * 10);   // automatic next to a driven fan: its factory curve
        }
        QCOMPARE(file(u"pwm1_enable"_s), 1);
        QCOMPARE(file(u"pwm2_enable"_s), 1);
        QCOMPARE(percent(1, 80), QString());
        QCOMPARE(file(u"pwm2_auto_point6_pwm"_s), 204);
        QCOMPARE(file(u"pwm1_auto_point6_pwm"_s), 153);
        QTRY_COMPARE(p->fans()[1].target, 80.0);
        QVERIFY(!p->fans()[1].autoControl);
        QCOMPARE(wait([&](FanPlugin::Done dn) { p->releaseAll(dn); }), QString());
        QCOMPARE(file(u"pwm1_enable"_s), 2);
        QCOMPARE(file(u"pwm2_enable"_s), 2);
        for (int a = 1; a <= 6; ++a) {                        // the factory curves are back
            QCOMPARE(file(u"pwm1_auto_point%1_pwm"_s.arg(a)), a * 10);
            QCOMPARE(file(u"pwm2_auto_point%1_pwm"_s.arg(a)), a * 10);
        }
    }
    void curvesTurnedOffOutsideAreWrittenAgain()
    {
        world(true, false);
        plugin();
        QCOMPARE(percent(0, 60), QString());
        put(hw + u"/pwm1_enable"_s, "2");                     // something else; the driver may put the factory's back
        put(hw + u"/pwm1_auto_point1_pwm"_s, "10");
        p->setFast(true);
        QSignalSpy spy(p.get(), &FanPlugin::statusRead);
        QVERIFY(spy.wait(4000));
        QVERIFY(p->fans()[0].autoControl);                    // the core writes the speed again
        QCOMPARE(percent(0, 60), QString());
        QCOMPARE(file(u"pwm1_auto_point1_pwm"_s), 153);
        QCOMPARE(file(u"pwm1_enable"_s), 1);
    }
    void aFailedCurveDoesntStay()
    {
        if (geteuid() == 0) QSKIP("root can write anyway");
        world(true, false);
        plugin();
        QCOMPARE(percent(0, 60), QString());
        QCOMPARE(file(u"pwm1_enable"_s), 1);
        QFile::remove(hw + u"/pwm2_auto_point3_pwm"_s);
        QVERIFY(QDir().mkpath(hw + u"/pwm2_auto_point3_pwm"_s));
        QVERIFY(!percent(1, 70).isEmpty());
        QCOMPARE(file(u"pwm1_enable"_s), 2);                  // from 1: the curves off
        QCOMPARE(file(u"pwm2_enable"_s), 2);
    }
    void aFailedHandBackIsntDone()
    {
        if (geteuid() == 0) QSKIP("root can write anyway");
        world(true, false);
        plugin();
        QCOMPARE(percent(0, 60), QString());
        QFile::setPermissions(hw + u"/pwm1_enable"_s, QFile::ReadOwner);
        p->setFast(true);
        QSignalSpy spy(p.get(), &FanPlugin::statusRead);
        QVERIFY(spy.wait(4000));
        QVERIFY(!wait([&](FanPlugin::Done dn) { p->setAuto(-1, dn); }).isEmpty());
        QVERIFY(spy.wait(4000));
        QVERIFY(!p->fans()[0].autoControl);
    }
    void leftoverCurvesGoOff()
    {
        world(true, false);
        put(hw + u"/pwm1_enable"_s, "1");
        put(hw + u"/pwm2_enable"_s, "1");
        plugin();
        QCOMPARE(file(u"pwm1_enable"_s), 2);
        QCOMPARE(file(u"pwm2_enable"_s), 2);
        QVERIFY(p->handBackCommand().contains(hw + u"/pwm1_auto_point1_pwm"_s));   // recorded before start(): read by then
    }
    void noFactoryCurveNoDriving()
    {
        world(true, false);
        put(hw + u"/pwm2_auto_point2_pwm"_s, "not a number");
        plugin();
        QVERIFY(percent(0, 60).contains(u"couldn't be read"_s));
        QCOMPARE(file(u"pwm1_enable"_s), 2);
        QCOMPARE(file(u"pwm1_auto_point1_pwm"_s), 10);
    }
    void flatCurveLeftAfterAnOutsideChangeGoes()
    {
        world(true, false);
        plugin();
        QCOMPARE(percent(0, 60), QString());
        put(hw + u"/pwm1_enable"_s, "2");
        put(hw + u"/pwm2_enable"_s, "2");
        p->setFast(true);
        QSignalSpy spy(p.get(), &FanPlugin::statusRead);
        QVERIFY(spy.wait(4000));
        QCOMPARE(wait([&](FanPlugin::Done dn) { p->setAuto(0, dn); }), QString());
        QCOMPARE(file(u"pwm1_auto_point1_pwm"_s), 10);        // ours was still there: the factory's again
    }
    void aRefusedSpeedIsNotCarriedOutLater()
    {
        world(true, false);
        const QString pid = d->path() + u"/run/nbfc_service.pid"_s;
        put(pid, QByteArray::number(QCoreApplication::applicationPid()));
        plugin();
        QVERIFY(!percent(0, 60).isEmpty());
        QFile::remove(pid);
        p->setFast(true);
        QSignalSpy spy(p.get(), &FanPlugin::statusRead);
        QVERIFY(spy.wait(4000));
        QCOMPARE(wait([&](FanPlugin::Done dn) { p->setAuto(1, dn); }), QString());
        QCOMPARE(file(u"pwm1_enable"_s), 2);
        QCOMPARE(file(u"pwm1_auto_point1_pwm"_s), 10);
    }
    void msiEcOnlyFullSpeed()
    {
        world(false, true);
        plugin();
        QVERIFY(!p->canSetSpeeds());
        QVERIFY(p->running());
        QCOMPARE(p->configId(), u"MSI MS-1591 (msi-ec)"_s);   // not the settings of a kernel with fan curves
        QCOMPARE(p->temp(), 61.0);
        QCOMPARE(p->fans()[0].current, 45.0);
        QCOMPARE(p->fans()[1].current, 100.0);                // (0-150 on some models)
        QCOMPARE(percent(0, 60), QString());
        QCOMPARE(read(ec + u"/cooler_boost"_s), QByteArray("off"));
        QCOMPARE(percent(1, 100), QString());
        QCOMPARE(read(ec + u"/cooler_boost"_s), QByteArray("on"));
        QCOMPARE(percent(1, 90), QString());
        QCOMPARE(read(ec + u"/cooler_boost"_s), QByteArray("off"));   // below 100: the firmware's again
        QCOMPARE(percent(0, 100), QString());
        QCOMPARE(wait([&](FanPlugin::Done dn) { p->releaseAll(dn); }), QString());
        QCOMPARE(read(ec + u"/cooler_boost"_s), QByteArray("off"));
    }
    void bothCurvesWin()
    {
        world(true, true);
        plugin();
        QVERIFY(p->canSetSpeeds());
        QCOMPARE(p->fans()[0].current, 45.0);                 // msi-ec reads
        QCOMPARE(percent(0, 100), QString());
        QCOMPARE(file(u"pwm1_auto_point1_pwm"_s), 255);
        QCOMPARE(read(ec + u"/cooler_boost"_s), QByteArray("off"));
    }
    void backToAutomaticWhileNbfcRuns()
    {
        world(true, false);
        plugin();
        QCOMPARE(percent(0, 60), QString());
        put(d->path() + u"/run/nbfc_service.pid"_s, QByteArray::number(QCoreApplication::applicationPid()));
        p->setFast(true);
        QSignalSpy spy(p.get(), &FanPlugin::statusRead);
        QVERIFY(spy.wait(4000));
        QCOMPARE(wait([&](FanPlugin::Done dn) { p->releaseAll(dn); }), QString());
        QCOMPARE(file(u"pwm1_enable"_s), 2);
    }
    void notNextToNbfc()
    {
        world(true, false);
        put(d->path() + u"/run/nbfc_service.pid"_s, QByteArray::number(QCoreApplication::applicationPid()));
        plugin();
        QVERIFY(p->readOnly());
        QVERIFY(!percent(0, 60).isEmpty());
        QCOMPARE(file(u"pwm1_enable"_s), 2);
    }
    void noPermission()
    {
        if (geteuid() == 0) QSKIP("root can write anyway");
        world(false, true);
        QFile::setPermissions(ec + u"/cooler_boost"_s, QFile::ReadOwner);
        plugin();
        QVERIFY(p->readOnly());
        QVERIFY(p->blockedReason().contains(u"fan-permission"_s));
    }
    void handBack()
    {
        for (const bool curves : { true, false }) {
            world(curves, !curves);
            plugin();
            QCOMPARE(percent(0, 100), QString());
            const QStringList c = p->handBackCommand();
            QProcess sh;
            sh.start(c[0], c.mid(1));
            QVERIFY(sh.waitForFinished(5000));
            QCOMPARE(sh.exitCode(), 0);
            if (curves) {
                QCOMPARE(file(u"pwm1_enable"_s), 2);
                QCOMPARE(file(u"pwm2_enable"_s), 2);
                QCOMPARE(file(u"pwm1_auto_point4_pwm"_s), 40);
            }
            else QCOMPARE(read(ec + u"/cooler_boost"_s), QByteArray("off"));
        }
    }
};

QTEST_GUILESS_MAIN(TstMsiFan)
#include "tst_msifan.moc"
