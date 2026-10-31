// The pure parts of Nbfc.qml and Sensors.qml (SPEC.md N-1..N-8, A-10..A-12, S-4..S-11, Q-5). The same functions are
// also compared with the QML itself by .dev/phase2diff.sh.
#include "core/Sensors.h"
#include "NbfcParse.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {
// The real nbfc-linux 0.4.1 output (dev machine, 2026-10-25)
const QString realStatus = u"Read-only                : false\n"
                           "Selected Config Name     : HP Victus 16-r0xxx\n\n"
                           "Fan Display Name         : CPU Fan\nTemperature              : 49.86\n"
                           "Auto Control Enabled     : false\nCritical Mode Enabled    : false\n"
                           "Current Fan Speed        : 38.00\nTarget Fan Speed         : 44.00\nFan Speed Steps          : 100\n\n"
                           "Fan Display Name         : GPU Fan\nTemperature              : 49.86\n"
                           "Auto Control Enabled     : true\nCritical Mode Enabled    : true\n"
                           "Current Fan Speed        : 100.00\nTarget Fan Speed         : 100.00\nFan Speed Steps          : 100\n"_s;
QJsonValue val(const char *json) { return QJsonDocument::fromJson(QByteArray("[") + json + "]").array().at(0); }
} // namespace

