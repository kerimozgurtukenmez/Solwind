// solwindd in control mode, end to end: the process on a private D-Bus session (dbus-run-session), the fake nbfc,
// a fake notification server in this test process. Restoring saved settings, the D-Bus control methods and their
// errors, max fan, quitting (D-Bus, SIGTERM) with the fans handed back, observe mode refusing writes, the guard
// against the QML app (TRANSITION), notifications (NT-2, NT-3, NT-7, QT-3). Nothing real is touched.
#include "TestEnv.h"
#include "dbus/DBusUtil.h"
#include "dbus/DaemonApi.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QProcess>

#include <cmath>
#include <csignal>
#include <memory>
#include <sys/stat.h>

using namespace Qt::StringLiterals;

namespace {
const QString service = QString::fromLatin1(DaemonApi::serviceName);
bool nameOwned() { return QDBusConnection::sessionBus().interface()->isServiceRegistered(service); }
} // namespace

// org.freedesktop.Notifications, as far as the daemon uses it
class FakeNotifications : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")
public:
    struct Sent { QString app, summary, body, icon; int urgency; };
    QList<Sent> sent;
public slots:
    uint Notify(const QString &app, uint, const QString &, const QString &summary, const QString &body, const QStringList &,
                const QVariantMap &hints, int)
    {
        sent.append({ app, summary, body, hints.value(u"image-path"_s).toString(), hints.value(u"urgency"_s).toInt() });
        return uint(sent.size());
    }
};

class TstControlD : public QObject
{
    Q_OBJECT

    std::unique_ptr<FakeWorld> w;
    FakeNotifications notes;
    QString runtime;

    QByteArray fanState() const
    {
        QFile f(w->cct() + u"/state.json"_s);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }
    QJsonArray stateFans() const { return QJsonDocument::fromJson(fanState()).object().value(u"fans"_s).toArray(); }
    bool fanIs(int i, bool autoControl, double target = -1) const
    {
        const QJsonObject f = stateFans().at(i).toObject();
        return f.value(u"auto"_s).toBool() == autoControl && (target < 0 || f.value(u"target"_s).toDouble() == target);
    }
    void settings(const QByteArray &json) const { FakeWorld::writeAt(w->settingsHome() + u"/solwind/settings.json"_s, json); }

    std::unique_ptr<QProcess> start(const QStringList &args = {})
    {
        QProcessEnvironment env = w->env();
        env.insert(u"XDG_RUNTIME_DIR"_s, runtime);
        env.insert(u"LANG"_s, u"en_US.UTF-8"_s);
        auto p = std::make_unique<QProcess>();
        p->setProcessEnvironment(env);
        p->setProcessChannelMode(QProcess::ForwardedErrorChannel);
        p->start(QStringLiteral(SOLWINDD_PATH), args);
        return p;
    }
    void startAndWait(std::unique_ptr<QProcess> &p, const QStringList &args = {})
    {
        p = start(args);
        QVERIFY(p->waitForStarted(3000));
        QTRY_VERIFY_WITH_TIMEOUT(nameOwned(), 5000);
    }
    QDBusMessage call(const QString &method, const QVariantList &args = {})
    {
        QDBusMessage m = QDBusMessage::createMethodCall(service, u"/solwind/Fans"_s, u"solwind.Fans"_s, method);
        m.setArguments(args);
        return QDBusConnection::sessionBus().call(m, QDBus::Block, 10000);
    }
    QVariant prop(const QString &name)
    {
        QDBusMessage m = QDBusMessage::createMethodCall(service, u"/solwind/Fans"_s, u"org.freedesktop.DBus.Properties"_s, u"Get"_s);
        m << u"solwind.Fans"_s << name;
        const QDBusMessage r = QDBusConnection::sessionBus().call(m);
        return r.arguments().value(0).value<QDBusVariant>().variant();
    }
    void expectExit(QProcess &p, int code)
    {
        QVERIFY(p.waitForFinished(10000));
        QCOMPARE(p.exitStatus(), QProcess::NormalExit);
        QCOMPARE(p.exitCode(), code);
        QTRY_VERIFY_WITH_TIMEOUT(!nameOwned(), 3000);
    }
    QVariantMap st(int mode, double fixed = 0.5)
    {
        return { { u"mode"_s, mode }, { u"fixed"_s, fixed } };
    }

private slots:
    void initTestCase()
    {
        const QString addr = qEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS");
        const QString loginBus = qEnvironmentVariable("XDG_RUNTIME_DIR") + u"/bus"_s;
        if (qEnvironmentVariable("SOLWIND_TEST_PRIVATE_BUS") != u"1"_s || addr.isEmpty() || addr.contains(loginBus))
            QSKIP("run through ctest (dbus-run-session): these tests must not touch the user's session bus");
        DBusUtil::registerTypes();
        QDBusConnection bus = QDBusConnection::sessionBus();
        QVERIFY(bus.registerObject(u"/org/freedesktop/Notifications"_s, &notes, QDBusConnection::ExportAllSlots));
        QVERIFY(bus.registerService(u"org.freedesktop.Notifications"_s));
    }
    void init()
    {
        w = std::make_unique<FakeWorld>();
        w->state(true, false, u"TestA"_s, 50, { { u"CPU Fan"_s }, { u"GPU Fan"_s } });
        runtime = w->cct() + u"/run"_s;
        QDir().mkpath(runtime + u"/quickshell/by-pid"_s);
        QDir().mkpath(runtime + u"/quickshell/by-id"_s);
        FakeWorld::writeAt(runtime + u"/quickshell/by-pid/12345"_s, "");   // left over from an instance that crashed
        ::chmod(QFile::encodeName(runtime).constData(), 0700);
        notes.sent.clear();
        QVERIFY(!nameOwned());
    }

