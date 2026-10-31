// HpWmiFan against a fake hwmon folder: what is written to pwm1_enable, pwm1, pwm2 (the speed floor, never above 255,
// a fan on automatic following a fixed one, both written when going manual), what it refuses (no permission, NBFC
// running), the hand-back, a manual mode left behind.
#include "TestEnv.h"
#include "HpWmiFan.h"
#include "TestHost.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;

class TstHpFan : public QObject
{
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> d;
    std::unique_ptr<TestHost> host;
    std::unique_ptr<HpWmiFan> p;
    QString hw;

    QString root() const { return d->path(); }
    int file(const QString &n) const { return read(hw + u'/' + n).toInt(); }
    static QByteArray read(const QString &path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll().trimmed() : QByteArray();
    }
    static void put(const QString &path, const QByteArray &v) { FakeWorld::writeAt(path, v + "\n"); }
    void world(const QByteArray &mode = "2", const QByteArray &board = "8BBD")
    {
        d = std::make_unique<QTemporaryDir>();
        hw = root() + u"/sys/class/hwmon/hwmon5"_s;
        put(hw + u"/name"_s, "hp");
        put(hw + u"/pwm1_enable"_s, mode);
        put(hw + u"/pwm1"_s, "0");
        put(hw + u"/pwm2"_s, "0");
        put(root() + u"/sys/class/hwmon/hwmon4/name"_s, "coretemp");
        put(root() + u"/sys/class/hwmon/hwmon4/temp1_input"_s, "51000");
        put(root() + u"/sys/class/dmi/id/board_name"_s, board);
        host = std::make_unique<TestHost>();
        host->p.sysRoot = root();
        host->p.nbfcRoot = root();
    }
    void plugin()
    {
        p = std::make_unique<HpWmiFan>(*host);
        p->start();
    }
    // setPercent / setAuto with the answer
    QString wait(const std::function<void(FanPlugin::Done)> &call)
    {
        bool got = false;
        QString error;
        call([&](bool ok, const QString &e) { got = true; error = ok ? QString() : e; });
        for (int i = 0; i < 200 && !got; ++i) QTest::qWait(10);
        return got ? error : u"no answer"_s;
    }
    void percent(int fan, int pct, const QString &wanted = QString())
    {
        QCOMPARE(wait([&](FanPlugin::Done dn) { p->setPercent(fan, pct, dn); }), wanted);
    }

private slots:
    void floorAndTop()
    {
        QCOMPARE(HpWmiFan::pwmOf(0), 106);                    // never below the firmware table's lowest row
        QCOMPARE(HpWmiFan::pwmOf(30), 106);
        QCOMPARE(HpWmiFan::pwmOf(50), 127);
        QCOMPARE(HpWmiFan::pwmOf(100), 255);
        QCOMPARE(HpWmiFan::pwmOf(1000), 255);                 // never above: 256 is 0 in the driver
        QCOMPARE(HpWmiFan::pwmOf(-5), 106);
    }
    void findsTheDriver()
    {
        world();
        QCOMPARE(HpWmiFan::find(host->p), hw);
        QFile::remove(hw + u"/pwm2"_s);                       // one channel only: not for this plugin
        QCOMPARE(HpWmiFan::find(host->p), QString());
    }
    void manualWritesBothFans()
    {
        world();
        plugin();
        QVERIFY(p->running());
        QVERIFY(!p->readOnly());
        QCOMPARE(p->temp(), 51.0);
        percent(0, 70);
        QCOMPARE(file(u"pwm1_enable"_s), 1);
        QCOMPARE(file(u"pwm1"_s), 179);
        QCOMPARE(file(u"pwm2"_s), 179);                       // never pwm 1: the other fan follows (it stood still at 96 °C)
        QTRY_VERIFY(!p->fans()[0].autoControl);
        QCOMPARE(p->fans()[0].target, 70.0);
        QVERIFY(p->fans()[1].autoControl);
        percent(1, 0);
        QCOMPARE(file(u"pwm1"_s), 179);
        QCOMPARE(file(u"pwm2"_s), 106);
    }
    void automatic()
    {
        world();
        plugin();
        percent(0, 70);
        percent(1, 60);
        QCOMPARE(wait([&](FanPlugin::Done dn) { p->setAuto(0, dn); }), QString());
        QCOMPARE(file(u"pwm1_enable"_s), 1);                  // fan 2 still has a speed
        QCOMPARE(file(u"pwm1"_s), HpWmiFan::pwmOf(60));       // follows fan 2
        QCOMPARE(wait([&](FanPlugin::Done dn) { p->setAuto(1, dn); }), QString());
        QCOMPARE(file(u"pwm1_enable"_s), 2);                  // both: the shared mode
        percent(0, 70);
        QCOMPARE(file(u"pwm1_enable"_s), 1);
        QCOMPARE(wait([&](FanPlugin::Done dn) { p->releaseAll(dn); }), QString());
        QCOMPARE(file(u"pwm1_enable"_s), 2);
    }
    void changedOutsideIsSeen()
    {
        world();
        plugin();
        percent(0, 70);
        QTRY_VERIFY(!p->fans()[0].autoControl);
        put(hw + u"/pwm1_enable"_s, "2");                     // something else, or the firmware after sleep
        p->setFast(true);
        QSignalSpy spy(p.get(), &FanPlugin::statusRead);
        QVERIFY(spy.wait(4000));
        QVERIFY(p->fans()[0].autoControl);                    // the core writes the speed again then
        percent(0, 70);
        QCOMPARE(file(u"pwm1_enable"_s), 1);
    }
    void noPermission()
    {
        if (geteuid() == 0) QSKIP("root can write anyway");
        world();
        QFile::setPermissions(hw + u"/pwm1_enable"_s, QFile::ReadOwner);
        plugin();
        QVERIFY(p->readOnly());
        QVERIFY(p->blockedReason().contains(u"fan-permission"_s));
        percent(0, 70, p->blockedReason());
        QCOMPARE(file(u"pwm1"_s), 0);
    }
    void notNextToNbfc()
    {
        world("1");
        put(root() + u"/run/nbfc_service.pid"_s, QByteArray::number(QCoreApplication::applicationPid()));
        plugin();
        QVERIFY(p->readOnly());
        QCOMPARE(file(u"pwm1_enable"_s), 1);                  // not even the leftover: NBFC may be the one using it
        QVERIFY(p->blockedReason().contains(u"NBFC"_s));
        percent(0, 70, p->blockedReason());
        QCOMPARE(file(u"pwm1"_s), 0);
        put(root() + u"/run/nbfc_service.pid"_s, "999999999");  // a pid file left behind
        p->setFast(true);
        QSignalSpy spy(p.get(), &FanPlugin::statusRead);
        QVERIFY(spy.wait(4000));
        QVERIFY(!p->readOnly());
    }
    void leftoverManualModeGoesBack()
    {
        world("1");
        plugin();
        QCOMPARE(file(u"pwm1_enable"_s), 2);
        world("0");
        plugin();
        QCOMPARE(file(u"pwm1_enable"_s), 0);                  // max only cools more
    }
    void aFailedWriteDoesntStayManual()
    {
        if (geteuid() == 0) QSKIP("root can write anyway");
        world();
        plugin();
        percent(0, 70);
        QCOMPARE(wait([&](FanPlugin::Done dn) { p->releaseAll(dn); }), QString());
        QFile::remove(hw + u"/pwm2"_s);                       // can't be written: fails after the mode was
        QVERIFY(QDir().mkpath(hw + u"/pwm2"_s));
        QVERIFY(!wait([&](FanPlugin::Done dn) { p->setPercent(0, 70, dn); }).isEmpty());
        QCOMPARE(file(u"pwm1_enable"_s), 2);
    }
    void aFailedHandBackIsntDone()
    {
        if (geteuid() == 0) QSKIP("root can write anyway");
        world();
        plugin();
        percent(0, 70);
        QFile::setPermissions(hw + u"/pwm1_enable"_s, QFile::ReadOwner);   // the permission went
        QTest::qWait(10);
        p->setFast(true);
        QSignalSpy spy(p.get(), &FanPlugin::statusRead);
        QVERIFY(spy.wait(4000));
        QVERIFY(!wait([&](FanPlugin::Done dn) { p->setAuto(-1, dn); }).isEmpty());
        QVERIFY(spy.wait(4000));
        QVERIFY(!p->fans()[0].autoControl);                   // still manual: the core keeps asking and then reports it
        QVERIFY(!p->fans()[1].autoControl);
    }
    void handBack()
    {
        world("1");
        plugin();
        percent(0, 70);
        const QStringList c = p->handBackCommand();
        QProcess sh;
        sh.start(c[0], c.mid(1));
        QVERIFY(sh.waitForFinished(5000));
        QCOMPARE(sh.exitCode(), 0);
        QCOMPARE(file(u"pwm1_enable"_s), 2);
    }
    void everyBoardOfTheDriver()
    {
        world();
        plugin();
        QCOMPARE(p->trust(), FanPlugin::Trust::WriteTested);
        QCOMPARE(p->configId(), u"HP 8BBD"_s);
        world("2", "8C78");
        plugin();
        QCOMPARE(p->trust(), FanPlugin::Trust::WriteTested);  // every board the driver opens pwm1 and pwm2 on
        QCOMPARE(p->configId(), u"HP 8C78"_s);
    }
};

QTEST_GUILESS_MAIN(TstHpFan)
#include "tst_hpfan.moc"
