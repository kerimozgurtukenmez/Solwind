// KeyboardController against fake /sys trees (SPEC.md §16): the colour restored at start (K-15, K-16), writing (colour
// first, then brightness; turning a dark light on; the brightness read again after Fn; no permission, K-20..K-22),
// effects only where tested (K-12, K-27), a write error stopping the effect and the recovery (K-24, K-26), the fix
// after waking up (K-33), quitting (K-25), observe mode, the permission actions (K-34..K-36, never run: the fake pkexec
// only logs), one read at a time (K-18), the theme's colours following Caelestia's scheme. Real threads and timers.
#include "TestEnv.h"
#include "core/I18n.h"
#include "core/KeyboardController.h"
#include "core/Settings.h"

#include <QSignalSpy>
#include <QStandardPaths>

#include <memory>

using namespace Qt::StringLiterals;

// The recovery's timing without waiting for it (KeyboardController lets this struct in, as phase4_probe's)
struct KeyboardProbe {
    static void schedule(KeyboardController &k) { k.scheduleRecover(); }
    static int interval(const KeyboardController &k) { return k.m_recover.interval(); }
    static bool scheduled(const KeyboardController &k) { return k.m_recover.isActive(); }
    static void stop(KeyboardController &k) { k.m_recover.stop(); }
    static int tries(const KeyboardController &k) { return k.m_recoverTries; }
    static void healthyMinuteSoon(KeyboardController &k) { k.m_healthy.setInterval(1); k.m_healthy.start(); }
};

class TstKeyboard : public QObject
{
    Q_OBJECT

    std::unique_ptr<FakeWorld> w;
    std::unique_ptr<Settings> settings;
    I18n i18n;
    std::unique_ptr<KeyboardController> k;

    QString leds() const { return w->sysRoot() + u"/sys/class/leds/"_s; }
    // A light: brightness, (RGB) multi_index / multi_intensity / multi_max_intensity, a driver link
    QString light(const QString &name, const QString &driver, const QString &brightness, const QString &intensity = QString(),
                  bool writableFiles = true, const QString &max = u"255"_s)
    {
        const QString d = leds() + name;
        auto file = [&](const QString &f, const QString &v, bool rw) {
            FakeWorld::writeAt(d + u'/' + f, v.toUtf8() + '\n');
            QFile(d + u'/' + f).setPermissions(rw ? QFile::ReadOwner | QFile::WriteOwner : QFile::ReadOwner);
        };
        file(u"brightness"_s, brightness, writableFiles);
        file(u"max_brightness"_s, max, false);
        if (!intensity.isNull()) {
            file(u"multi_index"_s, u"red green blue"_s, false);
            file(u"multi_intensity"_s, intensity, writableFiles);
            file(u"multi_max_intensity"_s, u"255 255 255"_s, false);
        }
        const QString drv = w->sysRoot() + u"/sys/bus/platform/drivers/"_s + driver;
        QDir().mkpath(drv);
        QDir().mkpath(d + u"/device"_s);
        QFile::link(drv, d + u"/device/driver"_s);
        return d;
    }
    static QString content(const QString &path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : u"<unreadable>"_s;
    }
    void saved(const QByteArray &kbdJson)
    {
        FakeWorld::writeAt(w->settingsHome() + u"/solwind/settings.json"_s, "{\"version\":1,\"configs\":{},\"kbd\":" + kbdJson + "}");
    }
    void make(bool control = true)
    {
        settings = std::make_unique<Settings>(w->settingsHome() + u"/solwind"_s);
        settings->load();
        i18n.savedLanguage = [this] { return settings->uiLang(); };
        Paths p;
        p.sysRoot = w->sysRoot();
        p.udevRoot = w->cct() + u"/udev"_s;
        p.home = w->cct() + u"/home"_s;
        p.appDir = QStringLiteral(SOLWIND_SOURCE_APP_DIR);
        k = std::make_unique<KeyboardController>(p, *settings, i18n, control);
        k->start();
        QTRY_VERIFY(k->ready());
    }

private slots:
    void init()
    {
        k.reset();
        settings.reset();
        w = std::make_unique<FakeWorld>();
        w->applyToThisProcess();
        QVERIFY(QStandardPaths::findExecutable(u"pkexec"_s).startsWith(QStringLiteral(FAKE_DIR)));
    }
    void cleanup() { k.reset(); settings.reset(); }