class TstParse : public QObject
{
    Q_OBJECT
private slots:
    // ---------- NBFC ----------
    void statusReal()
    {
        const Nbfc::Status s = Nbfc::parseStatus(realStatus);
        QCOMPARE(s.readOnly, std::optional<bool>(false));
        QCOMPARE(s.config, u"HP Victus 16-r0xxx"_s);
        QCOMPARE(s.fans.size(), 2);
        QCOMPARE(s.fans[0].name, u"CPU Fan"_s);
        QCOMPARE(s.fans[0].temp, 49.86);
        QCOMPARE(s.fans[0].autoControl, false);
        QCOMPARE(s.fans[0].current, 38.0);
        QCOMPARE(s.fans[0].target, 44.0);
        QCOMPARE(s.fans[1].autoControl, true);
        QCOMPARE(s.fans[1].critical, true);
        QCOMPARE(s.temp, 49.86);                              // N-8: no service temperature -> the hottest fan's
    }
    void statusVariants()
    {
        // No fan names (older output): a new fan at every "Current Fan Speed"; service temperature given
        const Nbfc::Status a = Nbfc::parseStatus(u"Temperature: 61.5\nRead-only: yes\nCurrent Fan Speed: 20\nTarget Fan Speed: x\n"
                                                 "Current Fan Speed: 30\nAuto Control Enabled: 1\n"_s);
        QCOMPARE(a.fans.size(), 2);
        QCOMPARE(a.fans[0].name, u"Fan 1"_s);
        QCOMPARE(a.fans[1].name, u"Fan 2"_s);
        QCOMPARE(a.fans[0].target, 0.0);                      // parseFloat("x") || 0
        QVERIFY(a.fans[1].autoControl);
        QCOMPARE(a.temp, 61.5);
        QCOMPARE(a.readOnly, std::optional<bool>(true));
        // Nothing useful
        const Nbfc::Status b = Nbfc::parseStatus(u"INFO: something\nno colon here\n"_s);
        QVERIFY(!b.readOnly);
        QVERIFY(std::isnan(b.temp));
        QVERIFY(b.fans.isEmpty());
        // "Selected Config Name" after a fan belongs to nobody: not the config
        const Nbfc::Status c = Nbfc::parseStatus(u"Fan Display Name: X\nSelected Config Name: Late\n"_s);
        QCOMPARE(c.config, QString());
    }
    void helpers()
    {
        QCOMPARE(Nbfc::firstLine(u"INFO: no warranty\n  INFO: x\nError: boom\nmore"_s), u"Error: boom"_s);
        QCOMPARE(Nbfc::firstLine(u"INFO: only"_s), u"INFO: only"_s);
        QCOMPARE(Nbfc::lines(u" a \n\n b\n"_s), (QStringList{ u"a"_s, u"b"_s }));
        QVERIFY(Nbfc::isTrue(u" Enabled "_s));
        QVERIFY(Nbfc::isTrue(u"1"_s));
        QVERIFY(!Nbfc::isTrue(u"no"_s));
    }
    void toCurve()
    {
        bool thrown = false;
        auto c = Nbfc::toCurve(val("[{\"UpThreshold\":70,\"DownThreshold\":65,\"FanSpeed\":60},{\"UpThreshold\":50,\"DownThreshold\":46,\"FanSpeed\":20}, {\"UpThreshold\":0,\"DownThreshold\":0,\"FanSpeed\":0},{\"UpThreshold\":\"x\",\"FanSpeed\":10}]"), &thrown);
        QVERIFY(c && !thrown);
        // sorted by UpThreshold, t clamped to 30..100 and strictly increasing, hyst = mean(up-down) of up > 0 rows
        QCOMPARE(c->points, (QList<Nbfc::CurvePoint>{ { 30, 0 }, { 50, 20 }, { 70, 60 } }));
        QCOMPARE(c->hyst, 5);                                 // (4 + 5) / 2 = 4.5 -> 5
        QVERIFY(!Nbfc::toCurve(val("[]"), &thrown));
        QVERIFY(!Nbfc::toCurve(val("[{\"UpThreshold\":40,\"FanSpeed\":1}]"), &thrown));          // one point
        auto crowd = Nbfc::toCurve(val("[{\"UpThreshold\":99,\"FanSpeed\":1},{\"UpThreshold\":99,\"FanSpeed\":2},{\"UpThreshold\":99,\"FanSpeed\":3}]"), &thrown);
        QVERIFY(crowd);
        QCOMPARE(crowd->points.size(), 2);                    // 99, 100, then 101 > 100 stops
        QCOMPARE(crowd->hyst, 3);                             // no DownThreshold: default
        thrown = false;
        QVERIFY(!Nbfc::toCurve(val("[{\"UpThreshold\":40,\"FanSpeed\":1}, null]"), &thrown));
        QVERIFY(thrown);                                      // a null row: TypeError in the QML
    }
    void configCurves()
    {
        const QByteArray f = "{\"FanConfigurations\":[{\"TemperatureThresholds\":[{\"UpThreshold\":40,\"FanSpeed\":0},{\"UpThreshold\":60,\"DownThreshold\":55,\"FanSpeed\":50}]}, {\"FanDisplayName\":\"no table\"}, 5]}";
        const auto c = Nbfc::configCurves(f);
        QCOMPARE(c.size(), 3);
        QVERIFY(c[0] && !c[1] && !c[2]);
        QCOMPARE(c[0]->hyst, 5);
        QVERIFY(Nbfc::configCurves("{ broken").isEmpty());
        QVERIFY(Nbfc::configCurves("null").isEmpty());
        QVERIFY(Nbfc::configCurves("{\"FanConfigurations\":{}}").isEmpty());          // .map isn't a function
        QVERIFY(Nbfc::configCurves("{\"FanConfigurations\":[null]}").isEmpty());      // f.TemperatureThresholds of null
        QVERIFY(Nbfc::configCurves("{\"FanConfigurations\":0}").isEmpty());           // || []
    }
    void mergeAndList()
    {
        QCOMPARE(Nbfc::mergeConfigs({ u"b"_s, u"a"_s, u"b"_s }, { u"cli"_s }, u"z"_s, u""_s),
                 (QStringList{ u"a"_s, u"b"_s, u"z"_s }));                  // folders win, selected added, sorted
        QCOMPARE(Nbfc::mergeConfigs({}, { u"y"_s, u"x"_s }, u""_s, u"x"_s), (QStringList{ u"x"_s, u"y"_s }));
        QCOMPARE(Nbfc::parseConfigList(u"Usage: nbfc\nHP A\n  error: x\nWarning: y\nHP B\n"_s),
                 (QStringList{ u"HP A"_s, u"HP B"_s }));
        QCOMPARE(Nbfc::matchRecommendations(u"HP B   (match 90%)\nHP A\nHP B\nUnknown\n"_s, { u"HP A"_s, u"HP B"_s }),
                 (QStringList{ u"HP B"_s, u"HP A"_s }));
    }

