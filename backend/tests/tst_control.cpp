// FanController: one test (or more) per rule of SPEC.md §6 (C-1..C-12), §7 (B-1..B-5), §11 (CF-1), §10 (QT-2, QT-3),
// on an in-process fake plugin whose commands and reads the test steps by hand, and a hand-moved clock.
#include "FakePlugin.h"
#include "core/FanController.h"
#include "core/I18n.h"

#include <QSignalSpy>
#include <QTest>

#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace {
constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

FanSt fixedSt(double f)
{
    FanSt s;
    s.mode = 1;
    s.fixed = f;
    s.curve = { { 45, 20 }, { 85, 100 } };
    return s;
}
FanSt curveSt(QList<CurvePoint> pts, bool smooth = true, int hyst = 3, const QString &src = u"cpu"_s)
{
    FanSt s;
    s.mode = 2;
    s.curve = std::move(pts);
    s.smooth = smooth;
    s.hyst = hyst;
    s.src = src;
    return s;
}
FanSt autoSt()
{
    FanSt s;
    s.mode = 0;
    return s;
}

struct Rig {
    FakePlugin p;
    I18n i18n;
    FanController c{ p, i18n };
    qint64 t = 1'000'000'000;
    double gpu = NaN;
    explicit Rig(int fans = 2) : p(fans)
    {
        c.now = [this] { return t; };
        c.gpuTemp = [this] { return gpu; };
    }
    // One status read as the daemon handles it (N-12): read, then max fan's end, then the loop
    void round() { p.read(); c.checkBoost(); c.control(); }
    void temp(double v) { p.driverTemp = v; for (auto &f : p.fanList) f.temp = v; }
};
} // namespace

class TstControl : public QObject
{
    Q_OBJECT
private slots:
    // ---------- C-2 ----------
    void applyFixed()
    {
        Rig r;
        r.c.applyTarget(0, fixedSt(0.4));
        QCOMPARE(r.p.pending(), QStringList{ u"spd 0 40"_s });
        QVERIFY(r.c.needsSensors() && r.c.controlling());
    }
    void applyAuto()
    {
        Rig r;
        r.c.applyTarget(1, autoSt());
        QCOMPARE(r.p.pending(), QStringList{ u"auto 1"_s });
        r.c.applyTarget(-1, autoSt());
        QCOMPARE(r.p.pending(), (QStringList{ u"auto 1"_s, u"auto all"_s }));
        QVERIFY(!r.c.controlling());
        r.c.applyTarget(-1, fixedSt(0.5));                    // -1: every current fan
        QCOMPARE(r.c.targets().keys(), (QList<int>{ 0, 1 }));
    }

    // ---------- C-1 ----------
    void noWritesWhenNotPossible_data()
    {
        QTest::addColumn<int>("which");
        QTest::newRow("not running") << 0;
        QTest::newRow("read-only") << 1;
        QTest::newRow("holding") << 2;
        QTest::newRow("trust read-only") << 3;
        QTest::newRow("trust untested") << 4;
        QTest::newRow("quitting") << 5;
    }
    void noWritesWhenNotPossible()
    {
        QFETCH(int, which);
        Rig r;
        if (which == 0) r.p.isRunning = false;
        if (which == 1) r.p.isReadOnly = true;
        if (which == 2) r.p.isHolding = true;
        if (which == 3) r.p.trustLevel = FanPlugin::Trust::ReadOnly;
        if (which == 4) r.p.trustLevel = FanPlugin::Trust::Untested;
        if (which == 5) r.c.quit([](const FanController::QuitResult &) {});
        r.p.log.clear();
        r.c.applyTarget(0, fixedSt(0.4));
        r.round();
        QVERIFY2(!r.p.log.join(u' ').contains(u"spd"_s), qPrintable(r.p.log.join(u", "_s)));
    }
    void untrustedGetsNoCommandAtAll()
    {
        // ARCH-PLAN rule 4: a plugin not trusted to write gets nothing, not even "auto"
        Rig r;
        r.p.trustLevel = FanPlugin::Trust::ReadOnly;
        r.c.applyTarget(0, autoSt());
        r.c.forgetFans();
        QVERIFY(!r.c.setBoost(5));
        QVERIFY(r.p.log.isEmpty());
        QCOMPARE(r.c.boostBlocked(), u"This fan driver isn't trusted to write fan speeds yet"_s);
    }
    void autoIsSentEvenWhenNotRunning()
    {
        // As in the QML (Nbfc.applyTarget): "back to auto" is asked for whatever the state, it can only help
        Rig r;
        r.p.isRunning = false;
        r.c.applyTarget(0, autoSt());
        QCOMPARE(r.p.pending(), QStringList{ u"auto 0"_s });
    }

