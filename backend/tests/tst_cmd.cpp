// Cmd and CommandQueue: SPEC.md X-1..X-5 (the callback exactly once, 127, 124 with SIGTERM then SIGKILL) and
// Q-1, Q-2 (one at a time, a waiting command replaced by a newer one with the same key, "" never replaced).
#include "core/Cmd.h"
#include "core/CommandQueue.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;

class TstCmd : public QObject
{
    Q_OBJECT
private slots:
    void output()
    {
        Cmd c;
        int calls = 0, code = -1;
        QByteArray out, err;
        c.go({ u"sh"_s, u"-c"_s, u"echo out; echo err >&2; exit 3"_s }, [&](int k, const QByteArray &o, const QByteArray &e) {
            ++calls; code = k; out = o; err = e;
        });
        QVERIFY(c.busy());
        QTRY_COMPARE(calls, 1);
        QCOMPARE(code, 3);
        QCOMPARE(out, QByteArray("out\n"));
        QCOMPARE(err, QByteArray("err\n"));
        QVERIFY(!c.busy());
        QTest::qWait(100);
        QCOMPARE(calls, 1);                                   // exactly once
    }
    void merged()
    {
        Cmd c;
        c.setMergedChannels(true);
        QByteArray out;
        bool done = false;
        c.go({ u"sh"_s, u"-c"_s, u"echo a; echo b >&2"_s }, [&](int, const QByteArray &o, const QByteArray &) { out = o; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(out.contains("a\n") && out.contains("b\n"));
    }
    void stdinIsEmpty()
    {
        Cmd c;
        int code = -1;
        c.go({ u"cat"_s }, [&](int k, const QByteArray &, const QByteArray &) { code = k; });   // would wait forever on a pipe
        QTRY_COMPARE_WITH_TIMEOUT(code, 0, 3000);
    }
    void cannotStart()
    {
        Cmd c;
        int calls = 0, code = -1;
        QByteArray err;
        c.go({ u"/nonexistent/solwind-test-program"_s }, [&](int k, const QByteArray &, const QByteArray &e) { ++calls; code = k; err = e; });
        QTRY_COMPARE(calls, 1);
        QCOMPARE(code, 127);
        QCOMPARE(err, QByteArray("failed to start: /nonexistent/solwind-test-program"));
        QVERIFY(!c.busy());
        // usable again afterwards
        bool ok = false;
        c.go({ u"true"_s }, [&](int k, const QByteArray &, const QByteArray &) { ok = k == 0; });
        QTRY_VERIFY(ok);
    }
    void timeoutTerm()
    {
        Cmd c;
        c.setTimeout(200);
        int code = -1;
        QElapsedTimer t;
        t.start();
        c.go({ u"sleep"_s, u"30"_s }, [&](int k, const QByteArray &, const QByteArray &) { code = k; });
        QTRY_COMPARE_WITH_TIMEOUT(code, 124, 3000);
        QVERIFY(t.elapsed() < 2000);                          // SIGTERM was enough
    }
    void timeoutKill()
    {
        // A command that ignores SIGTERM: SIGKILL after the kill delay
        Cmd c;
        c.setTimeout(200);
        c.setKillDelay(300);
        int code = -1;
        QElapsedTimer t;
        t.start();
        c.go({ u"sh"_s, u"-c"_s, u"trap '' TERM; while :; do sleep 0.05; done"_s }, [&](int k, const QByteArray &, const QByteArray &) { code = k; });
        QTRY_COMPARE_WITH_TIMEOUT(code, 124, 5000);
        QVERIFY(t.elapsed() >= 450);
    }

    void crashIsNotSuccess()
    {
        Cmd c;
        int code = -1;
        c.go({ u"sh"_s, u"-c"_s, u"kill -SEGV $$"_s }, [&](int k, const QByteArray &, const QByteArray &) { code = k; });
        QTRY_VERIFY(code != -1);
        QVERIFY(code != 0);
    }
    void busyCallGetsAnAnswer()
    {
        Cmd c;
        int first = -1, second = -1;
        c.go({ u"sh"_s, u"-c"_s, u"sleep 0.2"_s }, [&](int k, const QByteArray &, const QByteArray &) { first = k; });
        c.go({ u"true"_s }, [&](int k, const QByteArray &, const QByteArray &) { second = k; });
        QCOMPARE(second, 125);                                // answered at once
        QTRY_COMPARE(first, 0);                               // the running one isn't lost
    }

    void queueOrderAndKeys()
    {
        CommandQueue q(5000);
        QStringList ran;
        auto rec = [&](const QString &name) { return [&ran, name](int, const QByteArray &, const QByteArray &) { ran << name; }; };
        q.enqueue(u"spd:0"_s, { u"sh"_s, u"-c"_s, u"sleep 0.3"_s }, rec(u"first"_s));   // runs at once
        q.enqueue(u"spd:0"_s, { u"true"_s }, rec(u"old speed"_s));                     // waits ...
        q.enqueue(u""_s, { u"true"_s }, rec(u"hand back"_s));
        q.enqueue(u""_s, { u"true"_s }, rec(u"hand back 2"_s));                         // "" never replaced
        q.enqueue(u"spd:0"_s, { u"true"_s }, rec(u"new speed"_s));                     // ... replaced, goes last
        QCOMPARE(q.waiting(), 3);
        QTRY_COMPARE_WITH_TIMEOUT(ran.size(), 4, 3000);
        QCOMPARE(ran, (QStringList{ u"first"_s, u"hand back"_s, u"hand back 2"_s, u"new speed"_s }));
    }
    void queueCallbackCanEnqueue()
    {
        CommandQueue q(5000);
        QStringList ran;
        q.enqueue(u"a"_s, { u"true"_s }, [&](int, const QByteArray &, const QByteArray &) {
            ran << u"a"_s;
            q.enqueue(u"b"_s, { u"true"_s }, [&](int, const QByteArray &, const QByteArray &) { ran << u"b"_s; });
        });
        QTRY_COMPARE(ran, (QStringList{ u"a"_s, u"b"_s }));
    }
    void queueTimeout()
    {
        CommandQueue q(200);
        int code = -1;
        q.enqueue(u"x"_s, { u"sleep"_s, u"30"_s }, [&](int k, const QByteArray &, const QByteArray &) { code = k; });
        bool next = false;
        q.enqueue(u"y"_s, { u"true"_s }, [&](int, const QByteArray &, const QByteArray &) { next = true; });
        QTRY_COMPARE_WITH_TIMEOUT(code, 124, 3000);
        QTRY_VERIFY(next);                                    // the queue goes on after a timeout
    }
};

QTEST_GUILESS_MAIN(TstCmd)
#include "tst_cmd.moc"
