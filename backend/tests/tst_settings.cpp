// settings.json: validation (SPEC.md ST-4, PR-2, PR-6/7, K-30, SE-1..SE-5) and writing. Each test works in its own
// temporary folder; the real ~/.config/solwind is never touched.
#include "core/Paths.h"
#include "core/Settings.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {
QJsonObject obj(const char *json) { return QJsonDocument::fromJson(json).object(); }
QJsonValue val(const char *json) { return QJsonDocument::fromJson(QByteArray("[") + json + "]").array().at(0); }
QList<CurvePoint> pts(std::initializer_list<CurvePoint> l) { return QList<CurvePoint>(l); }
void writeFile(const QString &path, const QByteArray &text)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(text);
}
QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
} // namespace

class TstSettings : public QObject
{
    Q_OBJECT
private slots:
    // ---------- ST-4 ----------
    void cleanStValid()
    {
        const auto st = Settings::cleanSt(val("{\"mode\":2,\"fixed\":0.4,\"curve\":[{\"t\":60,\"s\":50},{\"t\":40,\"s\":20}], \"smooth\":false,\"src\":\"gpu\",\"hyst\":5}"));
        QVERIFY(st);
        QCOMPARE(st->mode, 2);
        QCOMPARE(st->fixed, 0.4);
        QCOMPARE(st->curve, pts({ { 40, 20 }, { 60, 50 } }));     // sorted by temperature
        QCOMPARE(st->smooth, false);
        QCOMPARE(st->src, u"gpu"_s);
        QCOMPARE(st->hyst, 5);
    }
    void cleanStDefaults()
    {
        const auto st = Settings::cleanSt(val("{\"mode\":0}"));
        QVERIFY(st);
        QCOMPARE(st->fixed, 0.5);
        QVERIFY(st->curve.isEmpty());                 // stored as null
        QCOMPARE(st->smooth, true);
        QCOMPARE(st->src, u"cpu"_s);
        QCOMPARE(st->hyst, 3);
        QCOMPARE(st->toJson().value(u"curve"_s), QJsonValue(QJsonValue::Null));
    }
    void cleanStRejects()
    {
        QVERIFY(!Settings::cleanSt(val("null")));
        QVERIFY(!Settings::cleanSt(val("5")));
        QVERIFY(!Settings::cleanSt(val("\"x\"")));
        QVERIFY(!Settings::cleanSt(val("[]")));
        QVERIFY(!Settings::cleanSt(val("{}")));                           // no mode
        QVERIFY(!Settings::cleanSt(val("{\"mode\":\"two\"}")));
        QVERIFY(!Settings::cleanSt(val("{\"mode\":1,\"fixed\":\"x\"}")));       // fixed without a valid speed
        QVERIFY(!Settings::cleanSt(val("{\"mode\":2,\"curve\":[{\"t\":40,\"s\":20}]}")));   // curve with one point
    }
    void cleanStCoercion()
    {
        // JS Number(): these are valid in the old app, so they must be here
        auto a = Settings::cleanSt(val("{\"mode\":null}"));
        QVERIFY(a && a->mode == 0);
        auto b = Settings::cleanSt(val("{\"mode\":\" 1 \",\"fixed\":\"0.3\"}"));
        QVERIFY(b && b->mode == 1 && b->fixed == 0.3);
        // 1.5 rounds up to 2 (curve) and 7 is clamped to 2: without two curve points both are invalid
        QVERIFY(!Settings::cleanSt(val("{\"mode\":1.5}")));
        QVERIFY(!Settings::cleanSt(val("{\"mode\":7,\"fixed\":2}")));
    }
    void cleanStModeClamp()
    {
        auto st = Settings::cleanSt(val("{\"mode\":7,\"curve\":[{\"t\":40,\"s\":20},{\"t\":50,\"s\":30}],\"fixed\":2}"));
        QVERIFY(st);
        QCOMPARE(st->mode, 2);
        QCOMPARE(st->fixed, 1.0);
        // Math.round(0.5) = 1 (fixed), which needs a valid fixed speed
        QVERIFY(!Settings::cleanSt(val("{\"mode\":0.5}")));
        auto half = Settings::cleanSt(val("{\"mode\":0.5,\"fixed\":0.2}"));
        QVERIFY(half);
        QCOMPARE(half->mode, 1);
    }
    void cleanStCurvePoints()
    {
        // Falsy entries become 0 °C / 0 %; others without t/s are dropped; values are clamped and rounded
        auto st = Settings::cleanSt(val("{\"mode\":2,\"curve\":[null,{\"t\":200,\"s\":-5},{\"t\":\"x\",\"s\":1},5,{\"t\":40.5,\"s\":20.5}]}"));
        QVERIFY(st);
        QCOMPARE(st->curve, pts({ { 0, 0 }, { 41, 21 }, { 110, 0 } }));
        // Equal temperatures are kept, in the order the QML engine's (unstable) sort leaves them
        auto dup = Settings::cleanSt(val("{\"mode\":2,\"curve\":[{\"t\":50,\"s\":1},{\"t\":50,\"s\":2},{\"t\":40,\"s\":3}]}"));
        QVERIFY(dup);
        QCOMPARE(dup->curve, pts({ { 40, 3 }, { 50, 2 }, { 50, 1 } }));
        // A curve that isn't an array gives no points
        auto str = Settings::cleanSt(val("{\"mode\":0,\"curve\":\"ab\"}"));
        QVERIFY(str && str->curve.isEmpty());
    }
    void cleanStOtherFields()
    {
        auto st = Settings::cleanSt(val("{\"mode\":0,\"smooth\":\"false\",\"src\":\"GPU\",\"hyst\":\"abc\"}"));
        QVERIFY(st);
        QCOMPARE(st->smooth, true);       // only the boolean false turns it off
        QCOMPARE(st->src, u"cpu"_s);      // exact names only
        QCOMPARE(st->hyst, 3);
        auto h = Settings::cleanSt(val("{\"mode\":0,\"hyst\":12.4}"));
        QVERIFY(h && h->hyst == 10);
    }