    // ---------- C-8, C-10 ----------
    void fixedRounds()
    {
        Rig r;
        r.c.applyTarget(0, fixedSt(0.455));
        QCOMPARE(r.p.pending(), QStringList{ u"spd 0 46"_s });   // Math.round(45.5) = 46
    }
    void sendThreshold()
    {
        Rig r(1);
        r.p.autoFinish = true;
        r.temp(50);
        r.c.applyTarget(0, curveSt({ { 40, 20 }, { 60, 40 }, { 80, 99 }, { 81, 100 } }));
        QCOMPARE(r.p.log.last(), u"spd 0 30"_s);
        r.p.log.clear();
        r.temp(51); r.round();                                // 31: a change of 1 is not sent
        QVERIFY(r.p.log.isEmpty());
        r.temp(52); r.round();                                // 32: 2 from what was sent
        QCOMPARE(r.p.log, QStringList{ u"spd 0 32"_s });
        r.temp(80); r.round();                                // 99
        r.temp(81); r.round();                                // exactly 100: sent although only 1 more
        QCOMPARE(r.p.log.last(), u"spd 0 100"_s);
    }
    void zeroIsSentExactly()
    {
        Rig r(1);
        r.p.autoFinish = true;
        r.temp(40);
        r.c.applyTarget(0, curveSt({ { 40, 1 }, { 60, 40 } }, true, 0));
        QCOMPARE(r.p.log.last(), u"spd 0 1"_s);
        r.c.applyTarget(0, curveSt({ { 40, 0 }, { 60, 40 } }, true, 0));   // a new setting: written at once
        QCOMPARE(r.p.log.last(), u"spd 0 0"_s);
    }

    // ---------- C-3 ----------
    void verifyRewrites()
    {
        Rig r(1);
        r.c.applyTarget(0, fixedSt(0.4));
        r.p.finishAll();                                       // NBFC took it: sentSeq = this poll number
        r.round();
        QVERIFY(r.p.pending().isEmpty());                      // it shows 40: fine
        r.p.fanList[0].autoControl = true;                     // NBFC went back to auto (outside Solwind)
        r.round();
        QCOMPARE(r.p.pending(), QStringList{ u"spd 0 40"_s });
        r.p.finishAll();
        r.p.fanList[0].target = 37;                            // another speed than sent
        r.round();
        QCOMPARE(r.p.pending(), QStringList{ u"spd 0 40"_s });
        r.p.finishAll();
        r.p.fanList[0].target = 40.6;                          // less than 1 off: fine
        r.round();
        QVERIFY(r.p.pending().isEmpty());
    }
    void verifyNotInCriticalMode()
    {
        Rig r(1);
        r.c.applyTarget(0, fixedSt(0.4));
        r.p.finishAll();
        r.p.fanList[0].critical = true;                         // NBFC runs it at 100% itself
        r.p.fanList[0].target = 100;
        r.round();
        QVERIFY(r.p.pending().isEmpty());
    }
    void verifyOnlyFromLaterReads()
    {
        Rig r(1);
        r.c.applyTarget(0, fixedSt(0.4));                     // on its way
        r.p.readStartedBefore();                               // a read started before NBFC took it ...
        r.p.finishAll();                                       // ... NBFC takes it (sentSeq = that read's number)
        r.p.fanList[0].autoControl = true;                     // ... and the read shows the old state
        r.p.reads = r.p.polls;
        r.c.control();
        QVERIFY2(r.p.pending().isEmpty(), "judged from a read that started before the speed was taken");
        r.round();                                             // a read started after it: now it counts
        QCOMPARE(r.p.pending(), QStringList{ u"spd 0 40"_s });
    }
    void verifyNotWhileOnTheWay()
    {
        Rig r(1);
        r.c.applyTarget(0, fixedSt(0.4));                     // not finished: +inf
        r.p.fanList[0].autoControl = true;
        r.round();
        QCOMPARE(r.p.pending(), QStringList{ u"spd 0 40"_s });  // still the one, not a second one
        QCOMPARE(r.p.log.size(), 1);
    }

