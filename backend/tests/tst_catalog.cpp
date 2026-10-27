// solwind-plugin's checks (PluginTool.h): plugin ids, commit ids, repository addresses (nothing that could run a
// command), the catalog (every field checked, nothing used if anything is wrong), whether a plugin fits this machine
// (a fake /sys, a fake PATH), the installed plugins and their records. Solwind's own plugin-catalog.json is read too.
#include "TestEnv.h"
#include "cli/PluginTool.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace PluginTool;

namespace {
const QString commit = u"8615713a0bb762d3045b06768661e9e14a467560"_s;
QByteArray catalogWith(const QByteArray &plugins, const QByteArray &repo = "https://github.com/a/b.git")
{
    return "{\"version\": 1, \"repository\": \"" + repo + "\", \"plugins\": [" + plugins + "]}";
}
QByteArray entry(const QByteArray &extra = {}, const QByteArray &id = "HP/x")
{
    return "{\"id\": \"" + id + "\", \"kind\": \"keyboard\", \"description\": \"d\", \"commit\": \"" + commit.toLatin1() + "\"" + extra + "}";
}
} // namespace

class TstCatalog : public QObject
{
    Q_OBJECT
private slots:
    void ids()
    {
        for (const char *ok : { "HP/hp-wmi-keyboard", "NBFC/nbfc", "A1/b_c.d+e-f" }) QVERIFY2(validId(QString::fromLatin1(ok)), ok);
        for (const char *bad : { "", "HP", "HP/", "/x", "HP/a/b", "../x", "HP/..", "HP/.x", "-a/b", "a/-b", "HP/a b", "HP/a;b",
                                 "HP/a..b", "HP\\a", "HP/ä" })
            QVERIFY2(!validId(QString::fromUtf8(bad)), bad);
    }
    void commits()
    {
        QVERIFY(validCommit(commit));
        for (const QString &bad : { QString(64, u'a'), commit.left(39), commit.left(12), commit.toUpper(), commit + u'0', QString(40, u'g'), QString() })
            QVERIFY2(!validCommit(bad), qPrintable(bad));
    }
    void repositories()
    {
        for (const char *ok : { "https://github.com/kerimozgurtukenmez/Solwind-plugins.git", "file:///tmp/x/repo.git" })
            QVERIFY2(validRepository(QString::fromLatin1(ok)), ok);
        for (const char *bad : { "ext::sh -c touch% /tmp/x", "ssh://git@github.com/a/b", "git://github.com/a/b", "http://github.com/a/b",
                                 "https://user:pw@github.com/a/b", "https://github.com/a/b?x=1", "https://github.com/", "https:///a",
                                 "-uhttps://x/y", "file://host/x", "file:relative", "github.com/a/b", "https://github.com/a b",
                                 "https://github.com/a/b\nx", "" })
            QVERIFY2(!validRepository(QString::fromUtf8(bad)), bad);
    }
    void catalogs()
    {
        Catalog c;
        QString why;
        QVERIFY2(parseCatalog(catalogWith(entry(", \"tested\": \"t\", \"fits\": {\"programs\": [\"nbfc\"], "
                                                "\"leds\": [{\"name\": \"*::kbd_backlight\", \"driver\": \"hp-wmi\"}, {\"name\": \"x\"}], "
                                                "\"paths\": [\"/sys/devices/platform/hp-wmi/rgb_zones\"], \"vendors\": [\"HP\"]}")),
                              &c, &why), qPrintable(why));
        QCOMPARE(c.plugins.size(), 1);
        const Entry &e = c.plugins[0];
        QCOMPARE(e.id, u"HP/x"_s);
        QCOMPARE(e.tested, u"t"_s);
        QCOMPARE(e.fits.leds.size(), 2);
        QCOMPARE(e.fits.leds[0].driver, u"hp-wmi"_s);
        QCOMPARE(e.fits.leds[1].driver, QString());
        QVERIFY(c.find(u"HP/x"_s) && !c.find(u"HP/y"_s));

        const QList<QByteArray> bad{
            "not json", "[]", "{\"version\": 2, \"repository\": \"https://a/b\", \"plugins\": []}",
            "{\"version\": 1, \"plugins\": []}",
            catalogWith(entry(), "ext::sh -c x"), catalogWith(entry(), "ssh://git@github.com/a/b"),
            "{\"version\": 1, \"repository\": \"https://a/b\"}",
            catalogWith(entry({}, "HP/../x")), catalogWith(entry() + ", " + entry()),
            catalogWith(entry().replace("keyboard", "printer")), catalogWith(entry().replace(commit.toLatin1(), "8615713")),
            catalogWith(entry().replace("\"d\"", "\" \"")),
            catalogWith(entry(", \"fits\": []")), catalogWith(entry(", \"fits\": {\"programs\": []}")),
            catalogWith(entry(", \"fits\": {\"programs\": [\"/bin/sh\"]}")), catalogWith(entry(", \"fits\": {\"paths\": [\"/etc/passwd\"]}")),
            catalogWith(entry(", \"fits\": {\"paths\": [\"/sys/../etc\"]}")), catalogWith(entry(", \"fits\": {\"leds\": [{\"name\": \"a/b\"}]}")),
            catalogWith(entry(", \"fits\": {\"leds\": [{\"name\": \"x\", \"driver\": \"y\", \"run\": \"z\"}]}")),
            catalogWith(entry(", \"fits\": {\"leds\": [{\"name\": \"x\", \"driver\": 1}]}")),
            catalogWith(entry(", \"fits\": {\"commands\": [\"x\"]}")),
            catalogWith("3"),
        };
        for (const QByteArray &b : bad) {
            Catalog x;
            QVERIFY2(!parseCatalog(b, &x, &why), b.constData());
            QVERIFY(!why.isEmpty());
        }
    }
    // Solwind's own catalog: valid, every plugin says when it fits
    void solwindsCatalog()
    {
        QFile f(QStringLiteral(SOLWIND_SOURCE_APP_DIR) + u"/plugin-catalog.json"_s);
        QVERIFY(f.open(QIODevice::ReadOnly));
        Catalog c;
        QString why;
        QVERIFY2(parseCatalog(f.readAll(), &c, &why), qPrintable(why));
        QVERIFY(c.repository.startsWith(u"https://github.com/kerimozgurtukenmez/"_s));
        QVERIFY(!c.plugins.isEmpty());
        for (const Entry &e : c.plugins) QVERIFY2(!e.fits.empty(), qPrintable(e.id));
    }
    void fitsThisMachine()
    {
        FakeWorld w;
        w.applyToThisProcess();                                 // (PATH: the fakes, nbfc among them)
        const QString sys = w.sysRoot() + u"/sys"_s;
        Fits f;
        QCOMPARE(fits(f, w.sysRoot()), Fit::Unknown);
        f.programs = { u"nbfc"_s };
        QCOMPARE(fits(f, w.sysRoot()), Fit::Yes);
        f.programs = { u"no-such-program-x"_s };
        QCOMPARE(fits(f, w.sysRoot()), Fit::No);

        Fits l;
        l.leds = { { u"*::kbd_backlight"_s, u"hp-wmi"_s } };
        QCOMPARE(fits(l, w.sysRoot()), Fit::No);
        QDir().mkpath(sys + u"/class/leds/hp::kbd_backlight/device"_s);
        QDir().mkpath(sys + u"/bus/platform/drivers/other"_s);
        QFile::link(sys + u"/bus/platform/drivers/other"_s, sys + u"/class/leds/hp::kbd_backlight/device/driver"_s);
        QCOMPARE(fits(l, w.sysRoot()), Fit::No);                 // another driver
        l.leds.append({ u"*::kbd_backlight"_s, QString() });
        QCOMPARE(fits(l, w.sysRoot()), Fit::Yes);                // any driver
        l.leds = { { u"*::kbd_zoned_backlight-*"_s, u"other"_s } };
        QCOMPARE(fits(l, w.sysRoot()), Fit::No);                 // the name

        Fits p;
        p.paths = { u"/sys/devices/platform/hp-wmi/rgb_zones"_s };
        QCOMPARE(fits(p, w.sysRoot()), Fit::No);
        QDir().mkpath(sys + u"/devices/platform/hp-wmi/rgb_zones"_s);
        QCOMPARE(fits(p, w.sysRoot()), Fit::Yes);
        p.vendors = { u"hp"_s };                                 // and the vendor too
        QCOMPARE(fits(p, w.sysRoot()), Fit::No);
        FakeWorld::writeAt(sys + u"/class/dmi/id/sys_vendor"_s, "HP\n");
        QCOMPARE(fits(p, w.sysRoot()), Fit::Yes);
    }
    void installedPlugins()
    {
        QTemporaryDir d;
        const QString root = d.path();
        FakeWorld::writeAt(root + u"/HP/a.so"_s, "x");
        FakeWorld::writeAt(root + u"/HP/a.json"_s, "{\"id\": \"HP/a\", \"commit\": \"c\", \"sdk\": \"s\", \"source\": \"catalog\", \"repository\": \"r\"}");
        FakeWorld::writeAt(root + u"/NBFC/nbfc.so"_s, "x");       // installed by hand: no record
        FakeWorld::writeAt(root + u"/a/b/c.so"_s, "x");          // too deep: not a plugin of the tool
        FakeWorld::writeAt(root + u"/top.so"_s, "x");
        const QList<Installed> l = installed(root);
        QCOMPARE(l.size(), 2);
        QCOMPARE(l[0].id, u"HP/a"_s);
        QCOMPARE(l[0].commit, u"c"_s);
        QCOMPARE(l[0].sdk, u"s"_s);
        QCOMPARE(l[0].source, u"catalog"_s);
        QCOMPARE(l[1].id, u"NBFC/nbfc"_s);
        QCOMPARE(l[1].source, QString());
    }
};

QTEST_GUILESS_MAIN(TstCatalog)
#include "tst_catalog.moc"
