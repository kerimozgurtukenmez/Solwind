// NbfcPlugin against the fake nbfc / pkexec / systemctl: SPEC.md Q-1, Q-2 (one queue, keys), A-1..A-9 (admin actions,
// hand-back first, holding, messages, quiet), L-2, L-3 (lost service), QT-3 (saved fixed speeds). Each test has its
// own fake world; nothing real is run.
#include "TestEnv.h"
#include "NbfcPlugin.h"
#include "TestHost.h"

#include <QRegularExpression>
#include <QSignalSpy>
#include <QStandardPaths>

#include <memory>

using namespace Qt::StringLiterals;

class TstNbfcPlugin : public QObject
{
    Q_OBJECT

    std::unique_ptr<FakeWorld> w;
    std::unique_ptr<TestHost> host;
    std::unique_ptr<NbfcPlugin> p;

    Paths paths() const
    {
        Paths x;
        x.sysRoot = w->sysRoot();
        x.nbfcRoot = w->nbfcRoot();
        return x;
    }
    void startPlugin()
    {
        host->p = paths();
        p = std::make_unique<NbfcPlugin>(*host);
        p->start();
        QTRY_VERIFY(p->statusSeq() >= 1);
    }
    void read()
    {
        const quint64 seq = p->statusSeq();
        QTRY_VERIFY(p->statusSeq() == p->pollSeq());
        p->monitor().pollNow();
        QTRY_VERIFY(p->statusSeq() > seq);
    }
    // The nbfc / pkexec / systemctl calls, without the status reads, in order
    QStringList calls() const
    {
        QStringList out;
        for (const QByteArray &l : w->calls().split('\n')) {
            const QString s = QString::fromUtf8(l).section(u' ', 1);
            if (s.isEmpty() || s.startsWith(u"nbfc status"_s) || s.startsWith(u"nbfc config -l"_s)
                || s.startsWith(u"systemctl is-enabled"_s) || s.startsWith(u"qs "_s))
                continue;
            out << s;
        }
        return out;
    }

private slots:
    void init()
    {
        p.reset();
        w = std::make_unique<FakeWorld>();
        w->applyToThisProcess();
        QVERIFY(QStandardPaths::findExecutable(u"nbfc"_s).startsWith(QStringLiteral(FAKE_DIR)));
        QVERIFY(QStandardPaths::findExecutable(u"pkexec"_s).startsWith(QStringLiteral(FAKE_DIR)));
        host = std::make_unique<TestHost>();
    }
    void cleanup() { p.reset(); }