    // ---------- C-11 ----------
    void failedSpeedIsRetriedAndReported()
    {
        Rig r(1);
        QSignalSpy failed(&r.c, &FanController::commandFailed);
        r.c.applyTarget(0, fixedSt(0.4));
        r.p.finishOne(false, u"Error: boom"_s);
        QCOMPARE(failed.count(), 1);
        QCOMPARE(failed[0][0].toString(), u"Could not set the speed of fan 1: Error: boom"_s);
        r.round();                                             // written again on the next round
        QCOMPARE(r.p.pending(), QStringList{ u"spd 0 40"_s });
        r.p.isQuiet = true;                                    // an admin action explains it: not reported
        r.p.finishOne(false, u"x"_s);
        QCOMPARE(failed.count(), 1);
    }

    // ---------- C-6 ----------
    void sourceTemperatures()
    {
        Rig r(1);
        r.p.driverTemp = 55;
        r.p.fanList[0].temp = NaN;                             // the fan's own: unknown -> the service's
        QCOMPARE(r.c.sourceTemp(0, u"cpu"_s), 55.0);
        QCOMPARE(r.c.sourceTemp(0, u"gpu"_s), 55.0);           // no GPU: the CPU
        QCOMPARE(r.c.sourceTemp(0, u"max"_s), 55.0);
        r.gpu = 70;
        QCOMPARE(r.c.sourceTemp(0, u"gpu"_s), 70.0);
        QCOMPARE(r.c.sourceTemp(0, u"max"_s), 70.0);
        r.p.fanList[0].temp = 80;
        QCOMPARE(r.c.sourceTemp(0, u"cpu"_s), 80.0);
        QCOMPARE(r.c.sourceTemp(0, u"max"_s), 80.0);
        r.p.fanList[0].temp = NaN;
        r.p.driverTemp = NaN;
        QCOMPARE(r.c.sourceTemp(0, u"max"_s), 70.0);           // only the GPU known
        QVERIFY(std::isnan(r.c.sourceTemp(0, u"cpu"_s)));
    }

    // ---------- C-7 ----------
    void safetyLimit()
    {
        Rig r(1);
        r.p.autoFinish = true;
        r.temp(89);
        r.c.applyTarget(0, fixedSt(0.3));
        QCOMPARE(r.p.log.last(), u"spd 0 30"_s);
        r.temp(90); r.round();
        QCOMPARE(r.p.log.last(), u"spd 0 100"_s);
        QVERIFY(r.c.hot(0));
        r.temp(87.5); r.round();                               // not 3 °C below yet: held
        QCOMPARE(r.p.log.last(), u"spd 0 100"_s);
        r.temp(NaN); r.round();                                // unreadable: held
        QVERIFY(r.c.hot(0));
        r.temp(86.9); r.round();                               // below 87: back to its setting
        QCOMPARE(r.p.log.last(), u"spd 0 30"_s);
        QVERIFY(!r.c.hot(0));
    }
    // No readable temperature at all (C-7): never held, the fixed speed is written; once held, held
    void safetyWithNoTemperature()
    {
        Rig r(1);
        r.p.autoFinish = true;
        r.temp(NaN);
        r.p.driverTemp = NaN;
        r.c.applyTarget(0, fixedSt(0.3));
        QCOMPARE(r.p.log.last(), u"spd 0 30"_s);
        QVERIFY(!r.c.hot(0));
        r.round();
        QVERIFY(!r.c.hot(0));
        r.gpu = 91; r.round();                                 // only the GPU readable: it counts
        QCOMPARE(r.p.log.last(), u"spd 0 100"_s);
        r.gpu = NaN; r.round();
        QVERIFY(r.c.hot(0));
        QCOMPARE(r.p.log.last(), u"spd 0 100"_s);
    }
    void safetyWatchesTheGpuEvenForCpuCurves()
    {
        Rig r(1);
        r.p.autoFinish = true;
        r.temp(50);
        r.c.applyTarget(0, curveSt({ { 40, 20 }, { 60, 40 } }));   // follows the CPU
        r.gpu = 91;
        r.round();
        QCOMPARE(r.p.log.last(), u"spd 0 100"_s);
    }
    void safetyLimitChange()
    {
        Rig r(1);
        r.p.autoFinish = true;
        r.temp(75);
        r.c.setSafety(80);
        r.c.applyTarget(0, fixedSt(0.3));
        QCOMPARE(r.p.log.last(), u"spd 0 30"_s);
        r.c.setSafety(70);
        r.round();
        QCOMPARE(r.p.log.last(), u"spd 0 100"_s);
    }

