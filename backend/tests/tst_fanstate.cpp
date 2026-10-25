// FanState: SPEC.md §5 F-1..F-8 (restore, changes) and PR-1..PR-10 (profiles), with the real Settings in a temporary
// folder, the real FanController and an in-process fake plugin. Notifications go to a bus that doesn't exist (the
// `sent` signal is checked), never to the desktop.
#include "FakePlugin.h"
#include "core/FanController.h"
#include "core/FanState.h"
#include "core/I18n.h"
#include "core/Notifier.h"
#include "core/Settings.h"

#include <QDBusConnection>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {
QDBusConnection noBus() { return QDBusConnection::connectToBus(u"unix:path=/nonexistent/solwind-test"_s, u"none"_s); }

struct Rig {
    QTemporaryDir dir;
    FakePlugin p;
    I18n i18n;
    Settings settings{ dir.path() };
    Notifier notifier{ noBus() };
    FanController c{ p, i18n };
    FanState s{ p, c, settings, i18n, notifier };
    explicit Rig(const QByteArray &json = QByteArray(), int fans = 2) : p(fans)
    {
        p.autoFinish = true;
        i18n.savedLanguage = [this] { return settings.uiLang(); };
        notifier.enabled = [this] { return settings.notifyOn(u"enabled"_s); };
        if (!json.isEmpty()) {
            QFile f(settings.path());
            if (f.open(QIODevice::WriteOnly)) f.write(json);
            f.close();
            settings.load();
        }
    }
};
FanSt fixedSt(double f)
{
    FanSt s = FanState::autoSt();
    s.mode = 1;
    s.fixed = f;
    return s;
}
} // namespace

