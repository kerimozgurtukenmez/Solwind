// SensorReader on a fake /sys: the same lines as scripts/sensors.sh and hwinfo.sh would print (SPEC.md S-1..S-3).
// .dev/phase2diff.sh also runs the shell scripts themselves on such trees and on the real machine and compares.
#include "TestEnv.h"
#include "core/SensorReader.h"

#include <QElapsedTimer>
#include <QRegularExpression>

#include <unistd.h>

using namespace Qt::StringLiterals;

class TstReader : public QObject
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
        // hwmon numbers chosen so byte order differs from number order (hwmon10 before hwmon2)
        w.hwmon(0, "ACAD", {});
        w.hwmon(1, "acpitz", { { 1, "", "27800\n" }, { 2, "", "10000\n" } });
        w.hwmon(10, "coretemp", { { 1, "Package id 0", "52000\n" }, { 10, "Core 8", "49000\n" }, { 2, "Core 0", "50000\n" } });
        w.hwmon(2, "hp", { { 1, "", "1000\n" } }, { { 1, "3100\n" }, { 2, "3400\n" }, { 3, "1234" } });   // fan3: no new line
        w.hwmon(3, "nvme", { { 1, "Composite", "40000\n" } });
        w.hwmon(4, "k10temp", { { 1, "", "61000" }, { 2, " Tctl ", " 62000 \n" } });
        FakeWorld::writeAt(w.sysRoot() + u"/sys/class/hwmon/hwmon4/name"_s, "  k10temp \n");       // read -r trims
        w.hwmon(5, "amdgpu", { { 1, "edge", "45000\n" } });
        if (::geteuid() != 0) {                                   // [ -r ] is always true for root
            w.hwmon(6, "i915", { { 1, "", "41000\n" } });
            ::chmod(QFile::encodeName(w.sysRoot() + u"/sys/class/hwmon/hwmon6/temp1_input"_s).constData(), 0);
        }
        // Display devices: an Intel one, an awake NVIDIA card, a sleeping NVIDIA 3D controller, NVIDIA audio
        w.pci(u"0000:00:02.0"_s, "0x8086", "0x030000");
        w.pci(u"0000:0e:00.0"_s, "0x10de", "0x030000", "active");
        w.pci(u"0000:0f:00.0"_s, "0x10de", "0x030200", "suspended");
        w.pci(u"0000:10:00.1"_s, "0x10de", "0x040300", "active");
    }

    void hwmonLines()
    {
        const QString out = SensorReader::round(paths(), false);
        const QString hw = out.left(out.indexOf(u"nvidia"_s));
        QCOMPARE(hw, u"acpitz||27800\nacpitz||10000\n"
                     "coretemp|Core 8|49000\ncoretemp|Package id 0|52000\ncoretemp|Core 0|50000\n"
                     "fan|hp|1|3100\nfan|hp|2|3400\n"
                     "k10temp|Tctl|62000\n"
                     "amdgpu|edge|45000\n"_s);
    }

    void nvidiaNotAGpuRound()
    {
        w.write(u"calls.log"_s, "");
        const QString out = SensorReader::round(paths(), false);
        QVERIFY(out.endsWith(u"nvidia||skip\nnvidia||asleep\n"_s));
        QCOMPARE(w.count("nvidia-smi"), 0);
    }
    void nvidiaValue()
    {
        w.write(u"calls.log"_s, "");
        w.write(u"smi-temp"_s, "50\n48\n");                      // two cards: the first one's, like ${t%%[!0-9]*}
        const QString out = SensorReader::round(paths(), true);
        QVERIFY2(out.endsWith(u"nvidia||50000\nnvidia||asleep\n"_s), qPrintable(out));
        QCOMPARE(w.count("nvidia-smi --query-gpu=temperature.gpu --format=csv,noheader,nounits"), 1);
        QFile::remove(w.cct() + u"/smi-temp"_s);
    }
    void nvidiaFails()
    {
        w.write(u"smi-temp"_s, " 50\n");                         // not starting with a digit
        QVERIFY(SensorReader::round(paths(), true).endsWith(u"nvidia||fail\nnvidia||asleep\n"_s));
        QFile::remove(w.cct() + u"/smi-temp"_s);
        QVERIFY(SensorReader::round(paths(), true).endsWith(u"nvidia||fail\nnvidia||asleep\n"_s));   // no output
    }
    void nvidiaHangs()
    {
        w.write(u"smi-hang"_s, "");
        SensorReader::Options o;
        o.smiTimeoutMs = 300;
        o.smiKillMs = 300;
        QElapsedTimer t;
        t.start();
        QVERIFY(SensorReader::round(paths(), true, o).endsWith(u"nvidia||fail\nnvidia||asleep\n"_s));
        QVERIFY(t.elapsed() < 3000);
        QFile::remove(w.cct() + u"/smi-hang"_s);
    }
    void nvidiaNotInstalled()
    {
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", "/nonexistent");
        const QString out = SensorReader::round(paths(), true);
        qputenv("PATH", path);
        QVERIFY2(out.endsWith(u"amdgpu|edge|45000\nnvidia||asleep\n"_s), qPrintable(out));   // command -v fails: no line
    }

    void hwInfo()
    {
        w.write(u"lspci/0000:00:02.0"_s, "Slot:\t00:02.0\nSDevice:\tSub\nDevice:\tRaptor Lake-P [Iris Xe Graphics]\tx\n");
        w.write(u"lspci/0000:0e:00.0"_s, "Device:\tNVIDIA GeForce RTX 3050\n");
        const QStringList l = SensorReader::hwInfo(paths()).split(u'\n');
        QVERIFY2(QRegularExpression(u"^cpu\\|.*\\|[0-9]+$"_s).match(l[0]).hasMatch(), qPrintable(l[0]));
        QCOMPARE(l[1], u"gpu|0x8086|0000:00:02.0|Raptor Lake-P [Iris Xe Graphics]"_s);
        QCOMPARE(l[2], u"gpu|0x10de|0000:0e:00.0|NVIDIA GeForce RTX 3050"_s);   // no driver file: lspci
        QCOMPARE(l[3], u"gpu|0x10de|0000:0f:00.0|NVIDIA GPU"_s);                // asleep: lspci is not run
        QCOMPARE(l.size(), 5);                                                   // (the audio device isn't a display)
        QVERIFY(!w.calls().contains("lspci -s 0000:0f:00.0"));
    }
};

QTEST_GUILESS_MAIN(TstReader)
#include "tst_reader.moc"