    // ---------- C-9 ----------
    void curveAt()
    {
        const FanSt s = curveSt({ { 40, 20 }, { 60, 60 }, { 80, 100 } });
        QCOMPARE(FanController::curveAt(s, 30), 20.0);         // below the first point: its speed
        QCOMPARE(FanController::curveAt(s, 40), 20.0);
        QCOMPARE(FanController::curveAt(s, 50), 40.0);         // smooth: in between
        QCOMPARE(FanController::curveAt(s, 70), 80.0);
        QCOMPARE(FanController::curveAt(s, 95), 100.0);        // above the last: its speed
        FanSt stepped = s;
        stepped.smooth = false;
        QCOMPARE(FanController::curveAt(stepped, 59.9), 20.0);  // stepped: the lower point's speed
        QCOMPARE(FanController::curveAt(stepped, 60), 60.0);
        QVERIFY(std::isnan(FanController::curveAt(FanSt{}, 50)));   // no points
    }
    void curveLevelSlowsDownGently()
    {
        Rig r(1);
        const FanSt s = curveSt({ { 40, 20 }, { 60, 60 } }, true, 3);
        QCOMPARE(r.c.curveLevel(0, s, 55), 50.0);              // no level yet: the curve
        r.c.setLevel(0, 50);
        QCOMPARE(r.c.curveLevel(0, s, 58), 56.0);              // up: at once
        QCOMPARE(r.c.curveLevel(0, s, 54), 50.0);              // 54 + 3 = 57 is still above 50: held
        QCOMPARE(r.c.curveLevel(0, s, 50), 46.0);              // 50 + 3 -> 46, within 5 of 50
        QCOMPARE(r.c.curveLevel(0, s, 40), 45.0);              // 43 -> 26, but at most 5 down per round
    }
    void curveRunsTheRampOverRounds()
    {
        Rig r(1);
        r.p.autoFinish = true;
        r.temp(60);
        r.c.applyTarget(0, curveSt({ { 40, 20 }, { 60, 60 } }, true, 3));
        QCOMPARE(r.p.log.last(), u"spd 0 60"_s);
        r.temp(40);
        QStringList sent;
        for (int i = 0; i < 10; ++i) { r.p.log.clear(); r.round(); sent << r.p.log; }
        QCOMPARE(sent, (QStringList{ u"spd 0 55"_s, u"spd 0 50"_s, u"spd 0 45"_s, u"spd 0 40"_s, u"spd 0 35"_s,
                                     u"spd 0 30"_s, u"spd 0 26"_s }));   // 5 per round, down to curveAt(43) = 26
    }
    void curveWithUnknownTemperatureIsSkipped()
    {
        Rig r(1);
        r.temp(NaN);
        r.c.applyTarget(0, curveSt({ { 40, 20 }, { 60, 60 } }));
        QVERIFY(r.p.log.isEmpty());
    }

