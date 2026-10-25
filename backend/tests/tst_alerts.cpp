// Alerts and translations: SPEC.md NT-2..NT-6 (what is notified, when, how often), I-1 (the language, the dictionary
// built in from translations.js). Notifications go to a bus that doesn't exist; the `sent` signal is checked.
#include "FakePlugin.h"
#include "core/Alerts.h"
#include "core/FanController.h"
#include "core/I18n.h"
#include "core/Notifier.h"

#include <QDBusConnection>
#include <QFile>
#include <QSignalSpy>
#include <QTest>

#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace {
constexpr double NaN = std::numeric_limits<double>::quiet_NaN();
QDBusConnection noBus() { return QDBusConnection::connectToBus(u"unix:path=/nonexistent/solwind-test"_s, u"none"_s); }

struct Rig {
    FakePlugin p{ 1 };
    I18n i18n;
    Notifier n{ noBus() };
    FanController c{ p, i18n };
    Alerts a{ p, c, i18n, n };
    QSignalSpy sent{ &n, &Notifier::sent };
    qint64 t = 1'000'000'000;
    double cpu = NaN, gpu = NaN;
    Rig()
    {
        a.now = [this] { return t; };
        a.sensorCpu = [this] { return cpu; };
        a.sensorGpu = [this] { return gpu; };
        c.gpuTemp = [this] { return gpu; };
    }
    void temp(double v) { p.driverTemp = v; a.update(); }
    QString title(int i) const { return sent[i][0].toString(); }
    QString body(int i) const { return sent[i][1].toString(); }
};
} // namespace

