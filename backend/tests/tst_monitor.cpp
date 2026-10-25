// NbfcMonitor against the fake nbfc (tests/fake/nbfc): SPEC.md N-9..N-14 (which config the fans belong to - the
// scenarios of the QML app's .dev/configtest.sh), A-8, A-10..A-12, L-1..L-3, P-2's pace. Each test has its own world.
#include "TestEnv.h"
#include "plugins/fan/nbfc/NbfcMonitor.h"

#include <QSignalSpy>
#include <QStandardPaths>

#include <memory>

using namespace Qt::StringLiterals;

namespace {
const QByteArray configA = "{\"FanConfigurations\": [{\"TemperatureThresholds\": [{\"UpThreshold\": 40, \"FanSpeed\": 0},"
                           " {\"UpThreshold\": 60, \"DownThreshold\": 54, \"FanSpeed\": 50}]}]}";
}

class TstMonitor : public QObject
{
    Q_OBJECT

    std::unique_ptr<FakeWorld> w;
    std::unique_ptr<NbfcMonitor> m;

    Paths paths() const
    {
        Paths p;
        p.sysRoot = w->sysRoot();
        p.nbfcRoot = w->nbfcRoot();
        return p;
    }
    void startMonitor()
    {
        m = std::make_unique<NbfcMonitor>(paths());
        m->start();
        QTRY_VERIFY(m->statusSeq() >= 1);
    }
    void pollAndWait()
    {
        const quint64 seq = m->statusSeq();
        QTRY_VERIFY(!m->pollSeq() || m->statusSeq() == m->pollSeq());   // let a running read finish
        m->pollNow();
        QTRY_VERIFY(m->statusSeq() > seq);
    }
    void nbfc(const QStringList &args)
    {
        QVERIFY(w->run(u"nbfc"_s, args) >= 0);
    }

private slots:
    void init()
    {
        m.reset();
        w = std::make_unique<FakeWorld>();
        w->applyToThisProcess();
        // Never the real nbfc / systemctl: the fakes must be the ones found
        QVERIFY(QStandardPaths::findExecutable(u"nbfc"_s).startsWith(QStringLiteral(FAKE_DIR)));
        QVERIFY(QStandardPaths::findExecutable(u"systemctl"_s).startsWith(QStringLiteral(FAKE_DIR)));
        w->write(u"boot"_s, "enabled");
        const QString share = w->nbfcRoot() + u"/usr/share/nbfc/configs/"_s;
        FakeWorld::writeAt(share + u"TestA.json"_s, configA);
        FakeWorld::writeAt(share + u"TestB.json"_s, "{}");
        FakeWorld::writeAt(share + u".Hidden.json"_s, "{}");
        FakeWorld::writeAt(share + u"notes.txt"_s, "");
        FakeWorld::writeAt(share + u"Upper.JSON"_s, "{}");                 // -name is case-sensitive
    }
    void cleanup() { m.reset(); }

    void status()
    {
        w->state(true, true, u"TestA"_s, 55.5, { { u"CPU Fan"_s, false, 44 }, { u"GPU Fan"_s, true, 30, true } });
        startMonitor();
        QVERIFY(m->running());
        QVERIFY(m->readOnly());
        QCOMPARE(m->temp(), 55.5);
        QCOMPARE(m->fans().size(), 2);
        QCOMPARE(m->fans()[0].target, 44.0);
        QVERIFY(!m->fans()[0].autoControl);
        QCOMPARE(m->fans()[1].current, 100.0);              // critical mode: the fake reports 100
        QVERIFY(m->fans()[1].critical);
        QCOMPARE(m->configId(), u"TestA"_s);
        QTRY_VERIFY(m->bootEnabled());
    }

