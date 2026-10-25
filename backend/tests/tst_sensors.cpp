// The Sensors service: reading in the worker thread, hardware names first, P-2 pace, P-3 (no stale values).
#include "TestEnv.h"
#include "core/Sensors.h"

#include <QElapsedTimer>
#include <QSignalSpy>

#include <memory>

using namespace Qt::StringLiterals;

class TstSensors : public QObject
{
    Q_OBJECT
    FakeWorld w;

    Paths paths() const
    {
        Paths p;
        p.sysRoot = w.sysRoot();
        return p;
    }

private slots:
    void initTestCase()
    {
        w.applyToThisProcess();
        w.hwmon(0, "coretemp", { { 1, "Package id 0", "52000\n" } });
        w.hwmon(1, "hp", {}, { { 1, "3100\n" }, { 2, "3400\n" } });
        w.pci(u"0000:0e:00.0"_s, "0x10de", "0x030000", "active");
        w.write(u"smi-temp"_s, "47\n");
        w.write(u"lspci/0000:0e:00.0"_s, "Device:\tNVIDIA GeForce RTX 3050 6GB Laptop GPU\n");
    }

    void readsWhenActive()
    {
        Sensors s(paths());
        s.start();
        QSignalSpy changed(&s, &Sensors::changed);
        QTest::qWait(300);
        QVERIFY(s.state().readings.isEmpty());                // not active: nothing read
        s.setActive(true);                                    // reads at once (triggeredOnStart)
        QTRY_COMPARE(s.state().cpu(), 52.0);
        QTRY_COMPARE(s.state().gpu(), 47.0);                  // fast by default: the GPU every round
        QCOMPARE(s.state().hw.gpu, u"GeForce RTX 3050 6GB Laptop GPU"_s);   // names came first
        QCOMPARE(s.state().readings[0].model.isEmpty(), false);
        QCOMPARE(s.state().rpms, (QList<double>{ 3100, 3400 }));
        QVERIFY(changed.count() >= 1);
        // Turned off: values gone at once, and a round that was still running doesn't bring them back
        s.setActive(false);
        QVERIFY(std::isnan(s.state().cpu()));
        QVERIFY(s.state().rpms.isEmpty());
        QTest::qWait(500);
        QVERIFY(std::isnan(s.state().cpu()));
    }
    void pace()
    {
        Sensors s(paths());
        s.start();
        QCOMPARE(s.interval(), 3000);
        s.setFast(false);
        QCOMPARE(s.interval(), 5000);
        s.setFast(true);
        QCOMPARE(s.interval(), 3000);
    }
    void slowSkipsGpu()
    {
        // Not fast: the first round reads the GPU (tick 0), the next three don't (nvidia||skip keeps the last value)
        Sensors s(paths());
        s.start();
        s.setFast(false);
        w.write(u"calls.log"_s, "");
        s.setActive(true);
        QTRY_COMPARE(s.state().gpu(), 47.0);
        QCOMPARE(w.count("nvidia-smi"), 1);
        for (int i = 0; i < 3; ++i) {                         // rounds 2-4: skipped, the value is kept
            s.pollNow();
            QTRY_VERIFY(!s.state().readings.isEmpty());
            QTest::qWait(150);
        }
        QCOMPARE(w.count("nvidia-smi"), 1);
        QCOMPARE(s.state().gpu(), 47.0);
        s.pollNow();                                          // round 5: read again
        QTRY_COMPARE(w.count("nvidia-smi"), 2);
    }
    void staleRoundDropped()
    {
        // A round started before the sensors were turned off and on again must not count as fresh
        Sensors s(paths());
        SensorReader::Options o;
        o.smiTimeoutMs = 600;                                 // a hanging nvidia-smi makes the round slow
        o.smiKillMs = 100;
        s.setReaderOptions(o);
        s.start();
        w.write(u"smi-hang"_s, "");
        s.setActive(true);                                    // round 1 starts (GPU, hangs ~0.6 s)
        QTest::qWait(150);
        FakeWorld::writeAt(w.sysRoot() + u"/sys/class/hwmon/hwmon0/temp1_input"_s, "61000\n");
        QFile::remove(w.cct() + u"/smi-hang"_s);
        s.setActive(false);
        s.setActive(true);                                    // round 1 is still running
        QList<double> seen;
        connect(&s, &Sensors::changed, this, [&] { seen << s.state().cpu(); });
        QTRY_COMPARE_WITH_TIMEOUT(s.state().cpu(), 61.0, 4000);
        QVERIFY2(!seen.contains(52.0), "the stale round's 52 °C was shown");
    }
    void shutdownIsPrompt()
    {
        // Destroyed while nvidia-smi hangs: doesn't wait for its time limit
        auto s = std::make_unique<Sensors>(paths());
        s->start();
        w.write(u"smi-hang"_s, "");
        s->setActive(true);
        QTest::qWait(300);
        QElapsedTimer t;
        t.start();
        s.reset();
        QVERIFY2(t.elapsed() < 1500, qPrintable(QString::number(t.elapsed())));
        QFile::remove(w.cct() + u"/smi-hang"_s);
    }
};

QTEST_GUILESS_MAIN(TstSensors)
#include "tst_sensors.moc"