    void restoresAndQuitsHandingBack()
    {
        settings("{\"configs\":{\"TestA\":{\"fans\":{\"0\":{\"mode\":1,\"fixed\":0.4}}}}}");
        std::unique_ptr<QProcess> p;
        startAndWait(p);
        QTRY_VERIFY_WITH_TIMEOUT(fanIs(0, false, 40), 8000);    // the saved fixed 40% on fan 0
        QVERIFY(fanIs(1, true));                                // fan 1 has nothing saved: left alone
        QTRY_COMPARE_WITH_TIMEOUT(prop(u"Restored"_s).toBool(), true, 3000);
        QCOMPARE(prop(u"Controlling"_s).toBool(), true);
        QDBusInterface api(service, u"/solwind"_s, service);
        QCOMPARE(QDBusReply<QVariantMap>(api.call(u"Status"_s)).value().value(u"mode"_s).toString(), u"control"_s);
        QCOMPARE(QDBusReply<QVariantMap>(api.call(u"Status"_s)).value().value(u"nbfcPollMs"_s).toInt(), 2000);   // fast while driving
        api.call(u"Quit"_s);
        expectExit(*p, 0);
        QVERIFY2(fanIs(0, true) && fanIs(1, true), fanState().constData());   // handed back before it went
        QVERIFY(w->calls().contains("nbfc set -a ok"));
    }
    void sigtermHandsBack()
    {
        settings("{\"configs\":{\"TestA\":{\"global\":{\"on\":true,\"st\":{\"mode\":1,\"fixed\":0.6}}}}}");
        std::unique_ptr<QProcess> p;
        startAndWait(p);
        QTRY_VERIFY_WITH_TIMEOUT(fanIs(0, false, 60) && fanIs(1, false, 60), 8000);
        QVERIFY(signalProcess(*p, SIGTERM));
        expectExit(*p, 0);
        QVERIFY(fanIs(0, true) && fanIs(1, true));
    }
    void controlMethods()
    {
        std::unique_ptr<QProcess> p;
        startAndWait(p);
        QTRY_VERIFY_WITH_TIMEOUT(prop(u"Restored"_s).toBool(), 8000);
        // SetFan: checked like the editor
        QDBusMessage r = call(u"SetFan"_s, { 0, st(1, 0.45) });
        QCOMPARE(r.type(), QDBusMessage::ReplyMessage);
        QTRY_VERIFY_WITH_TIMEOUT(fanIs(0, false, 45), 6000);
        const QVariantMap badCurve{ { u"mode"_s, 2 }, { u"curve"_s, QVariant::fromValue(VariantMapList{ { { u"t"_s, 50 }, { u"s"_s, 10 } },
                                                                                                   { { u"t"_s, 50 }, { u"s"_s, 20 } } }) } };
        r = call(u"SetFan"_s, { 0, badCurve });
        QCOMPARE(r.errorName(), u"solwind.Error.Usage"_s);
        QCOMPARE(r.errorMessage(), u"curve points rise in temperature, at least 1 °C apart"_s);
        const QVariantMap goodCurve{ { u"mode"_s, 2 }, { u"src"_s, u"max"_s }, { u"hyst"_s, 2 },
                                     { u"curve"_s, QVariant::fromValue(VariantMapList{ { { u"t"_s, 40 }, { u"s"_s, 20 } },
                                                                                      { { u"t"_s, 60 }, { u"s"_s, 60 } } }) } };
        r = call(u"SetFan"_s, { 1, goodCurve });
        QCOMPARE(r.type(), QDBusMessage::ReplyMessage);
        QTRY_VERIFY_WITH_TIMEOUT(fanIs(1, false, 40), 6000);    // 50 °C on the curve: 40%
        const auto fs = prop(u"FanSettings"_s).value<QDBusArgument>();
        QCOMPARE(fs.currentSignature(), u"aa{sv}"_s);
        QCOMPARE(call(u"SetFan"_s, { 5, st(0) }).errorName(), u"solwind.Error.Usage"_s);   // no such fan
        QCOMPARE(call(u"SetFan"_s, { 0, QVariantMap{ { u"what"_s, 1 } } }).errorMessage(), u"unknown field: what"_s);
        // Profiles
        r = call(u"ApplyProfile"_s, { u"Quiet"_s, false });
        QCOMPARE(r.arguments().value(0).toString(), u"Applied: Quiet"_s);
        QCOMPARE(prop(u"ActiveProfile"_s).toString(), u"quiet"_s);
        QCOMPARE(prop(u"GlobalOn"_s).toBool(), true);
        r = call(u"ApplyProfile"_s, { u"nope"_s, false });
        QCOMPARE(r.errorName(), u"solwind.Error.Usage"_s);
        QVERIFY(r.errorMessage().startsWith(u"Profile not found: nope\n"_s));
        QCOMPARE(call(u"SaveProfile"_s, { u"Mine"_s }).type(), QDBusMessage::ReplyMessage);
        QCOMPARE(prop(u"ActiveProfile"_s).toString(), u"Mine"_s);
        QCOMPARE(call(u"SaveProfile"_s, { u"Balanced"_s }).errorName(), u"solwind.Error.Usage"_s);
        QCOMPARE(call(u"RemoveProfile"_s, { u"Mine"_s }).type(), QDBusMessage::ReplyMessage);
        QCOMPARE(prop(u"ActiveProfile"_s).toString(), QString());      // it was the active one (as Settings.qml)
        QCOMPARE(call(u"ApplyProfile"_s, { u"quiet"_s, false }).type(), QDBusMessage::ReplyMessage);
        // Max fan, the safety limit
        QCOMPARE(call(u"SetBoost"_s, { qlonglong(-1) }).type(), QDBusMessage::ReplyMessage);
        QTRY_VERIFY_WITH_TIMEOUT(fanIs(0, false, 100) && fanIs(1, false, 100), 6000);
        QCOMPARE(prop(u"BoostUntil"_s).toLongLong(), -1);
        QCOMPARE(call(u"SetBoost"_s, { qlonglong(0) }).type(), QDBusMessage::ReplyMessage);
        QTRY_VERIFY_WITH_TIMEOUT(!fanIs(0, false, 100), 6000);
        QCOMPARE(call(u"SetSafety"_s, { 69 }).errorName(), u"solwind.Error.Usage"_s);
        QCOMPARE(call(u"SetSafety"_s, { 80 }).type(), QDBusMessage::ReplyMessage);
        QCOMPARE(prop(u"Safety"_s).toDouble(), 80.0);
        // Refused while NBFC is read-only
        w->state(true, true, u"TestA"_s, 50, { { u"CPU Fan"_s }, { u"GPU Fan"_s } });
        QTRY_VERIFY_WITH_TIMEOUT(prop(u"ReadOnly"_s).toBool(), 6000);
        r = call(u"SetFan"_s, { 0, st(1, 0.3) });
        QCOMPARE(r.errorName(), u"solwind.Error.Failed"_s);
        QCOMPARE(r.errorMessage(), u"NBFC is in read-only mode, fan speeds can't be written"_s);
        QCOMPARE(call(u"SetBoost"_s, { qlonglong(5) }).errorName(), u"solwind.Error.Failed"_s);
        // Saved
        QDBusInterface(service, u"/solwind"_s, service).call(u"Quit"_s);
        expectExit(*p, 0);
        QFile f(w->settingsHome() + u"/solwind/settings.json"_s);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject d = QJsonDocument::fromJson(f.readAll()).object();
        QCOMPARE(d.value(u"safety"_s).toDouble(), 80.0);
        QCOMPARE(d.value(u"configs"_s).toObject().value(u"TestA"_s).toObject().value(u"active"_s).toString(), u"quiet"_s);
    }
    void observeRefusesAndWritesNothing()
    {
        settings("{\"configs\":{\"TestA\":{\"fans\":{\"0\":{\"mode\":1,\"fixed\":0.4}}}}}");
        std::unique_ptr<QProcess> p;
        startAndWait(p, { u"--observe"_s });
        QTRY_VERIFY_WITH_TIMEOUT(prop(u"Running"_s).toBool(), 6000);
        QTest::qWait(1500);
        QCOMPARE(call(u"SetFan"_s, { 0, st(1, 0.3) }).errorName(), u"solwind.Error.Observe"_s);
        QCOMPARE(call(u"ApplyProfile"_s, { u"quiet"_s, false }).errorName(), u"solwind.Error.Observe"_s);
        QCOMPARE(call(u"StopService"_s).errorName(), u"solwind.Error.Observe"_s);
        // NBFC goes away: the QML app next to it says so, not this one
        QCOMPARE(w->run(u"nbfc"_s, { u"stop"_s }), 0);
        QTRY_VERIFY_WITH_TIMEOUT(!prop(u"Running"_s).toBool(), 12000);
        QTest::qWait(1000);
        QVERIFY(signalProcess(*p, SIGTERM));
        expectExit(*p, 0);
        QVERIFY2(!w->calls().contains("nbfc set"), w->calls().constData());   // the saved 40% was not applied
        QVERIFY(notes.sent.isEmpty());                          // no notifications either
    }
    void refusesToStartNextToTheQmlApp()
    {
        w->write(u"qs-list.json"_s, "[{\"config_path\": \"/usr/local/share/solwind/shell.qml\", \"pid\": 1}]");
        settings("{\"configs\":{\"TestA\":{\"fans\":{\"0\":{\"mode\":1,\"fixed\":0.4}}}}}");
        auto p = start();
        QVERIFY(p->waitForFinished(8000));
        QCOMPARE(p->exitCode(), 3);
        QVERIFY(!w->calls().contains("nbfc"));                  // didn't even read
        auto o = start({ u"--observe"_s });                     // observe mode may run next to it
        QVERIFY(o->waitForStarted(3000));
        QTRY_VERIFY_WITH_TIMEOUT(nameOwned(), 5000);
        QVERIFY(signalProcess(*o, SIGTERM));
        expectExit(*o, 0);
    }
    void knowsTheQmlAppByItsAppId()
    {
        // In a folder of any name (a clone called Ccenter-Caelestia); Caelestia's own shell next to it is no reason
        w->write(u"Ccenter-Caelestia/shell.qml"_s, "//@ pragma AppId ccenter\nimport Quickshell\n");
        w->write(u"caelestia/shell.qml"_s, "//@ pragma UseQApplication\nimport Quickshell\n");
        const QString list = u"[{\"config_path\": \"%1/caelestia/shell.qml\", \"pid\": 5}, {\"config_path\": \"%1/Ccenter-Caelestia/shell.qml\", \"pid\": 6}]"_s.arg(w->cct());
        w->write(u"qs-list.json"_s, list.toUtf8());
        auto p = start();
        QVERIFY(p->waitForFinished(8000));
        QCOMPARE(p->exitCode(), 3);
        w->write(u"qs-list.json"_s, u"[{\"config_path\": \"%1/caelestia/shell.qml\", \"pid\": 5}]"_s.arg(w->cct()).toUtf8());
        std::unique_ptr<QProcess> q;
        startAndWait(q);                                        // Caelestia alone: starts
        QDBusInterface(service, u"/solwind"_s, service).call(u"Quit"_s);
        expectExit(*q, 0);
    }
    void noStartWhenItCantTell()
    {
        w->write(u"qs-fail"_s, "");
        auto p = start();
        QVERIFY(p->waitForFinished(8000));
        QCOMPARE(p->exitCode(), 1);                             // fails safe (and is restarted by a service)
        QVERIFY(!w->calls().contains("nbfc"));
    }
    void refusesChangesWhileQuitting()
    {
        settings("{\"configs\":{\"TestA\":{\"fans\":{\"0\":{\"mode\":1,\"fixed\":0.4}}}}}");
        std::unique_ptr<QProcess> p;
        startAndWait(p);
        QTRY_VERIFY_WITH_TIMEOUT(fanIs(0, false, 40), 8000);
        w->write(u"slow"_s, "");                               // nbfc set takes 6 s from now on: the hand-back too
        QDBusInterface(service, u"/solwind"_s, service).call(u"Quit"_s);
        QDBusMessage r = call(u"SetFan"_s, { 1, st(1, 0.3) });
        QCOMPARE(r.errorName(), u"solwind.Error.Failed"_s);
        QCOMPARE(r.errorMessage(), u"Solwind is quitting"_s);
        QCOMPARE(call(u"SetBoost"_s, { qlonglong(-1) }).errorMessage(), u"Solwind is quitting"_s);
        QCOMPARE(call(u"ApplyProfile"_s, { u"quiet"_s, false }).errorMessage(), u"Solwind is quitting"_s);
        expectExit(*p, 0);
        QVERIFY2(fanIs(0, true) && fanIs(1, true), fanState().constData());
    }
    void applyConfigOnlyAListedOne()
    {
        std::unique_ptr<QProcess> p;
        startAndWait(p);
        QTRY_VERIFY_WITH_TIMEOUT(prop(u"Configs"_s).toStringList().contains(u"TestB"_s), 8000);
        for (const QString &bad : { u"/tmp/some.json"_s, u"-r"_s, u"testb"_s }) {
            const QDBusMessage r = call(u"ApplyConfig"_s, { bad });
            QCOMPARE(r.errorName(), u"solwind.Error.Usage"_s);
            QCOMPARE(r.errorMessage(), u"not a known config: "_s + bad);
        }
        QVERIFY(!w->calls().contains("pkexec"));
        QCOMPARE(call(u"ApplyConfig"_s, { u"TestB"_s }).type(), QDBusMessage::ReplyMessage);
        QTRY_COMPARE_WITH_TIMEOUT(prop(u"ConfigId"_s).toString(), u"TestB"_s, 10000);
        QDBusInterface(service, u"/solwind"_s, service).call(u"Quit"_s);
        expectExit(*p, 0);
    }
    void clientValidation()
    {
        std::unique_ptr<QProcess> p;
        startAndWait(p);
        QTRY_VERIFY_WITH_TIMEOUT(prop(u"Restored"_s).toBool(), 8000);
        auto pt = [](const QVariant &t, const QVariant &s) { return QVariantMap{ { u"t"_s, t }, { u"s"_s, s } }; };
        auto curve = [](const VariantMapList &l) { return QVariantMap{ { u"mode"_s, 2 }, { u"curve"_s, QVariant::fromValue(l) } }; };
        VariantMapList thirteen;
        for (int i = 0; i < 13; ++i) thirteen << pt(30 + 5 * i, 50);
        QVariantMap extra = pt(60, 50);
        extra.insert(u"x"_s, 1);
        const QString mode = u"mode must be 0 (auto), 1 (fixed) or 2 (curve)"_s, fixed = u"fixed must be a number from 0 to 1"_s;
        const QString src = u"src must be cpu, gpu or max"_s, hyst = u"hyst must be a whole °C from 0 to 10"_s;
        const QList<std::pair<QVariantMap, QString>> cases{
            { { { u"mode"_s, 3 } }, mode }, { { { u"mode"_s, 1.5 } }, mode }, { { { u"mode"_s, u"1"_s } }, mode },
            { { { u"mode"_s, true } }, mode },
            { { { u"mode"_s, 1 }, { u"fixed"_s, 1.5 } }, fixed }, { { { u"mode"_s, 1 }, { u"fixed"_s, std::nan("") } }, fixed },
            { { { u"mode"_s, 1 }, { u"fixed"_s, u"0.5"_s } }, fixed },
            { curve({ pt(50, 10) }), u"a curve has 2 to 12 points"_s }, { curve(thirteen), u"a curve has 2 to 12 points"_s },
            { { { u"mode"_s, 2 }, { u"curve"_s, u"x"_s } }, u"curve must be a list of {t, s}"_s },
            { curve({ pt(29, 10), pt(50, 20) }), u"a curve point's t is a whole °C from 30 to 100"_s },
            { curve({ pt(40.5, 10), pt(50, 20) }), u"a curve point's t is a whole °C from 30 to 100"_s },
            { curve({ pt(40, 10), pt(50, 101) }), u"a curve point's s is a whole % from 0 to 100"_s },
            { curve({ pt(40, 10), extra }), u"a curve point is {t, s}"_s },
            { { { u"smooth"_s, 1 } }, u"smooth must be true or false"_s },
            { { { u"src"_s, u"both"_s } }, src }, { { { u"src"_s, 5 } }, src },
            { { { u"hyst"_s, 11 } }, hyst }, { { { u"hyst"_s, -1 } }, hyst }, { { { u"hyst"_s, 2.5 } }, hyst },
        };
        for (const auto &[stMap, why] : cases) {
            const QDBusMessage r = call(u"SetFan"_s, { 0, stMap });
            QCOMPARE(r.errorName(), u"solwind.Error.Usage"_s);
            QCOMPARE(r.errorMessage(), why);
        }
        QVERIFY(!w->calls().contains("nbfc set"));              // none of them reached the fans
        QDBusInterface(service, u"/solwind"_s, service).call(u"Quit"_s);
        expectExit(*p, 0);
    }
    void stepsAsideWhenTheQmlAppStarts_data()
    {
        QTest::addColumn<QString>("entry");
        QTest::newRow("new by-pid link") << u"by-pid/23456"_s;
        QTest::newRow("pid of a stale link: only by-id") << u"by-id/x7k2qochnt/log.qslog"_s;
    }
    void stepsAsideWhenTheQmlAppStarts()
    {
        QFETCH(QString, entry);
        settings("{\"configs\":{\"TestA\":{\"fans\":{\"0\":{\"mode\":1,\"fixed\":0.4}}}}}");
        std::unique_ptr<QProcess> p;
        startAndWait(p);
        QTRY_VERIFY_WITH_TIMEOUT(fanIs(0, false, 40), 8000);
        // The QML app starts: Quickshell makes its entries, and qs list shows it
        w->write(u"qs-list.json"_s, "[{\"config_path\": \"/home/u/dev/Ccenter/shell.qml\", \"pid\": 2}]");
        FakeWorld::writeAt(runtime + u"/quickshell/"_s + entry, "");
        expectExit(*p, 3);
        QVERIFY(fanIs(0, true));                                // handed back to it
    }
    void notificationsFromTheDaemon()
    {
        settings("{\"configs\":{\"TestA\":{\"fans\":{\"0\":{\"mode\":1,\"fixed\":0.4}}}}}");
        std::unique_ptr<QProcess> p;
        startAndWait(p);
        QTRY_VERIFY_WITH_TIMEOUT(prop(u"Restored"_s).toBool(), 8000);
        call(u"ApplyProfile"_s, { u"balanced"_s, true });       // as from a terminal (NT-7)
        QTRY_COMPARE(notes.sent.size(), 1);
        QCOMPARE(notes.sent[0].app, u"Solwind"_s);
        QCOMPARE(notes.sent[0].summary, u"Fan profile: Balanced"_s);
        QCOMPARE(notes.sent[0].urgency, 0);
        QCOMPARE(notes.sent[0].icon, u"dialog-information"_s);
        // Critical temperature (NT-2): the fake's temperature goes over the limit
        w->state(true, false, u"TestA"_s, 93, { { u"CPU Fan"_s }, { u"GPU Fan"_s } });
        QTRY_VERIFY_WITH_TIMEOUT(notes.sent.size() >= 2, 8000);
        QCOMPARE(notes.sent[1].summary, u"Critical temperature: 93°C (CPU)"_s);
        QCOMPARE(notes.sent[1].body, u"Safety limit (90°C) exceeded; fans driven by Solwind were set to 100%."_s);
        QCOMPARE(notes.sent[1].urgency, 2);
        QTRY_VERIFY_WITH_TIMEOUT(fanIs(0, false, 100) && fanIs(1, false, 100), 6000);   // (Global curve: both)
        // NBFC goes away (NT-3)
        QCOMPARE(w->run(u"nbfc"_s, { u"stop"_s }), 0);
        QTRY_VERIFY_WITH_TIMEOUT(notes.sent.size() >= 3, 8000);
        QCOMPARE(notes.sent[2].summary, u"NBFC service stopped"_s);
        // Quitting with fixed speeds saved by NBFC (QT-3): said even with notifications off
        QDBusInterface api(service, u"/solwind"_s, service);
        QCOMPARE(api.call(u"SetNotify"_s, u"enabled"_s, false).type(), QDBusMessage::ReplyMessage);
        api.call(u"Quit"_s);
        expectExit(*p, 0);
        QTRY_COMPARE(notes.sent.size(), 4);
        QCOMPARE(notes.sent[3].summary, u"Fans may stay at a fixed speed"_s);
    }
};

QTEST_GUILESS_MAIN(TstControlD)
#include "tst_controld.moc"