    void configList()
    {
        startMonitor();
        QTRY_VERIFY(m->cliListCode() == 0);
        QVERIFY(m->configs().contains(u".Hidden"_s));        // find -name matches dot files
        QVERIFY(!m->configs().contains(u"Upper"_s) && !m->configs().contains(u"notes"_s));
        QVERIFY(m->configs().contains(u"TestA"_s) && m->configs().contains(u"TestB"_s));
        QCOMPARE(m->configs().size(), 3);                    // the folders win over `nbfc config -l` (TestA TestB)
        QCOMPARE(m->configDirsUsed(), QStringList{ w->nbfcRoot() + u"/usr/share/nbfc/configs"_s });
        for (qsizetype i = 1; i < m->configs().size(); ++i)
            QVERIFY(QString::localeAwareCompare(m->configs()[i - 1], m->configs()[i]) <= 0);
    }
    void configListFromCli()
    {
        QDir(w->nbfcRoot() + u"/usr/share/nbfc"_s).removeRecursively();
        w->write(u"configs.txt"_s, "Usage: nbfc config\nZeta\nAlpha\n");
        startMonitor();
        QTRY_COMPARE(m->configs(), (QStringList{ u"Alpha"_s, u"TestA"_s, u"Zeta"_s }));   // + the selected one
        QVERIFY(m->configDirsUsed().isEmpty());
    }

    void curvesLookup()
    {
        startMonitor();
        QTRY_COMPARE(m->configCurves().size(), 1);
        QVERIFY(m->configCurves()[0]);
        QCOMPARE(m->configCurves()[0]->hyst, 6);
        QCOMPARE(m->configCurves()[0]->points.size(), 2);
    }
    void curvesLookupOrder()
    {
        // /var/lib/nbfc/configs comes before /usr/share; the id as a path from "/" before both
        FakeWorld::writeAt(w->nbfcRoot() + u"/var/lib/nbfc/configs/TestA"_s,   // without .json, in the first folder
                           "{\"FanConfigurations\": [{}, {}]}");
        startMonitor();
        QTRY_COMPARE(m->configCurves().size(), 2);
        FakeWorld::writeAt(w->nbfcRoot() + u"/TestA"_s, "{\"FanConfigurations\": [{}, {}, {}]}");
        w->state(true, false, u"TestB"_s, 50, { { u"Fan"_s } });     // switch away and back to reload
        pollAndWait();
        w->state(true, false, u"TestA"_s, 50, { { u"Fan"_s } });
        pollAndWait();
        QTRY_COMPARE(m->configCurves().size(), 3);
    }

    // ---------- which config (N-13): the scenarios of the QML app's configtest.sh ----------
    void configSelectedWithoutRestart()
    {
        startMonitor();
        QCOMPARE(m->configId(), u"TestA"_s);
        QSignalSpy changedId(m.get(), &NbfcMonitor::configIdChanged);
        nbfc({ u"config"_s, u"-s"_s, u"TestB"_s });                  // only the file changes
        QTRY_COMPARE(m->fileConfigId(), u"TestB"_s);
        QCOMPARE(m->configId(), u"TestA"_s);                         // still the running one
        QVERIFY(m->configPending());
        pollAndWait();
        QCOMPARE(m->configId(), u"TestA"_s);
        QCOMPARE(changedId.count(), 0);
        nbfc({ u"restart"_s });                                     // now NBFC runs TestB
        pollAndWait();
        QCOMPARE(m->configId(), u"TestB"_s);
        QVERIFY(!m->configPending());
    }
    void stoppedFileIsTheOne()
    {
        startMonitor();
        nbfc({ u"config"_s, u"-s"_s, u"TestB"_s });
        QTRY_COMPARE(m->fileConfigId(), u"TestB"_s);
        nbfc({ u"stop"_s });
        pollAndWait();                                               // one failed read: could be a timeout
        QVERIFY(!m->running());
        QCOMPARE(m->configId(), u"TestA"_s);
        QVERIFY(std::isnan(m->temp()));
        pollAndWait();                                               // two: really stopped
        QCOMPARE(m->configId(), u"TestB"_s);
    }
    void halfWrittenFileKeepsId()
    {
        startMonitor();
        w->write(u"nbfc.json"_s, "");                                 // NBFC rewriting it
        QTest::qWait(300);
        QCOMPARE(m->fileConfigId(), u"TestA"_s);
        w->write(u"nbfc.json"_s, "{\"SelectedConfigId\": \"TestB\"}");
        QTRY_COMPARE(m->fileConfigId(), u"TestB"_s);
        w->write(u"nbfc.json"_s, "{\"Other\": 1}");                   // no SelectedConfigId: none
        QTRY_COMPARE(m->fileConfigId(), QString());
    }
    void fileReplacedByRename()
    {
        startMonitor();
        // Replaced, not rewritten: the watch must follow the new file (folder watch)
        const QString f = w->nbfcRoot() + u"/etc/nbfc/nbfc.json"_s;
        QFile::remove(f);
        FakeWorld::writeAt(f + u".tmp"_s, "{\"SelectedConfigId\": \"TestB\"}");
        QVERIFY(QFile::rename(f + u".tmp"_s, f));
        QTRY_COMPARE(m->fileConfigId(), u"TestB"_s);
        FakeWorld::writeAt(f, "{\"SelectedConfigId\": \"TestC\"}");
        QTRY_COMPARE(m->fileConfigId(), u"TestC"_s);
    }
    void fileDeletedAndCreatedAgain()
    {
        // As Quickshell's FileView did (measured): a deleted file keeps the id, a new file and later edits are read
        startMonitor();
        const QString f = w->nbfcRoot() + u"/etc/nbfc/nbfc.json"_s;
        QFile::remove(f);
        QTest::qWait(300);
        QCOMPARE(m->fileConfigId(), u"TestA"_s);
        FakeWorld::writeAt(f, "{\"SelectedConfigId\": \"TestE\"}");
        QTRY_COMPARE(m->fileConfigId(), u"TestE"_s);
        FakeWorld::writeAt(f, "{\"SelectedConfigId\": \"TestF\"}");
        QTRY_COMPARE(m->fileConfigId(), u"TestF"_s);
    }
    void noConfigYet()
    {
        QFile::remove(w->nbfcRoot() + u"/etc/nbfc/nbfc.json"_s);
        w->state(false, false, u""_s, 50, {});
        startMonitor();
        QVERIFY(m->configRead());
        QVERIFY(m->noConfig());
        QVERIFY(!m->running());
    }