class TstFanState : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("LANG", "en_US.UTF-8"); }

    // ---------- F-1..F-4 ----------
    void restoreSaved()
    {
        Rig r("{\"configs\":{\"TestA\":{\"fans\":{\"0\":{\"mode\":1,\"fixed\":0.4}}}}}");
        r.p.fanList[1].autoControl = false;                     // set from outside, no saved setting
        r.p.fanList[1].target = 63;
        r.s.restore();
        QVERIFY(r.s.restored());
        QCOMPARE(r.p.log, QStringList{ u"spd 0 40"_s });        // fan 1 gets no command (F-2)
        QCOMPARE(r.s.fanSt()[1].mode, 1);                      // only shown as it is
        QCOMPARE(r.s.fanSt()[1].fixed, 0.63);
        QCOMPARE(r.s.fanSt()[0].curve, FanState::defaultCurve());   // full(): the default curve filled in
        r.p.log.clear();
        r.s.restore();                                          // once, while the fan count stays
        QVERIFY(r.p.log.isEmpty());
        r.p.fanList.append({ u"Fan 3"_s, 50, true, false, 30, 30 });
        r.s.restore();                                          // a fan more: again
        QCOMPARE(r.s.fanSt().size(), 3);
    }
    void restoreGlobal()
    {
        Rig r("{\"configs\":{\"TestA\":{\"fans\":{\"0\":{\"mode\":1,\"fixed\":0.4}},\"global\":{\"on\":true,\"st\":{\"mode\":1,\"fixed\":0.7}}}}}");
        r.s.restore();
        QCOMPARE(r.p.log, (QStringList{ u"spd 0 70"_s, u"spd 1 70"_s }));   // global wins, on every fan
        QVERIFY(r.s.globalOn());
    }
    void restoreNoFans()
    {
        Rig r(QByteArray(), 0);
        r.s.restore();
        QVERIFY(!r.s.restored());
    }
    void restorePerConfig()
    {
        Rig r("{\"configs\":{\"TestB\":{\"fans\":{\"0\":{\"mode\":1,\"fixed\":0.4}}}}}");
        r.s.restore();                                          // TestA has nothing saved
        QVERIFY(r.p.log.isEmpty());
        r.p.config = u"TestB"_s;
        r.s.unrestore();
        r.s.restore();
        QCOMPARE(r.p.log, QStringList{ u"spd 0 40"_s });
    }
    void restoreKeepsAnOldGlobal()
    {
        // F-3 port note: a config without a saved global keeps the previous globalSt (only globalOn goes off)
        Rig r("{\"configs\":{\"TestA\":{\"global\":{\"on\":true,\"st\":{\"mode\":1,\"fixed\":0.7}}},\"TestB\":{}}}");
        r.s.restore();
        r.p.config = u"TestB"_s;
        r.s.unrestore();
        r.s.restore();
        QVERIFY(!r.s.globalOn());
        QCOMPARE(r.s.globalSt().fixed, 0.7);
    }

    // ---------- F-5..F-8 ----------
    void setFanSavesAndApplies()
    {
        Rig r("{\"configs\":{\"TestA\":{\"active\":\"quiet\"}}}");
        r.s.restore();
        r.s.setFan(1, fixedSt(0.55));
        QCOMPARE(r.p.log, QStringList{ u"spd 1 55"_s });
        QCOMPARE(r.settings.fanOf(u"TestA"_s, 1)->fixed, 0.55);
        QCOMPARE(r.settings.activeOf(u"TestA"_s), QString());  // a manual change: no profile any more
    }
    void setFanWhileGlobalOn()
    {
        Rig r("{\"configs\":{\"TestA\":{\"global\":{\"on\":true,\"st\":{\"mode\":1,\"fixed\":0.7}}}}}");
        r.s.restore();
        r.p.log.clear();
        r.s.setFan(0, fixedSt(0.2));                            // saved, but Global drives the fans
        QVERIFY(r.p.log.isEmpty());
        QCOMPARE(r.settings.fanOf(u"TestA"_s, 0)->fixed, 0.2);
    }
    void globalOnOff()
    {
        Rig r("{\"configs\":{\"TestA\":{\"fans\":{\"0\":{\"mode\":1,\"fixed\":0.4}}}}}");
        r.s.restore();
        r.p.log.clear();
        r.s.setGlobal(fixedSt(0.8));                            // off: only saved
        QVERIFY(r.p.log.isEmpty());
        r.s.setGlobalOn(true);
        QCOMPARE(r.p.log, (QStringList{ u"spd 0 80"_s, u"spd 1 80"_s }));
        r.p.log.clear();
        r.s.setGlobalOn(false);                                 // each fan its own again: fan 1 mirrored "auto"
        QCOMPARE(r.p.log, (QStringList{ u"spd 0 40"_s, u"auto 1"_s }));
        QCOMPARE(r.settings.globalOf(u"TestA"_s)->on, false);
    }

    // ---------- PR-1, PR-8 ----------
    void builtinQuiet()
    {
        Rig r;
        r.s.restore();
        const auto res = r.s.applyByName(u"quiet"_s, false);
        QVERIFY(res.ok);
        QCOMPARE(res.text, u"Applied: Quiet"_s);
        QVERIFY(r.s.globalOn());
        QCOMPARE(r.s.globalSt().src, u"max"_s);
        QCOMPARE(r.s.globalSt().hyst, 4);
        QCOMPARE(r.s.globalSt().curve.first(), (CurvePoint{ 45, 0 }));
        QCOMPARE(r.settings.activeOf(u"TestA"_s), u"quiet"_s);
        QCOMPARE(r.settings.fanOf(u"TestA"_s, 0)->mode, 0);     // the fans' own settings: auto
    }
    void builtinAuto()
    {
        Rig r("{\"configs\":{\"TestA\":{\"fans\":{\"0\":{\"mode\":1,\"fixed\":0.4}}}}}");
        r.s.restore();
        r.p.log.clear();
        QVERIFY(r.s.applyByName(u"NBFC Auto"_s, false).ok);     // the English label works too
        QVERIFY(!r.s.globalOn());
        QCOMPARE(r.p.log, (QStringList{ u"auto 0"_s, u"auto 1"_s }));
        QVERIFY(!r.c.controlling());
    }

    // ---------- PR-9 ----------
    void nameLookup()
    {
        Rig r("{\"configs\":{\"TestA\":{\"profiles\":[{\"name\":\"Gece\",\"fans\":{\"0\":{\"mode\":1,\"fixed\":0.3}}}, {\"name\":\"gece\",\"fans\":{\"0\":{\"mode\":1,\"fixed\":0.6}}}, {\"name\":\"quiet\",\"fans\":{\"0\":{\"mode\":1,\"fixed\":0.9}}}]}}}");
        r.s.restore();
        QVERIFY(r.s.applyByName(u" gece "_s, false).ok);        // the exact (trimmed) name first
        QCOMPARE(r.s.fanSt()[0].fixed, 0.6);
        QVERIFY(r.s.applyByName(u"GECE"_s, false).ok);          // then any case: the first one
        QCOMPARE(r.s.fanSt()[0].fixed, 0.3);
        QVERIFY(r.s.applyByName(u"quiet"_s, false).ok);         // a built-in's name: the built-in, not the user's
        QVERIFY(r.s.globalOn());
        QVERIFY(r.s.applyByName(u"Sessiz"_s, false).ok);        // the Turkish label too
        QCOMPARE(r.settings.activeOf(u"TestA"_s), u"quiet"_s);
        const auto none = r.s.applyByName(u"nope"_s, false);
        QVERIFY(!none.ok && none.usage);
        QVERIFY(none.text.startsWith(u"Profile not found: nope\n"_s));
        QVERIFY(none.text.contains(u"* Quiet  (quiet)"_s));     // the list, the active one marked
    }
    void refusedStates()
    {
        Rig r;
        auto res = r.s.applyByName(u"quiet"_s, false);          // not restored yet
        QVERIFY(!res.ok && !res.usage);
        QCOMPARE(res.text, u"Fans not read yet (is NBFC running?), try again"_s);
        r.s.restore();
        r.p.isRunning = false;
        QCOMPARE(r.s.applyByName(u"quiet"_s, false).text, u"The NBFC service is not running"_s);
        r.p.isRunning = true;
        r.p.isReadOnly = true;
        QCOMPARE(r.s.applyByName(u"quiet"_s, false).text, u"NBFC is in read-only mode, fan speeds can't be written"_s);
    }
    void notification()
    {
        Rig r;
        QSignalSpy sent(&r.notifier, &Notifier::sent);
        r.s.restore();
        QVERIFY(r.s.applyByName(u"quiet"_s, false).ok);         // from the UI: no notification
        QCOMPARE(sent.count(), 0);
        QVERIFY(r.s.applyByName(u"balanced"_s, true).ok);       // from a terminal: one (NT-7)
        QCOMPARE(sent.count(), 1);
        QCOMPARE(sent[0][0].toString(), u"Fan profile: Balanced"_s);
        QCOMPARE(sent[0][2].toInt(), int(Notifier::Low));
        r.settings.setNotify(u"profile"_s, false);
        QVERIFY(r.s.applyByName(u"quiet"_s, true).ok);
        QCOMPARE(sent.count(), 1);
        r.settings.setNotify(u"profile"_s, true);
        r.settings.setNotify(u"enabled"_s, false);
        QVERIFY(r.s.applyByName(u"quiet"_s, true).ok);
        QCOMPARE(sent.count(), 1);
    }
    void turkish()
    {
        Rig r("{\"ui\":{\"lang\":\"tr\"}}");
        r.s.restore();
        QCOMPARE(r.s.applyByName(u"quiet"_s, false).text, u"Uygulandı: Sessiz"_s);
    }

    // ---------- PR-3, PR-10 ----------
    void legacyActiveAndList()
    {
        Rig r("{\"configs\":{\"TestA\":{\"active\":\"Dengeli\",\"profiles\":[{\"name\":\"Mine\"}]}}}");
        QCOMPARE(r.s.activeId(), u"balanced"_s);
        QCOMPARE(r.s.activeName(), u"Balanced"_s);
        QCOMPARE(r.s.profileList(), u"  NBFC Auto  (auto)\n  Quiet  (quiet)\n* Balanced  (balanced)\n"
                                    "  Performance  (performance)\n  Mine"_s);
    }

    // ---------- saving, removing (UI-7, PR-5..PR-7) ----------
    void saveAndRemove()
    {
        Rig r("{\"configs\":{\"TestA\":{\"fans\":{\"0\":{\"mode\":1,\"fixed\":0.4}}}}}");
        QCOMPARE(r.s.saveProfile(u"Night"_s), u"Fans not read yet (is NBFC running?), try again"_s);
        r.s.restore();
        QCOMPARE(r.s.saveProfile(u"   "_s), u"A profile needs a name"_s);
        QCOMPARE(r.s.saveProfile(QString(33, u'x')), u"A profile name can have at most 32 characters"_s);
        QCOMPARE(r.s.saveProfile(u"Sessiz"_s), u"This name belongs to a built-in profile, pick another one."_s);
        QCOMPARE(r.s.saveProfile(u"  Night "_s), QString());
        const auto saved = r.settings.profilesOf(u"TestA"_s);
        QCOMPARE(saved.size(), 1);
        QCOMPARE(saved[0].name, u"Night"_s);
        QCOMPARE(saved[0].fans.value(u"0"_s).fixed, 0.4);      // the snapshot
        QVERIFY(saved[0].global && !saved[0].global->on);
        QCOMPARE(r.settings.activeOf(u"TestA"_s), u"Night"_s);
        QCOMPARE(r.s.removeProfile(u"night"_s), u"Profile not found: night"_s);   // the exact name
        QCOMPARE(r.s.removeProfile(u"Night"_s), QString());
        QVERIFY(r.settings.profilesOf(u"TestA"_s).isEmpty());
        QCOMPARE(r.settings.activeOf(u"TestA"_s), QString());
    }
};

QTEST_GUILESS_MAIN(TstFanState)
#include "tst_fanstate.moc"