    // ---------- start (K-15, K-16) ----------
    void startAppliesTheSavedColour()
    {
        const QString d = light(u"hp::kbd_backlight"_s, u"hp-wmi"_s, u"200"_s, u"10 20 30"_s);
        saved("{\"color\": \"#ff0000\", \"effect\": \"static\"}");
        make();
        QCOMPARE(k->backend(), u"led-rgb"_s);
        QTRY_COMPARE(content(d + u"/multi_intensity"_s), u"255 0 0"_s);
        QCOMPARE(content(d + u"/brightness"_s), u"200"_s);       // untouched
        QCOMPARE(Qml::hex(k->color()), u"#FF0000"_s);
    }
    void startLeavesALightThatIsOff()
    {
        const QString d = light(u"hp::kbd_backlight"_s, u"hp-wmi"_s, u"0"_s, u"10 20 30"_s);
        saved("{\"color\": \"#ff0000\"}");
        make();
        QTest::qWait(300);
        QCOMPARE(content(d + u"/multi_intensity"_s), u"10 20 30"_s);   // turned off with Fn: writing must not turn it on
        QCOMPARE(Qml::hex(k->color()), u"#0A141E"_s);                   // the colour it shows
    }
    void asusColourOnceAtStart()
    {
        const QString d = light(u"asus::kbd_backlight"_s, u"asus-nb-wmi"_s, u"2"_s, QString(), true, u"3"_s);
        FakeWorld::writeAt(d + u"/kbd_rgb_mode"_s, "");
        saved("{\"color\": \"#00ff00\"}");
        make();
        QCOMPARE(k->backend(), u"asus-tuf"_s);
        QSignalSpy wrote(k.get(), &KeyboardController::wrote);
        QTRY_COMPARE(content(d + u"/kbd_rgb_mode"_s), u"0 0 0 255 0 1"_s);   // cmd 0: never saved to the BIOS
        k->refresh();
        QTest::qWait(300);
        QCOMPARE(wrote.count(), 1);                              // only at the first read
    }