    // ---------- lost service (L-1..L-3) ----------
    void serviceLost()
    {
        startMonitor();
        QSignalSpy lost(m.get(), &NbfcMonitor::serviceLost);
        nbfc({ u"stop"_s });
        pollAndWait();
        QCOMPARE(lost.count(), 1);
        pollAndWait();
        QCOMPARE(lost.count(), 1);                                    // once
        nbfc({ u"start"_s });
        pollAndWait();
        QVERIFY(m->running());
    }
    void notRunningAtStartIsNotLost()
    {
        w->state(false, false, u"TestA"_s, 50, {});
        m = std::make_unique<NbfcMonitor>(paths());
        QSignalSpy lost(m.get(), &NbfcMonitor::serviceLost);
        m->start();
        QTRY_VERIFY(m->statusSeq() >= 1);
        QCOMPARE(lost.count(), 0);
    }

    // ---------- recommendations, pace ----------
    void recommend()
    {
        w->write(u"recommend.txt"_s, "TestB   (score 90)\nTestA\nNot a config\n");
        startMonitor();
        QTRY_VERIFY(m->configs().contains(u"TestB"_s));
        int answers = 0;
        QStringList got1, got2;
        m->recommend([&](bool ok, const QStringList &f) { QVERIFY(ok); got1 = f; ++answers; });
        m->recommend([&](bool ok, const QStringList &f) { QVERIFY(ok); got2 = f; ++answers; });   // while one waits
        QTRY_COMPARE(answers, 2);
        QCOMPARE(got1, (QStringList{ u"TestB"_s, u"TestA"_s }));
        QCOMPARE(got2, got1);
        QCOMPARE(m->recommended(), got1);
    }
    void pace()
    {
        m = std::make_unique<NbfcMonitor>(paths());
        QCOMPARE(m->interval(), 5000);
        m->setFast(true);
        QCOMPARE(m->interval(), 2000);
        m->setFast(false);
        QCOMPARE(m->interval(), 5000);
    }
    void readsOnly()
    {
        // Phase 2: whatever happens, the monitor never runs anything that changes NBFC
        startMonitor();
        pollAndWait();
        m->recommend(nullptr);
        QTest::qWait(500);
        const QByteArray c = w->calls();
        QVERIFY2(!c.contains("nbfc set") && !c.contains("nbfc start") && !c.contains("nbfc stop")
                 && !c.contains("nbfc restart") && !c.contains("config -s"), c.constData());
    }
};

QTEST_GUILESS_MAIN(TstMonitor)
#include "tst_monitor.moc"