    // ---------- PR-2 ----------
    void cleanProfile()
    {
        QVERIFY(!Settings::cleanProfile(val("{\"name\":\"  \"}")));
        QVERIFY(!Settings::cleanProfile(val("{\"name\":5}")));
        QVERIFY(!Settings::cleanProfile(val("[]")));
        const auto p = Settings::cleanProfile(val("{\"name\":\"  Night  \",\"fans\":{\"0\":{\"mode\":1,\"fixed\":0.3},\"x\":{\"mode\":0},\"1\":{\"mode\":\"bad\"}}, \"global\":{\"on\":true,\"st\":{\"mode\":0}}}"));
        QVERIFY(p);
        QCOMPARE(p->name, u"Night"_s);
        QCOMPARE(p->fans.keys(), QStringList{ u"0"_s });      // non-digit key and invalid setting dropped
        QVERIFY(p->global && p->global->on);
        const auto longName = Settings::cleanProfile(QJsonObject{ { u"name"_s, QString(40, u'a') } });
        QVERIFY(longName && longName->name.size() == 32);
        const auto badGlobal = Settings::cleanProfile(val("{\"name\":\"a\",\"global\":{\"on\":true,\"st\":5}}"));
        QVERIFY(badGlobal && !badGlobal->global);
        const auto arrFans = Settings::cleanProfile(val("{\"name\":\"a\",\"fans\":[{\"mode\":0},{\"mode\":1,\"fixed\":0.2}]}"));
        QVERIFY(arrFans && arrFans->fans.keys() == (QStringList{ u"0"_s, u"1"_s }));
    }