    // ---------- sensors ----------
    void names()
    {
        QCOMPARE(SensorParser::cleanCpu(u"13th Gen Intel(R) Core(TM) i7-13700H"_s), u"Intel Core i7-13700H"_s);
        QCOMPARE(SensorParser::cleanCpu(u"AMD Ryzen 7 5800H with Radeon Graphics"_s), u"AMD Ryzen 7 5800H with Radeon Graphics"_s);
        QCOMPARE(SensorParser::cleanCpu(u"Intel(R) Core(TM) i5-8250U CPU @ 1.60GHz"_s), u"Intel Core i5-8250U"_s);
        QCOMPARE(SensorParser::cleanGpu(u"Raptor Lake-P [Iris Xe Graphics]"_s), u"Iris Xe Graphics"_s);
        QCOMPARE(SensorParser::cleanGpu(u"NVIDIA GeForce RTX 3050 6GB Laptop GPU"_s), u"GeForce RTX 3050 6GB Laptop GPU"_s);
        SensorParser p;
        p.parseHw(u"cpu|13th Gen Intel(R) Core(TM) i7-13700H|20\ngpu|0x8086|0000:00:02.0|Raptor Lake-P [Iris Xe Graphics]\n"
                  "gpu|0x10de|0000:01:00.0|NVIDIA GeForce RTX 3050 6GB Laptop GPU\n"_s);
        QCOMPARE(p.hw.cpu, u"Intel Core i7-13700H"_s);
        QCOMPARE(p.hw.cores, u"20"_s);
        QCOMPARE(p.hw.gpu, u"GeForce RTX 3050 6GB Laptop GPU"_s);
        QCOMPARE(p.hw.igpu, u"Iris Xe Graphics"_s);
        p.parseHw(u"gpu|0x1002|a|AMD dGPU\ngpu|0x10de|b|NVIDIA X\n"_s);    // NVIDIA + AMD: the AMD one is the iGPU
        QCOMPARE(p.hw.gpu, u"X"_s);
        QCOMPARE(p.hw.igpu, u"AMD dGPU"_s);
        p.parseHw(u"gpu|0x1002|a|[Radeon]\n"_s);                          // AMD only: it is "the GPU"
        QCOMPARE(p.hw.gpu, u"Radeon"_s);
        QCOMPARE(p.hw.igpu, QString());
    }
    void round()
    {
        SensorParser p;
        p.parseHw(u"cpu|CPU X|8\ngpu|0x10de|b|NVIDIA X\n"_s);
        p.parse(u"acpitz||27800\ncoretemp|Core 0|49000\ncoretemp|Package id 0|52000\ncoretemp|Core 1|55000\n"
                "fan|hp|1|3100\nfan|hp|2|3400\nfan|other|1|999\nnvidia||50000\n"_s, 1000);
        QCOMPARE(p.readings.size(), 2);
        QCOMPARE(p.cpu(), 52.0);                              // the package, not the hottest core
        QCOMPARE(p.readings[0].threads, u"8"_s);
        QCOMPARE(p.gpu(), 50.0);
        QCOMPARE(p.rpms, (QList<double>{ 3100, 3400 }));      // only the first fan driver
        QVERIFY(p.rpmWorks);
        QCOMPARE(p.rpm(1, 2), 3400.0);
        QVERIFY(std::isnan(p.rpm(1, 3)));                     // NBFC has another number of fans
        // Usage: the CPU's from what /proc/stat's counters did since the round before, the GPU's as nvidia-smi says
        QVERIFY(std::isnan(p.readings[0].usage));
        p.parse(u"usage|cpu|1000|800\nnvidia||50000\nusage|gpu|13\n"_s, 1500);
        QVERIFY(std::isnan(p.readings[0].usage));             // nothing to compare with yet
        QCOMPARE(p.readings[1].usage, 13.0);
        p.parse(u"usage|cpu|1100|875\nnvidia||skip\n"_s, 1600);
        QCOMPARE(p.readings[0].usage, 25.0);
        QCOMPARE(p.readings[1].usage, 13.0);                  // not read this round: the last value
        p.parse(u"usage|cpu|1100|875\nnvidia||asleep\n"_s, 1700);
        QVERIFY(std::isnan(p.readings[0].usage));             // the counters didn't move
        QVERIFY(std::isnan(p.readings[1].usage));
        p.parse(u"usage|gpu|[N/A]\nnvidia||50000\n"_s, 1800);
        QVERIFY(std::isnan(p.readings[1].usage));
        p.deactivate();
        p.parse(u"usage|cpu|1200|900\n"_s, 1900);
        QVERIFY(std::isnan(p.readings[0].usage));             // off in between: not compared
        // CPU fallbacks: k10temp Tctl, then the hottest core, then the first acpitz
        p.parse(u"k10temp|Tccd1|60000\nk10temp|Tctl|65000\n"_s, 2000);
        QCOMPARE(p.cpu(), 65.0);
        p.parse(u"coretemp|Core 0|40000\ncoretemp|Core 1|45000\nacpitz||30000\n"_s, 3000);
        QCOMPARE(p.cpu(), 45.0);
        p.parse(u"acpitz||30000\nacpitz||20000\n"_s, 4000);
        QCOMPARE(p.cpu(), 30.0);
        // iGPU: i915/xe, the last one; amdgpu next to NVIDIA is the iGPU
        p.parseHw(u"gpu|0x8086|a|[Iris]\n"_s);
        p.parse(u"i915||41000\nxe||42000\n"_s, 5000);
        QCOMPARE(p.readings.last().label, u"iGPU"_s);
        QCOMPARE(p.readings.last().value, 42.0);
        p.parseHw(QString());
        p.parse(u"amdgpu|junction|70000\namdgpu|edge|60000\nnvidia||skip\n"_s, 6000);
        QCOMPARE(p.readings.last().value, 60.0);              // edge preferred, and it is the iGPU (NVIDIA exists)
        // A line with the wrong number of fields is not used
        p.parse(u"coretemp|a|b|50000\ncoretemp|Package|30000\n"_s, 7000);
        QCOMPARE(p.cpu(), 30.0);
    }
    void nvidiaStates()
    {
        SensorParser p;
        p.parse(u"nvidia||asleep\n"_s, 0);
        QVERIFY(std::isnan(p.gpu()));
        QCOMPARE(p.readings[1].note, u"asleep"_s);
        p.parse(u"nvidia||50000\n"_s, 0);
        QCOMPARE(p.gpu(), 50.0);
        p.parse(u"nvidia||skip\n"_s, 0);                      // not read this round: the last value
        QCOMPARE(p.gpu(), 50.0);
        p.parse(u"nvidia||asleep\n"_s, 0);
        p.parse(u"nvidia||skip\n"_s, 0);
        QCOMPARE(p.readings[1].note, u"asleep"_s);            // the last note too
        QVERIFY(std::isnan(p.gpu()));
    }
    void nvidiaBackoff()
    {
        // S-9: wait 0 after the 1st failure, then 30 s, 1, 2, 4 ... min, at most 10 min; a success resets
        SensorParser p;
        QStringList logs;
        p.log = [&](const QString &s) { logs << s; };
        const qint64 t0 = 1'000'000;
        qint64 waits[12];
        for (int i = 0; i < 12; ++i) { p.parse(u"nvidia||fail\n"_s, t0); waits[i] = p.nvRetryAt - t0; }
        QCOMPARE(waits[0], 0);
        QCOMPARE(waits[1], 30000);
        QCOMPARE(waits[2], 60000);
        QCOMPARE(waits[5], 480000);
        QCOMPARE(waits[6], 600000);
        QCOMPARE(waits[11], 600000);
        QCOMPARE(p.readings[1].note, u"error"_s);
        QCOMPARE(logs.size(), 2);                             // the 2nd failure, and the 12th (at the cap, every 6th)
        // A skipped round while backing off shows the error, not an older note
        p.parse(u"nvidia||skip\n"_s, t0);
        QCOMPARE(p.readings[1].note, u"error"_s);
        p.parse(u"nvidia||48000\n"_s, t0);
        QCOMPARE(p.nvFails, 0);
        QCOMPARE(p.nvRetryAt, 0);
        QVERIFY(logs.last().contains(u"works again"_s));
    }
    void gpuRounds()
    {
        SensorParser p;
        // not fast: every 4th round; fast: every round; never before the retry time
        QList<bool> slow;
        for (int i = 0; i < 8; ++i) slow << p.nextRoundReadsGpu(false, 0);
        QCOMPARE(slow, (QList<bool>{ true, false, false, false, true, false, false, false }));
        QVERIFY(p.nextRoundReadsGpu(true, 0));
        p.nvRetryAt = 5000;
        QVERIFY(!p.nextRoundReadsGpu(true, 4999));
        QVERIFY(p.nextRoundReadsGpu(true, 5000));
        p.nvRetryAt = 10'000'000;                             // the clock went back: at most 10 min from now
        QVERIFY(!p.nextRoundReadsGpu(true, 0));
        QCOMPARE(p.nvRetryAt, 600000);
    }
    void deactivate()
    {
        SensorParser p;
        p.parse(u"coretemp|Package|50000\nfan|hp|1|3000\n"_s, 0);
        p.deactivate();                                       // P-3
        QVERIFY(std::isnan(p.cpu()));
        QVERIFY(p.rpms.isEmpty());
        QVERIFY(p.rpmWorks);                                  // stays known
    }
};

QTEST_GUILESS_MAIN(TstParse)
#include "tst_parse.moc"