    // ---------- C-12 ----------
    void releaseConfirmedByALaterRead()
    {
        Rig r(1);
        r.p.fanList[0].autoControl = false;
        QSignalSpy failed(&r.c, &FanController::releaseFailed);
        r.c.applyTarget(0, autoSt());
        r.p.finishAll();                                        // the fake sets auto
        r.round();                                              // seen in a later read: done
        r.t += 60000;
        for (int i = 0; i < 3; ++i) r.round();
        QCOMPARE(r.p.log, QStringList{ u"auto 0"_s });
        QCOMPARE(failed.count(), 0);
    }
    void releaseRetriedThenReported()
    {
        Rig r(1);
        r.p.fanList[0].autoControl = false;
        QSignalSpy failed(&r.c, &FanController::releaseFailed);
        r.c.applyTarget(0, autoSt());
        auto ignore = [&] { r.p.commands.clear(); r.p.fanList[0].autoControl = false; };   // the fan ignores "auto"
        ignore();
        r.round();
        QCOMPARE(r.p.log.size(), 1);                            // less than 4 s: wait
        for (int k = 1; k <= 5; ++k) {
            r.t += 4000;
            r.round();
            QCOMPARE(r.p.log.size(), 1 + k);                    // asked again every 4 s
            ignore();
        }
        r.t += 4000;
        r.p.isQuiet = true;                                     // an admin action: wait
        r.round();
        QCOMPARE(failed.count(), 0);
        r.p.isQuiet = false;
        r.round();
        QCOMPARE(failed.count(), 1);
        QCOMPARE(failed[0][0].toString(), u"Fan 1 could not be handed back to NBFC's automatic control"_s);
        r.t += 60000;
        r.round();
        QCOMPARE(r.p.log.size(), 6);                            // given up: nothing more
    }
    void releaseClockJumpBack()
    {
        Rig r(1);
        r.p.fanList[0].autoControl = false;
        r.c.applyTarget(0, autoSt());
        r.p.commands.clear();
        r.t -= 100000;                                          // the clock went back: counts as time up
        r.round();
        QCOMPARE(r.p.log.size(), 2);
    }

    // ---------- B-1..B-5 ----------
    void boostOnOff()
    {
        Rig r;
        r.p.autoFinish = true;
        r.c.applyTarget(0, fixedSt(0.4));
        r.p.log.clear();
        QVERIFY(r.c.setBoost(15));
        QCOMPARE(r.c.boostUntil(), r.t + 15 * 60000);
        QCOMPARE(r.p.log, (QStringList{ u"spd 0 100"_s, u"spd 1 100"_s }));
        r.p.log.clear();
        r.c.applyTarget(0, fixedSt(0.6));                      // while max fan runs: kept for later, not sent
        QVERIFY(r.p.log.isEmpty());
        r.round();                                              // the new setting forgot what was sent: 100 again,
        QCOMPARE(r.p.log, QStringList{ u"spd 0 100"_s });       // as in Nbfc.qml (applyTarget clears lastSent)
        r.p.log.clear();
        QVERIFY(r.c.setBoost(0));                               // off: fan 0 its setting, fan 1 back to auto
        QCOMPARE(r.p.log, (QStringList{ u"auto 1"_s, u"spd 0 60"_s }));
        QVERIFY(!r.c.boosting());
    }
    void boostBlocked()
    {
        Rig r;
        r.p.isRunning = false;
        QVERIFY(!r.c.setBoost(5));
        QCOMPARE(r.c.boostBlocked(), u"The NBFC service is not running"_s);
        r.p.isRunning = true;
        r.p.isReadOnly = true;
        QVERIFY(!r.c.setBoost(-1));
        r.p.isReadOnly = false;
        r.p.isHolding = true;
        QCOMPARE(r.c.boostBlocked(), u"Waiting for the NBFC service action to finish"_s);
        QVERIFY(r.c.setBoost(0));                               // turning it off always works
        r.p.isHolding = false;
        QVERIFY(r.c.setBoost(-1));
        QCOMPARE(r.c.boostUntil(), -1);
        r.p.isRunning = false;                                  // B-4: on stays on through a stop
        r.round();
        QVERIFY(r.c.boosting());
    }
    void boostEnds()
    {
        Rig r;
        r.p.autoFinish = true;
        QSignalSpy ended(&r.c, &FanController::boostEnded);
        QVERIFY(r.c.setBoost(1));
        r.t += 59000;
        r.round();
        QVERIFY(r.c.boosting());
        r.t += 1000;
        r.round();
        QVERIFY(!r.c.boosting());
        QCOMPARE(ended.count(), 1);
        QVERIFY(r.p.log.contains(u"auto 0"_s) && r.p.log.contains(u"auto 1"_s));
    }