class TstAlerts : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("LANG", "en_US.UTF-8"); }

    // ---------- NT-2 ----------
    void criticalOnceAndRearm()
    {
        Rig r;
        r.temp(80);
        r.temp(91);
        QCOMPARE(r.sent.count(), 0);                            // worded 300 ms later
        QTRY_COMPARE(r.sent.count(), 1);
        QCOMPARE(r.title(0), u"Critical temperature: 91°C (CPU)"_s);
        QCOMPARE(r.body(0), u"Safety limit (90°C) exceeded; the fans are under NBFC control."_s);
        QCOMPARE(r.sent[0][2].toInt(), int(Notifier::Critical));
        QCOMPARE(r.sent[0][3].toString(), u"dialog-warning"_s);
        r.temp(95);
        r.temp(86);                                             // not 5 °C below yet
        r.temp(92);
        QTest::qWait(500);
        QCOMPARE(r.sent.count(), 1);
        r.temp(84.9);                                           // re-armed
        r.temp(90);
        QTRY_COMPARE(r.sent.count(), 2);
    }
    void criticalSaysWhatHappens()
    {
        Rig r;
        r.p.isRunning = false;
        r.temp(91);
        QTRY_COMPARE(r.sent.count(), 1);
        QCOMPARE(r.body(0), u"Safety limit (90°C) exceeded; NBFC isn't running, so Solwind can't change the fan speed."_s);
        Rig ro;
        ro.p.isReadOnly = true;
        ro.temp(91);
        QTRY_COMPARE(ro.sent.count(), 1);
        QCOMPARE(ro.body(0), u"Safety limit (90°C) exceeded; NBFC is in read-only mode, so fan speeds can't be set."_s);
        Rig own;
        own.p.autoFinish = true;
        own.p.driverTemp = 50;
        FanSt st;
        st.mode = 1;
        own.c.applyTarget(0, st);
        own.temp(91);
        QTRY_COMPARE(own.sent.count(), 1);
        QCOMPARE(own.body(0), u"Safety limit (90°C) exceeded; fans driven by Solwind were set to 100%."_s);
    }
    void criticalFromTheGpu()
    {
        Rig r;
        r.temp(70);
        r.gpu = 93;
        r.a.update();
        QTRY_COMPARE(r.sent.count(), 1);
        QCOMPARE(r.title(0), u"Critical temperature: 93°C (GPU)"_s);
    }
    void kernelCpuWhileNbfcReportsNothing()
    {
        Rig r;
        r.p.driverTemp = NaN;
        r.cpu = 92.4;
        r.a.update();
        QTRY_COMPARE(r.sent.count(), 1);
        QCOMPARE(r.title(0), u"Critical temperature: 92°C (CPU)"_s);
    }
    void lowerLimit()
    {
        Rig r;
        r.c.setSafety(75);
        r.temp(76);
        QTRY_COMPARE(r.sent.count(), 1);
        QCOMPARE(r.body(0), u"Safety limit (75°C) exceeded; the fans are under NBFC control."_s);
    }
    void notificationsOff()
    {
        Rig r;
        r.n.enabled = [] { return false; };
        r.temp(91);
        QTest::qWait(500);
        QCOMPARE(r.sent.count(), 0);
    }

    // ---------- NT-3..NT-6 ----------
    void serviceLost()
    {
        Rig r;
        emit r.p.serviceLost();
        QCOMPARE(r.sent.count(), 1);
        QCOMPARE(r.title(0), u"NBFC service stopped"_s);
        QCOMPARE(r.body(0), u"Fan control is not possible. Restart it from Solwind or with `sudo nbfc start`."_s);
        QCOMPARE(r.sent[0][3].toString(), u"dialog-error"_s);
    }
    void failuresAtMostEveryFiveMinutes()
    {
        Rig r;
        emit r.c.commandFailed(u"a"_s);
        r.t += 299999;
        emit r.c.commandFailed(u"b"_s);
        QCOMPARE(r.sent.count(), 1);
        emit r.c.releaseFailed(u"c"_s);                          // its own limit
        QCOMPARE(r.sent.count(), 2);
        QCOMPARE(r.title(1), u"Could not hand a fan back to NBFC"_s);
        r.t += 1;
        emit r.c.commandFailed(u"d"_s);
        QCOMPARE(r.sent.count(), 3);
        QCOMPARE(r.title(2), u"Could not set the fan speed"_s);
        QCOMPARE(r.body(2), u"d"_s);
        r.t -= 1000000;                                          // the clock went back: allowed
        emit r.c.commandFailed(u"e"_s);
        QCOMPARE(r.sent.count(), 4);
    }
    void boostEnded()
    {
        Rig r;
        emit r.c.boostEnded();
        QCOMPARE(r.title(0), u"Max fan ended"_s);
        QCOMPARE(r.body(0), u"The fans went back to their previous settings."_s);
        QCOMPARE(r.sent[0][2].toInt(), int(Notifier::Low));
    }

    // ---------- I-1 ----------
    void translations()
    {
        I18n i;
        QVERIFY(i.dict().contains(u"tr"_s));
        QVERIFY(i.dict().value(u"tr"_s).size() > 250);
        QString saved;
        i.savedLanguage = [&] { return saved; };
        saved = u"tr"_s;
        QCOMPARE(i.t(u"Max fan ended"_s), u"Maksimum fan sona erdi"_s);    // (translations.js)
        QCOMPARE(i.t(u"no such text"_s), u"no such text"_s);    // missing: English
        QCOMPARE(i.num(1.5, 1), u"1,5"_s);
        saved = u"en"_s;
        QCOMPARE(i.t(u"Max fan ended"_s), u"Max fan ended"_s);
        QCOMPARE(i.num(1.5, 1), u"1.5"_s);
        saved = QString();                                      // the system locale (LANG en_US)
        QCOMPARE(i.lang(), u"en"_s);
    }
    void parser()
    {
        QHash<QString, QHash<QString, QString>> d;
        QString why;
        QVERIFY(I18n::parse(u".pragma library\n// c\n/* block */ var dict = { tr: { \"a\\\"b\": 'c\\u00e7\\n', 'k': \"v\", }, de: {} }"_s, &d, &why));
        QCOMPARE(d.value(u"tr"_s).value(u"a\"b"_s), u"cç\n"_s);
        QCOMPARE(d.value(u"tr"_s).value(u"k"_s), u"v"_s);
        QVERIFY(d.contains(u"de"_s));
        QVERIFY(!I18n::parse(u"var dict = { tr: { \"a\": 5 } }"_s, &d, &why));
        QVERIFY(!I18n::parse(u"var x = {}"_s, &d, &why));
        QVERIFY(!I18n::parse(u"var dict = { tr: { \"a\": \"b }"_s, &d, &why));
    }
};

QTEST_GUILESS_MAIN(TstAlerts)
#include "tst_alerts.moc"