    // ---------- reading ----------
    void readers()
    {
        QTemporaryDir tmp;
        writeFile(tmp.path() + u"/settings.json"_s, "{ \"safety\": 85, \"ui\": {\"lang\": \"tr\", \"tray\": false}, \"notify\": {\"profile\": false}, \"configs\": { \"HP\": {\"fans\": {\"0\": {\"mode\":1,\"fixed\":0.4}}, \"global\": {\"on\": true, \"st\": {\"mode\":0}}, \"active\": \"quiet\", \"profiles\": [null, {\"name\":\"Mine\"}, {\"name\":\"\"}]}, \"default\": {\"fans\": [{\"mode\":0}]} }}");
        Settings s(tmp.path());
        QVERIFY(s.load());
        QCOMPARE(s.fanOf(u"HP"_s, 0)->fixed, 0.4);
        QVERIFY(!s.fanOf(u"HP"_s, 1));
        QVERIFY(s.globalOf(u"HP"_s)->on);
        QCOMPARE(s.activeOf(u"HP"_s), u"quiet"_s);
        QCOMPARE(s.profilesOf(u"HP"_s).size(), 1);
        QVERIFY(s.fanOf(QString(), 0));                       // no config id -> "default"; an array of fans works
        QCOMPARE(s.safety(), 85.0);
        QCOMPARE(s.uiLang(), u"tr"_s);
        QVERIFY(!s.uiOn(u"tray"_s));
        QVERIFY(s.uiOn(u"other"_s));
        QVERIFY(!s.notifyOn(u"profile"_s));
        QVERIFY(s.notifyOn(u"enabled"_s));
    }
    void safetyValues()
    {
        QTemporaryDir tmp;
        Settings s(tmp.path());
        QVERIFY(std::isnan(s.safety()));                      // not set: the app uses 90
        writeFile(tmp.path() + u"/settings.json"_s, "{\"safety\": null}");
        QVERIFY(s.load());
        QCOMPARE(s.safety(), 70.0);                           // null -> 0 -> clamped, as in the old app
        writeFile(tmp.path() + u"/settings.json"_s, "{\"safety\": \"abc\"}");
        QVERIFY(s.load());
        QVERIFY(std::isnan(s.safety()));
        writeFile(tmp.path() + u"/settings.json"_s, "{\"safety\": 120}");
        QVERIFY(s.load());
        QCOMPARE(s.safety(), 100.0);
    }

    // ---------- K-30 ----------
    void kbdDefaults()
    {
        QTemporaryDir tmp;
        Settings s(tmp.path());
        const KbdSettings k = s.kbd();
        QCOMPARE(k.effect, u"static"_s);
        QCOMPARE(k.source, u"single"_s);
        QCOMPARE(k.colors, (QStringList{ u"#ff0000"_s, u"#00ff00"_s, u"#0040ff"_s }));
        QCOMPARE(k.speed, 0.5);
        QCOMPARE(k.min, 0.0);
        QCOMPARE(k.max, 1.0);
        QCOMPARE(k.themeCount, 3);
        QCOMPARE(k.color, QString());
    }
    void kbdValues()
    {
        QTemporaryDir tmp;
        writeFile(tmp.path() + u"/settings.json"_s, "{\"kbd\": {\"effect\":\"cycle\",\"source\":\"theme\", \"colors\":[\"#AABBCC\",\"red\",\"#123\",\"#000001\",\"#000002\",\"#000003\",\"#000004\",\"#000005\",\"#000006\\n\"], \"speed\":2,\"min\":0.8,\"max\":0.3,\"themeCount\":4.5,\"color\":\"#b557ff\"}}");
        Settings s(tmp.path());
        QVERIFY(s.load());
        const KbdSettings k = s.kbd();
        QCOMPARE(k.effect, u"cycle"_s);
        QCOMPARE(k.source, u"theme"_s);
        QCOMPARE(k.colors.size(), 6);                         // only #rrggbb, at most 6
        QCOMPARE(k.colors.first(), u"#AABBCC"_s);
        QCOMPARE(k.speed, 1.0);
        QCOMPARE(k.min, 0.3);                                 // lowest above highest: lowered to it
        QCOMPARE(k.max, 0.3);
        QCOMPARE(k.themeCount, 5);                            // Math.round(4.5)
        QCOMPARE(k.color, u"#b557ff"_s);
    }