    // ---------- CF-1 ----------
    void forgetFans()
    {
        Rig r(3);
        r.p.autoFinish = true;
        r.c.applyTarget(0, fixedSt(0.4));
        r.p.fanList[2].autoControl = false;
        r.p.autoFinish = false;
        r.c.applyTarget(2, autoSt());                            // releasing, not confirmed
        r.p.commands.clear();
        r.p.log.clear();
        r.c.forgetFans();
        QCOMPARE(r.p.log, (QStringList{ u"auto 0"_s, u"auto 2"_s }));
        QVERIFY(r.c.targets().isEmpty());
        QVERIFY(!r.c.controlling());
    }
    void forgetFansNotWhileBoostingOrStopped()
    {
        Rig r;
        r.p.autoFinish = true;
        r.c.applyTarget(0, fixedSt(0.4));
        QVERIFY(r.c.setBoost(5));
        r.p.log.clear();
        r.c.forgetFans();
        QVERIFY(r.p.log.isEmpty());                             // max fan holds 100 anyway
        Rig s;
        s.p.autoFinish = true;
        s.c.applyTarget(0, fixedSt(0.4));
        s.p.isRunning = false;
        s.p.log.clear();
        s.c.forgetFans();
        QVERIFY(s.p.log.isEmpty());
    }

    // ---------- QT-2, QT-3 ----------
    void quitHandsBack()
    {
        Rig r;
        r.p.autoFinish = true;
        r.c.applyTarget(0, fixedSt(0.4));
        r.p.autoFinish = false;
        int done = 0;
        bool fixed = true;
        bool failed = true;
        r.c.quit([&](const FanController::QuitResult &q) { ++done; fixed = q.fixedSpeedsSaved; failed = q.handBackFailed; });
        QCOMPARE(r.p.pending(), QStringList{ u"release"_s });
        QCOMPARE(done, 0);                                      // only once NBFC has it
        r.p.finishAll();
        QCOMPARE(done, 1);
        QVERIFY(!fixed);
        QVERIFY(!failed);
        r.p.log.clear();
        r.round();
        QVERIFY(r.p.log.isEmpty());                             // nothing written after quitting
    }
    void quitWithNbfcStopped()
    {
        Rig r;
        r.p.autoFinish = true;
        r.c.applyTarget(0, fixedSt(0.4));
        r.p.isRunning = false;
        r.p.fixedSaved = true;
        bool fixed = false;
        r.c.quit([&](const FanController::QuitResult &q) { fixed = q.fixedSpeedsSaved; });
        QVERIFY(fixed);
    }
    // Deliberate (SPEC.md QT-2): a hand-back NBFC didn't take is reported; the QML quit silently
    void quitHandBackFailed()
    {
        Rig r;
        r.p.autoFinish = true;
        r.c.applyTarget(0, fixedSt(0.4));
        r.p.autoFinish = false;
        FanController::QuitResult got;
        int done = 0;
        r.c.quit([&](const FanController::QuitResult &q) { got = q; ++done; });
        r.p.finishOne(false, u"Error: not running"_s);
        QCOMPARE(done, 1);
        QVERIFY(got.handBackFailed);
        QCOMPARE(got.error, u"Error: not running"_s);
        QVERIFY(!got.fixedSpeedsSaved);
    }
    // Deliberate (SPEC.md QT-2): a fan still being handed back counts as owned (the QML forgot it)
    void quitWithAFanStillBeingHandedBack()
    {
        Rig r;
        r.p.autoFinish = true;
        r.c.applyTarget(0, fixedSt(0.4));
        r.p.autoFinish = false;
        r.c.applyTarget(0, autoSt());                           // asked back, not confirmed yet
        QVERIFY(!r.c.owned());
        r.p.finishAll(false);                                   // ... and NBFC didn't take it
        r.p.log.clear();
        r.c.quit([](const FanController::QuitResult &) {});
        QCOMPARE(r.p.log, QStringList{ u"release"_s });
    }
    void quitOwningNothing()
    {
        Rig r;
        bool called = false;
        r.c.quit([&](const FanController::QuitResult &) { called = true; });
        QVERIFY(r.p.log.isEmpty());
        QTRY_VERIFY(called);                                    // (from the event loop)
    }
    void quitWhileBoosting()
    {
        Rig r;
        r.p.autoFinish = true;
        QVERIFY(r.c.setBoost(-1));
        r.p.log.clear();
        r.c.quit([](const FanController::QuitResult &) {});
        QCOMPARE(r.p.log, QStringList{ u"release"_s });
        QVERIFY(!r.c.boosting());
    }
};

QTEST_GUILESS_MAIN(TstControl)
#include "tst_control.moc"