    // ---------- writing (K-20..K-22) ----------
    void colourFirstThenBrightness()
    {
        const QString d = light(u"hp::kbd_backlight"_s, u"hp-wmi"_s, u"200"_s, u"10 20 30"_s);
        make();
        QSignalSpy wrote(k.get(), &KeyboardController::wrote);
        k->setBrightness(100);
        k->setColor(QColor::fromString(u"#0000ff"));
        QTRY_COMPARE(wrote.count(), 1);                          // one flush (80 ms) for both
        QCOMPARE(wrote[0][0].toStringList(), (QStringList{ d + u"/multi_intensity"_s, u"0 0 255"_s, d + u"/brightness"_s, u"100"_s }));
        QCOMPARE(content(d + u"/brightness"_s), u"100"_s);
        QCOMPARE(settings->kbd().color, u"#0000FF"_s);
    }
    void aColourTurnsADarkLightOn()
    {
        const QString d = light(u"hp::kbd_backlight"_s, u"hp-wmi"_s, u"0"_s, u"10 20 30"_s);
        make();
        k->setColor(QColor::fromString(u"#ff0000"));
        QTRY_COMPARE(content(d + u"/brightness"_s), u"255"_s);
        QCOMPARE(content(d + u"/multi_intensity"_s), u"255 0 0"_s);
    }
    void brightnessReadAgainAfterFn()
    {
        const QString d = light(u"hp::kbd_backlight"_s, u"hp-wmi"_s, u"200"_s, u"10 20 30"_s);
        make();
        FakeWorld::writeAt(d + u"/brightness"_s, "0\n");        // the key turned it off behind our back
        const qint64 later = QDateTime::currentMSecsSinceEpoch() + 2000;
        k->now = [later] { return later; };                       // what we know is older than a second
        k->setColor(QColor::fromString(u"#00ff00"));
        QTRY_COMPARE(content(d + u"/brightness"_s), u"255"_s);
        QCOMPARE(k->brightness(), 255);
    }
    void withoutPermissionNothingChanges()
    {
        const QString d = light(u"hp::kbd_backlight"_s, u"hp-wmi"_s, u"200"_s, u"10 20 30"_s, false);
        make();
        QVERIFY(!k->writable());
        QSignalSpy wrote(k.get(), &KeyboardController::wrote);
        const QColor before = k->color();
        k->setColor(QColor::fromString(u"#00ff00"));
        k->setBrightness(10);
        QTest::qWait(300);
        QCOMPARE(wrote.count(), 0);
        QCOMPARE(k->color(), before);
        QCOMPARE(k->brightness(), 200);
    }
    void zonesTogether()
    {
        QStringList zones;
        for (const char *z : { "left", "middle", "right", "wasd" })
            zones << light(u"hp::kbd_zoned_backlight-"_s + QString::fromLatin1(z), u"hp-wmi"_s, u"100"_s, u"1 2 3"_s);
        make();
        QCOMPARE(k->backend(), u"hp-zoned"_s);
        QCOMPARE(k->name(), u"hp::kbd_zoned_backlight ×4"_s);
        QVERIFY(!k->effectsAllowed());
        QSignalSpy wrote(k.get(), &KeyboardController::wrote);
        k->setColor(QColor::fromString(u"#00ff00"));
        QTest::qWait(150);
        QCOMPARE(wrote.count(), 0);                              // zoned: written after 250 ms, not 80
        QTRY_COMPARE(wrote.count(), 1);
        QStringList expected;
        for (const QString &z : zones) expected << z + u"/multi_intensity"_s << u"0 255 0"_s;
        QCOMPARE(wrote[0][0].toStringList(), expected);
    }
    void hpRgbZonesHex()
    {
        const QString z = w->sysRoot() + u"/sys/devices/platform/hp-wmi/rgb_zones/"_s;
        for (const char *n : { "zone00", "zone01" }) FakeWorld::writeAt(z + QString::fromLatin1(n), "FF0000\n");
        make();
        QCOMPARE(k->backend(), u"hp-rgbzones"_s);
        QVERIFY(!k->hasBrightness());
        k->setColor(QColor::fromString(u"#12ab9c"));
        QTRY_COMPARE(content(z + u"zone01"_s), u"12AB9C"_s);
        QCOMPARE(content(z + u"zone00"_s), u"12AB9C"_s);
    }