    // ---------- PR-6 / PR-7 ----------
    void profiles()
    {
        QTemporaryDir tmp;
        Settings s(tmp.path());
        Profile a;
        a.name = u"Gece"_s;
        s.saveProfile(u"HP"_s, a);
        s.setActive(u"HP"_s, u"Gece"_s);
        Profile b;
        b.name = u"gece"_s;
        b.fans.insert(u"0"_s, FanSt{});
        s.saveProfile(u"HP"_s, b);                             // replaces "Gece", the active name follows
        QCOMPARE(s.profilesOf(u"HP"_s).size(), 1);
        QCOMPARE(s.profilesOf(u"HP"_s).first().name, u"gece"_s);
        QCOMPARE(s.activeOf(u"HP"_s), u"gece"_s);
        s.removeProfile(u"HP"_s, u"GECE"_s);                    // removing needs the exact name
        QCOMPARE(s.profilesOf(u"HP"_s).size(), 1);
        s.removeProfile(u"HP"_s, u"gece"_s);
        QCOMPARE(s.profilesOf(u"HP"_s).size(), 0);
        QCOMPARE(s.activeOf(u"HP"_s), QString());
    }

    // ---------- SE-3, SE-4 ----------
    void missingAndInvalidFile()
    {
        QTemporaryDir tmp;
        Settings s(tmp.path() + u"/solwind"_s);
        QVERIFY(!s.load());                                   // missing: defaults, nothing created
        QVERIFY(!QFile::exists(s.path()));
        writeFile(s.path(), "{ not json");
        QVERIFY(!s.load());
        QCOMPARE(readFile(s.path()), QByteArray("{ not json"));   // left alone
        writeFile(s.path(), "[1, 2]");
        QVERIFY(!s.load());                                   // must be an object
    }
    void unreadableFileKept()
    {
        // The QML app overwrote a file it couldn't read; the port keeps it as settings.json.bad first
        QTemporaryDir tmp;
        Settings s(tmp.path());
        writeFile(s.path(), "{ broken");
        QVERIFY(!s.load());
        s.setSafety(80);
        s.flush();
        QCOMPARE(readFile(s.path() + u".bad"_s), QByteArray("{ broken"));
        QCOMPARE(QJsonDocument::fromJson(readFile(s.path())).object().value(u"safety"_s).toDouble(), 80.0);
        s.setSafety(85);                                       // only before the first write
        s.flush();
        QCOMPARE(readFile(s.path() + u".bad"_s), QByteArray("{ broken"));
        // A readable file is never copied
        QTemporaryDir tmp2;
        Settings ok(tmp2.path());
        writeFile(ok.path(), "{}");
        QVERIFY(ok.load());
        ok.setSafety(80);
        ok.flush();
        QVERIFY(!QFile::exists(ok.path() + u".bad"_s));
    }
    void readLikeTheQmlApp()
    {
        // Measured with .dev/settingsdiff.sh: invalid UTF-8 is read (U+FFFD), a raw control character in a string isn't
        QTemporaryDir tmp;
        Settings s(tmp.path());
        writeFile(s.path(), "{\"ui\": {\"note\": \"a\xff" "b\"}, \"safety\": 80}");
        QVERIFY(s.load());
        QCOMPARE(s.data().value(u"ui"_s).toObject().value(u"note"_s).toString(), u"a\ufffdb"_s);
        writeFile(s.path(), "{\"note\": \"tab\there\", \"safety\": 80}");
        QVERIFY(!s.load());
        writeFile(s.path(), "\xef\xbb\xbf{\"safety\": 81}");          // BOM
        QVERIFY(s.load());
        QCOMPARE(s.safety(), 81.0);
        writeFile(s.path(), "{\"safety\": 80, \"safety\": 90}");      // duplicate key: the last one
        QVERIFY(s.load());
        QCOMPARE(s.safety(), 90.0);
    }
    void fansArrayKept()
    {
        QTemporaryDir tmp;
        Settings s(tmp.path());
        writeFile(s.path(), "{\"configs\": {\"HP\": {\"fans\": [{\"mode\": 1, \"fixed\": 0.3}]}}}");
        QVERIFY(s.load());
        FanSt st;
        st.mode = 1;
        st.fixed = 0.6;
        s.setFan(u"HP"_s, 1, st);
        QCOMPARE(s.fanOf(u"HP"_s, 0)->fixed, 0.3);              // the array entry stays
        QCOMPARE(s.fanOf(u"HP"_s, 1)->fixed, 0.6);
    }
    void writesBatchedAndAtomic()
    {
        QTemporaryDir tmp;
        Settings s(tmp.path() + u"/sub/solwind"_s);           // the folder doesn't exist yet
        s.setSafety(80);
        s.setUi(u"tray"_s, false);
        QVERIFY(s.writePending());
        QVERIFY(!QFile::exists(s.path()));                    // nothing written before 500 ms
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(s.path()), 2000);
        QVERIFY(!s.writePending());
        const QJsonObject d = QJsonDocument::fromJson(readFile(s.path())).object();
        QCOMPARE(d.value(u"safety"_s).toDouble(), 80.0);
        QCOMPARE(d.value(u"version"_s).toInt(), 1);
        QVERIFY(d.value(u"configs"_s).isObject());
        QCOMPARE(QDir(tmp.path() + u"/sub/solwind"_s).entryList(QDir::Files), QStringList{ u"settings.json"_s });   // no temp files left
    }
    void flushWritesNow()
    {
        QTemporaryDir tmp;
        Settings s(tmp.path());
        s.setNotify(u"enabled"_s, false);
        s.flush();
        QVERIFY(QFile::exists(s.path()));
        QVERIFY(!s.writePending());
        Settings again(tmp.path());
        QVERIFY(again.load());
        QVERIFY(!again.notifyOn(u"enabled"_s));
    }
    void unknownKeysKept()
    {
        QTemporaryDir tmp;
        writeFile(tmp.path() + u"/settings.json"_s, "{\"version\":1,\"future\":{\"x\":[1,2]},\"configs\":{\"HP\":{\"extra\":true}}}");
        Settings s(tmp.path());
        QVERIFY(s.load());
        s.setFan(u"HP"_s, 0, FanSt{});
        s.flush();
        const QJsonObject d = QJsonDocument::fromJson(readFile(s.path())).object();
        QCOMPARE(d.value(u"future"_s).toObject().value(u"x"_s).toArray().size(), 2);
        QVERIFY(d.value(u"configs"_s).toObject().value(u"HP"_s).toObject().value(u"extra"_s).toBool());
        QVERIFY(Settings(tmp.path()).load());
    }
    void fileFormat()
    {
        // JSON.stringify(data, null, 2) + "\n": two-space indents, empty containers inline
        const QByteArray t = Settings::toFileText(obj("{\"a\":{\"b\":[1,{\"c\":\"x\"}],\"e\":{},\"f\":[]},\"g\":0.45}"));
        QCOMPARE(t, QByteArray("{\n  \"a\": {\n    \"b\": [\n      1,\n      {\n        \"c\": \"x\"\n      }\n    ],\n"
                               "    \"e\": {},\n    \"f\": []\n  },\n  \"g\": 0.45\n}\n"));
        QVERIFY(QJsonDocument::fromJson(Settings::toFileText(obj("{\"s\":\"a\\\"b\\\\c\\nd\"}"))).object().value(u"s"_s)
                == QJsonValue(u"a\"b\\c\nd"_s));
    }
    void setKbdMerges()
    {
        QTemporaryDir tmp;
        Settings s(tmp.path());
        s.setKbd({ { u"effect"_s, u"breathing"_s } });
        s.setKbd({ { u"speed"_s, 0.7 } });
        QCOMPARE(s.kbd().effect, u"breathing"_s);
        QCOMPARE(s.kbd().speed, 0.7);
    }
    void defaultDir()
    {
        qputenv("XDG_CONFIG_HOME", "/tmp/x-config");
        QCOMPARE(Paths::fromEnvironment().settingsDir, u"/tmp/x-config/solwind"_s);
        qputenv("XDG_CONFIG_HOME", "");                        // empty counts as unset
        QCOMPARE(Paths::fromEnvironment().settingsDir, QDir::homePath() + u"/.config/solwind"_s);
        qputenv("SOLWIND_SYS", "/tmp/fake");
        QCOMPARE(Paths::fromEnvironment().sys(u"/class/leds"_s), u"/tmp/fake/sys/class/leds"_s);
        qunsetenv("SOLWIND_SYS");
        QCOMPARE(Paths::fromEnvironment().sys(u"/class/leds"_s), u"/sys/class/leds"_s);
    }
};

QTEST_GUILESS_MAIN(TstSettings)
#include "tst_settings.moc"