    void basics()
    {
        startPlugin();
        QCOMPARE(p->id(), u"nbfc"_s);
        QVERIFY(p->trust() == FanPlugin::Trust::WriteTested);
        QCOMPARE(p->fans().size(), 2);
        QCOMPARE(p->fans()[0].name, u"CPU Fan"_s);
        QCOMPARE(p->blockedReason(), QString());
    }
    void speedsAndKeys()
    {
        startPlugin();
        int answered = 0;
        bool ok0 = false;
        p->setPercent(0, 40, [&](bool ok, const QString &) { ok0 = ok; ++answered; });
        p->setPercent(1, 50, [&](bool, const QString &) { ++answered; });
        p->setPercent(1, 55, [&](bool, const QString &) { ++answered; });   // replaces the waiting 50
        p->setAuto(0, [&](bool, const QString &) { ++answered; });
        QTRY_COMPARE(answered, 3);
        QVERIFY(ok0);
        QTRY_VERIFY(calls().size() == 3);
        QCOMPARE(calls(), (QStringList{ u"nbfc set -f 0 -s 40 ok"_s, u"nbfc set -f 1 -s 55 ok"_s, u"nbfc set -f 0 -a ok"_s }));
        read();
        QVERIFY(p->fans()[0].autoControl);
        QCOMPARE(p->fans()[1].target, 55.0);
    }
    void failedCommandSaysWhy()
    {
        w->state(false, false, u"TestA"_s, 50, { { u"CPU Fan"_s } });
        startPlugin();
        QString err;
        bool done = false;
        p->setPercent(0, 40, [&](bool ok, const QString &e) { QVERIFY(!ok); err = e; done = true; });
        QTRY_VERIFY(done);
        QCOMPARE(err, u"Error: not running"_s);
        QCOMPARE(p->blockedReason(), u"The NBFC service is not running"_s);
    }
    void stopHandsBackFirst()
    {
        startPlugin();
        QSignalSpy lost(p.get(), &FanPlugin::serviceLost);
        w->write(u"pkexec-wait"_s, "");                        // the password dialog stays open for now
        p->setPercent(0, 40, nullptr);
        p->stopService();
        QVERIFY(p->holding());                                   // nothing written from now on
        QCOMPARE(p->message(), QString());                       // the action starts after the hand-back (as Nbfc.qml)
        QTRY_VERIFY(p->adminBusy() && w->calls().contains("pkexec nbfc stop"));
        QCOMPARE(p->message(), u"Waiting for admin permission…"_s);
        // The speed that was waiting, then the hand-back, then (and only then) the stop
        QCOMPARE(calls().mid(0, 3), (QStringList{ u"nbfc set -f 0 -s 40 ok"_s, u"nbfc set -a ok"_s, u"pkexec nbfc stop"_s }));
        QVERIFY(p->quiet());
        QFile::remove(w->cct() + u"/pkexec-wait"_s);
        QTRY_VERIFY(!p->holding());
        QCOMPARE(p->message(), QString());
        QTRY_VERIFY(!p->running());                            // (it polls right after the action)
        read();
        QCOMPARE(lost.count(), 0);                             // stopped by the user: not "lost"
    }
    void lostReportedOnlyWhenNothingExplainsIt()
    {
        startPlugin();
        QSignalSpy lost(p.get(), &FanPlugin::serviceLost);
        w->write(u"pkexec-wait"_s, "");
        p->setBoot(true);                                        // an admin action is running...
        QTRY_VERIFY(w->calls().contains("pkexec systemctl enable"));
        w->run(u"nbfc"_s, { u"stop"_s });            // ... and NBFC goes away meanwhile
        read();
        read();
        QCOMPARE(lost.count(), 0);                             // an action could explain it: not yet
        QFile::remove(w->cct() + u"/pkexec-wait"_s);
        QTRY_VERIFY(!p->adminBusy());
        read();
        QCOMPARE(lost.count(), 0);                             // and for 5 s after it
        p->now = [] { return QDateTime::currentMSecsSinceEpoch() + 10000; };   // later than that
        read();
        QCOMPARE(lost.count(), 1);                             // NBFC didn't come back: now it's reported
        read();
        QCOMPARE(lost.count(), 1);
    }
    void quittingHoldsLost()
    {
        startPlugin();
        QSignalSpy lost(p.get(), &FanPlugin::serviceLost);
        p->setQuitting(true);
        w->run(u"nbfc"_s, { u"stop"_s });
        read();
        QCOMPARE(lost.count(), 0);
    }
    void cancelledSaysSo()
    {
        startPlugin();
        w->write(u"pkexec-cancel"_s, "");
        p->setBoot(true);
        QTRY_VERIFY(p->messageError());
        QCOMPARE(p->message(), u"Failed: Error executing command as another user: Not authorized (code 126)"_s);
        QVERIFY(!p->monitor().bootEnabled());
        QFile::remove(w->cct() + u"/pkexec-cancel"_s);
        p->setBoot(true);
        QTRY_VERIFY(p->monitor().bootEnabled());               // refreshed after the action (A-8)
        QVERIFY(!p->messageError());
    }
    void readOnlyRestarts()
    {
        startPlugin();
        p->setReadOnly(true);
        QTRY_VERIFY(w->calls().contains("pkexec nbfc restart -r"));
        QTRY_VERIFY(p->readOnly());
        QCOMPARE(p->blockedReason(), u"NBFC is in read-only mode, fan speeds can't be written"_s);
        w->state(false, false, u"TestA"_s, 50, { { u"CPU Fan"_s } });
        read();
        read();
        p->setReadOnly(false);                                   // not running: only remembered for the next start
        QVERIFY(!p->readOnly());
        p->startService();
        QTRY_VERIFY(w->calls().contains("pkexec nbfc start ok") || w->calls().contains("pkexec nbfc start\n"));
        QVERIFY(!calls().contains(u"pkexec nbfc start -r"_s));
    }
    void applyConfigHandsBackThenSwitches()
    {
        startPlugin();
        p->applyConfig(u"Test B; rm -rf x"_s);                   // the name is an argument, never shell text
        QTRY_VERIFY(!p->holding());
        const QStringList c = calls();
        QCOMPARE(c.value(0), u"nbfc set -a ok"_s);
        QCOMPARE(c.value(1), u"pkexec sh -c nbfc config -s \"$1\" && nbfc restart $2 sh Test B; rm -rf x "_s);
        QFile f(w->cct() + u"/nbfc.json"_s);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(f.readAll().contains("Test B; rm -rf x"));
    }
    void startWhenNotRunningDoesntHandBack()
    {
        w->state(false, false, u"TestA"_s, 50, { { u"CPU Fan"_s } });
        startPlugin();
        p->stopService();                                        // not running: straight to the action
        QTRY_VERIFY(!p->holding());
        QCOMPARE(calls().value(0), u"pkexec nbfc stop"_s);
    }
    void releaseAllIsNeverReplaced()
    {
        startPlugin();
        int done = 0;
        p->releaseAll([&](bool, const QString &) { ++done; });
        p->releaseAll([&](bool, const QString &) { ++done; });
        QTRY_COMPARE(done, 2);
    }
    void savedFixedSpeeds()
    {
        startPlugin();
        auto ask = [&] {
            bool r = false, got = false;
            p->willRestoreFixedSpeeds([&](bool v) { r = v; got = true; });
            [&] { QTRY_VERIFY(got); }();
            return r;
        };
        w->write(u"saved.json"_s, "{\"TargetFanSpeeds\": [-1, -1]}");
        QVERIFY(!ask());
        w->write(u"saved.json"_s, "{\"TargetFanSpeeds\": [-1, 40]}");
        QVERIFY(ask());
        w->write(u"saved.json"_s, "{\"TargetFanSpeeds\": [\"0\"]}");    // Number("0") >= 0
        QVERIFY(ask());
        w->write(u"saved.json"_s, "{\"TargetFanSpeeds\": \"x\"}");
        QVERIFY(!ask());
        w->write(u"saved.json"_s, "not json");
        QVERIFY(!ask());
        QFile::remove(w->cct() + u"/saved.json"_s);
        QVERIFY(!ask());
    }
    void recommendMessage()
    {
        startPlugin();
        bool done = false;
        p->recommend([&](bool ok, const QStringList &f) { QVERIFY(ok); QVERIFY(f.isEmpty()); done = true; });
        QTRY_VERIFY(done);
        QCOMPARE(p->message(), u"No recommendation found"_s);
        QVERIFY(!p->messageError());
    }
};

QTEST_GUILESS_MAIN(TstNbfcPlugin)
#include "tst_nbfcplugin.moc"