    // ---------- effects (K-12, K-27, K-31) ----------
    void effectRuns()
    {
        light(u"hp::kbd_backlight"_s, u"hp-wmi"_s, u"200"_s, u"10 20 30"_s);
        saved("{\"effect\": \"cycle\", \"source\": \"multi\", \"colors\": [\"#ff0000\", \"#0000ff\"], \"speed\": 1}");
        make();
        QVERIFY(k->effectActive());
        QSignalSpy frames(k.get(), &KeyboardController::wroteFrame);
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() >= 8, 3000);
        QSet<QString> distinct;
        for (const auto &f : frames) distinct << f[0].toString();
        QVERIFY(distinct.size() >= 4);
    }
    void effectsOnlyWhereTested()
    {
        light(u"x::kbd_backlight"_s, u"some-driver"_s, u"200"_s, u"10 20 30"_s);
        saved("{\"effect\": \"breathing\"}");
        make();
        QCOMPARE(k->backend(), u"led-rgb"_s);
        QVERIFY(!k->effectsAllowed());
        QSignalSpy frames(k.get(), &KeyboardController::wroteFrame);
        QTest::qWait(500);
        QCOMPARE(frames.count(), 0);
        QVERIFY(k->statusText().contains(u"Effect: static"_s));
    }

    // ---------- a write error, recovery (K-24, K-26) ----------
    void writeErrorStopsTheEffectThenRecovers()
    {
        const QString d = light(u"hp::kbd_backlight"_s, u"hp-wmi"_s, u"200"_s, u"10 20 30"_s);
        saved("{\"effect\": \"cycle\", \"source\": \"multi\", \"colors\": [\"#ff0000\", \"#0000ff\"], \"speed\": 1}");
        make();
        QSignalSpy frames(k.get(), &KeyboardController::wroteFrame);
        QTRY_VERIFY(frames.count() >= 2);
        // The colour file goes away and something that can't be written is there: the next frame fails
        QFile::remove(d + u"/multi_intensity"_s);
        QDir().mkpath(d + u"/multi_intensity"_s);
        QTRY_VERIFY(k->error().startsWith(u"Could not write to the keyboard light: "_s));
        QCOMPARE(k->effect(), u"static"_s);
        QVERIFY(k->lostEffect());
        QCOMPARE(settings->kbd().effect, u"cycle"_s);           // (not saved)
        // It comes back before the first look (2 s): the effect resumes
        QDir(d + u"/multi_intensity"_s).removeRecursively();
        FakeWorld::writeAt(d + u"/multi_intensity"_s, "1 2 3\n");
        frames.clear();
        QTRY_VERIFY_WITH_TIMEOUT(k->effect() == u"cycle"_s && frames.count() >= 2, 6000);
        QCOMPARE(k->error(), QString());
        QVERIFY(!k->lostEffect());
    }
    void lightGoneThenBack()
    {
        const QString d = light(u"hp::kbd_backlight"_s, u"hp-wmi"_s, u"200"_s, u"10 20 30"_s);
        saved("{\"color\": \"#0000ff\"}");
        make();
        QTRY_COMPARE(content(d + u"/multi_intensity"_s), u"0 0 255"_s);
        QDir(d).removeRecursively();                             // the module was unloaded
        k->setColor(QColor::fromString(u"#00ff00"));            // ... and the write fails
        QTRY_VERIFY(!k->error().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!k->available(), 4000);        // the first look (2 s): no light
        light(u"hp::kbd_backlight"_s, u"hp-wmi"_s, u"200"_s, u"0 0 0"_s);   // loaded again
        // The second look (4 s later) finds it writable: the chosen colour is written again
        QTRY_COMPARE_WITH_TIMEOUT(content(d + u"/multi_intensity"_s), u"0 255 0"_s, 8000);
        QCOMPARE(k->error(), QString());
    }

    // K-26: 2, 4, 8, 16, 32 s, then a minute; a light that is there gets 5 tries, a missing one is looked for on;
    // a healthy minute resets the count
    void recoveryTiming()
    {
        light(u"hp::kbd_backlight"_s, u"hp-wmi"_s, u"200"_s, u"10 20 30"_s);
        make();
        QList<int> seen;
        for (int i = 0; i < 6; ++i) {
            KeyboardProbe::schedule(*k);
            seen << (KeyboardProbe::scheduled(*k) ? KeyboardProbe::interval(*k) : -1);
            KeyboardProbe::stop(*k);
        }
        QCOMPARE(seen, (QList<int>{ 2000, 4000, 8000, 16000, 32000, -1 }));   // there: left alone after 5
        QCOMPARE(KeyboardProbe::tries(*k), 5);
        KeyboardProbe::healthyMinuteSoon(*k);
        QTRY_COMPARE(KeyboardProbe::tries(*k), 0);
        // Missing: looked for on, at most every minute
        QDir(leds()).removeRecursively();
        k->refresh();
        QTRY_VERIFY(!k->available());
        seen.clear();
        for (int i = 0; i < 7; ++i) {
            KeyboardProbe::schedule(*k);
            seen << (KeyboardProbe::scheduled(*k) ? KeyboardProbe::interval(*k) : -1);
            KeyboardProbe::stop(*k);
        }
        QCOMPARE(seen, (QList<int>{ 2000, 4000, 8000, 16000, 32000, 60000, 60000 }));
    }
    // K-28: a cycle needs several colours
    void cycleNeedsColours()
    {
        light(u"hp::kbd_backlight"_s, u"hp-wmi"_s, u"200"_s, u"10 20 30"_s);
        saved("{\"effect\": \"static\", \"source\": \"single\"}");
        make();
        k->setEffect(u"cycle"_s);
        QCOMPARE(k->source(), u"multi"_s);                       // forced, and saved
        QCOMPARE(settings->kbd().source, u"multi"_s);
        QVERIFY(!k->setSource(u"single"_s));                     // refused during a cycle
        QCOMPARE(k->source(), u"multi"_s);
        QVERIFY(k->setSource(u"theme"_s));
        k->setEffect(u"breathing"_s);
        QVERIFY(k->setSource(u"single"_s));                      // fine for breathing
    }

    // ---------- after waking up (K-33) ----------
    void wakeFixesABrightnessLeftAtZero()
    {
        const QString d = light(u"hp::kbd_backlight"_s, u"hp-wmi"_s, u"200"_s, u"10 20 30"_s);
        make();
        FakeWorld::writeAt(d + u"/brightness"_s, "0\n");        // what the module says after the firmware lit it again
        const qint64 later = QDateTime::currentMSecsSinceEpoch() + 60000;
        k->now = [later] { return later; };                       // the next check sees a minute go by
        QTRY_COMPARE_WITH_TIMEOUT(content(d + u"/brightness"_s), u"200"_s, 7000);
    }
    void wakeLeavesWhatFnTurnedOff()
    {
        const QString d = light(u"hp::kbd_backlight"_s, u"hp-wmi"_s, u"200"_s, u"10 20 30"_s);
        make();
        FakeWorld::writeAt(d + u"/brightness"_s, "0\n");
        FakeWorld::writeAt(d + u"/brightness_hw_changed"_s, "0\n");   // the key did it
        const qint64 later = QDateTime::currentMSecsSinceEpoch() + 60000;
        k->now = [later] { return later; };
        QTRY_COMPARE_WITH_TIMEOUT(k->brightness(), 0, 7000);
        QTest::qWait(200);
        QCOMPARE(content(d + u"/brightness"_s), u"0"_s);
    }

    // ---------- quitting, observe mode ----------
    void quitGoesBackToTheStaticColour()
    {
        const QString d = light(u"hp::kbd_backlight"_s, u"hp-wmi"_s, u"200"_s, u"10 20 30"_s);
        saved("{\"effect\": \"breathing\", \"color\": \"#00ff00\", \"speed\": 1}");
        make();
        QSignalSpy frames(k.get(), &KeyboardController::wroteFrame);
        QTRY_VERIFY(frames.count() >= 2);
        bool done = false;
        k->restoreForQuit([&] { done = true; });
        QTRY_VERIFY(done);
        QCOMPARE(content(d + u"/multi_intensity"_s), u"0 255 0"_s);
        QVERIFY(k->quitting());
        frames.clear();
        QTest::qWait(400);
        QCOMPARE(frames.count(), 0);                             // nothing written after it
        QCOMPARE(settings->kbd().effect, u"breathing"_s);        // the effect resumes on the next start
    }
    void observeWritesNothing()
    {
        const QString d = light(u"hp::kbd_backlight"_s, u"hp-wmi"_s, u"200"_s, u"10 20 30"_s);
        saved("{\"effect\": \"static\", \"color\": \"#ff0000\"}");   // (a light that is on: the start would write it)
        make(false);
        QSignalSpy frames(k.get(), &KeyboardController::wroteFrame);
        QTest::qWait(500);
        QCOMPARE(frames.count(), 0);
        QVERIFY(!k->effectActive());
        QCOMPARE(content(d + u"/multi_intensity"_s), u"10 20 30"_s);
        bool done = false;
        k->restoreForQuit([&] { done = true; });
        QVERIFY(done);
        QCOMPARE(content(d + u"/multi_intensity"_s), u"10 20 30"_s);
    }

    // ---------- permission (K-34..K-36): only the command, never run ----------
    void permissionActions()
    {
        light(u"hp::kbd_backlight"_s, u"hp-wmi"_s, u"200"_s, u"10 20 30"_s);
        saved("{\"effect\": \"cycle\", \"source\": \"multi\"}");
        make();
        w->write(u"pkexec-noexec"_s, "0");
        QVERIFY(k->effectActive());
        k->grantPermission();
        QVERIFY(k->paused());                                    // no frame while the files change hands
        QVERIFY(k->permBusy());
        QTRY_VERIFY(!k->permBusy() && !k->paused());
        QCOMPARE(k->error(), QString());
        const QString log = QString::fromUtf8(w->calls());
        QVERIFY(log.contains(u"pkexec sh -c uid=$PKEXEC_UID;"_s));
        QVERIFY(log.contains(QStringLiteral(SOLWIND_SOURCE_APP_DIR) + u"/dist/90-solwind.rules"_s));
        QVERIFY(log.contains(QStringLiteral(SOLWIND_SOURCE_APP_DIR) + u"/scripts/kbd-rule-check.sh"_s));
        w->write(u"pkexec-noexec"_s, "126");                     // the password dialog dismissed
        k->revokePermission();
        QTRY_VERIFY(!k->permBusy());
        QCOMPARE(k->error(), u"Cancelled."_s);
        QVERIFY(QString::fromUtf8(w->calls()).contains(u"pkexec sh -c rm -f /etc/udev/rules.d/90-solwind.rules"_s));
    }

    // ---------- reading (K-18), theme ----------
    void oneReadAtATime()
    {
        light(u"hp::kbd_backlight"_s, u"hp-wmi"_s, u"200"_s, u"10 20 30"_s);
        make();
        QStringList order;
        k->refresh([&] { order << u"a"_s; });
        k->refresh([&] { order << u"b"_s; });
        k->refresh([&] { order << u"c"_s; });
        QTRY_COMPARE(order.size(), 3);
        QCOMPARE(order, (QStringList{ u"a"_s, u"b"_s, u"c"_s }));
    }
    void themeFollowsCaelestia()
    {
        light(u"hp::kbd_backlight"_s, u"hp-wmi"_s, u"200"_s, u"10 20 30"_s);
        const QString scheme = w->cct() + u"/home/.local/state/caelestia/scheme.json"_s;
        FakeWorld::writeAt(scheme, "{\"colours\": {\"primary\": \"ff0000\", \"secondary\": \"00ff00\", \"tertiary\": \"0000ff\"}}");
        make();
        QCOMPARE(Qml::hex(k->themeColors().value(0)), u"#FF0000"_s);
        QSignalSpy changed(k.get(), &KeyboardController::changed);
        FakeWorld::writeAt(scheme + u".new"_s, "{\"colours\": {\"primary\": \"00ffff\", \"secondary\": \"00ff00\", \"tertiary\": \"0000ff\"}}");
        QFile::remove(scheme);
        QFile::rename(scheme + u".new"_s, scheme);              // replaced, as an atomic write does
        QTRY_COMPARE(Qml::hex(k->themeColors().value(0)), u"#00FFFF"_s);
        QVERIFY(changed.count() >= 1);
    }
};

QTEST_GUILESS_MAIN(TstKeyboard)
#include "tst_keyboard.moc"
