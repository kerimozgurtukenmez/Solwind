#pragma once

// A fake world for the tests, all inside one temporary folder ($CCT): fake nbfc / systemctl / nvidia-smi / lspci
// (tests/fake, first in PATH), a fake /sys (SOLWIND_SYS), fake NBFC files (SOLWIND_NBFC_ROOT) and a settings folder
// (XDG_CONFIG_HOME). Nothing real is read or written.

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>

#include <sys/stat.h>

struct FakeFan {
    QString name;
    bool autoControl = true;
    double target = 30;
    bool critical = false;
};

class FakeWorld
{
public:
    QTemporaryDir dir;

    FakeWorld()
    {
        QDir().mkpath(sysRoot() + QStringLiteral("/sys/class/hwmon"));
        QDir().mkpath(sysRoot() + QStringLiteral("/sys/bus/pci/devices"));
        QDir().mkpath(nbfcRoot() + QStringLiteral("/etc/nbfc"));
        QDir().mkpath(cct() + QStringLiteral("/cfg"));
        // The daemon reads <nbfcRoot>/etc/nbfc/nbfc.json, the fake nbfc writes $CCT/nbfc.json: one file
        write(QStringLiteral("nbfc.json"), "{\"SelectedConfigId\": \"TestA\"}");
        QFile::link(cct() + QStringLiteral("/nbfc.json"), nbfcRoot() + QStringLiteral("/etc/nbfc/nbfc.json"));
        write(QStringLiteral("saved.json"), "{\"TargetFanSpeeds\": []}");
        write(QStringLiteral("calls.log"), "");
        state(true, false, QStringLiteral("TestA"), 50, { { QStringLiteral("CPU Fan") }, { QStringLiteral("GPU Fan") } });
    }

    QString cct() const { return dir.path(); }
    QString sysRoot() const { return dir.path() + QStringLiteral("/root"); }
    QString nbfcRoot() const { return dir.path() + QStringLiteral("/nbfcroot"); }
    QString settingsHome() const { return dir.path() + QStringLiteral("/cfg"); }

    // $CCT/<name> (folders made as needed)
    void write(const QString &name, const QByteArray &text) const { writeAt(cct() + u'/' + name, text); }
    static void writeAt(const QString &path, const QByteArray &text)
    {
        QDir().mkpath(QFileInfo(path).path());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(text);
    }
    QByteArray calls() const
    {
        QFile f(cct() + QStringLiteral("/calls.log"));
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }
    int count(const QByteArray &what) const { return calls().count(what); }

    // The fake NBFC service's state ($CCT/state.json)
    void state(bool running, bool readOnly, const QString &config, double temp, const QList<FakeFan> &fans) const
    {
        QJsonArray fs;
        for (const FakeFan &f : fans)
            fs.append(QJsonObject{ { QStringLiteral("name"), f.name }, { QStringLiteral("auto"), f.autoControl }, { QStringLiteral("target"), f.target },
                                   { QStringLiteral("critical"), f.critical } });
        const QJsonObject s{ { QStringLiteral("running"), running }, { QStringLiteral("readOnly"), readOnly }, { QStringLiteral("config"), config },
                             { QStringLiteral("temp"), temp }, { QStringLiteral("ignoreAuto"), QJsonArray() }, { QStringLiteral("fans"), fs } };
        write(QStringLiteral("state.json"), QJsonDocument(s).toJson());
    }

    // ---------- fake /sys ----------
    // hwmon<n> with a name, temperatures ({file number, label or "" for none, value text}) and fans ({number, rpm})
    struct Temp { int n; QByteArray label; QByteArray value; };
    struct Fan { int n; QByteArray rpm; };
    void hwmon(int n, const QByteArray &name, const QList<Temp> &temps, const QList<Fan> &fans = {}) const
    {
        const QString h = sysRoot() + QStringLiteral("/sys/class/hwmon/hwmon%1").arg(n);
        writeAt(h + QStringLiteral("/name"), name + '\n');
        for (const Temp &t : temps) {
            writeAt(h + QStringLiteral("/temp%1_input").arg(t.n), t.value);
            if (!t.label.isEmpty()) writeAt(h + QStringLiteral("/temp%1_label").arg(t.n), t.label + '\n');
        }
        for (const Fan &f : fans) writeAt(h + QStringLiteral("/fan%1_input").arg(f.n), f.rpm);
    }
    // A PCI device: vendor, class, runtime_status ("" = no file)
    void pci(const QString &addr, const QByteArray &vendor, const QByteArray &cls, const QByteArray &rs = {}) const
    {
        const QString d = sysRoot() + QStringLiteral("/sys/bus/pci/devices/") + addr;
        writeAt(d + QStringLiteral("/vendor"), vendor + '\n');
        writeAt(d + QStringLiteral("/class"), cls + '\n');
        if (!rs.isEmpty()) writeAt(d + QStringLiteral("/power/runtime_status"), rs + '\n');
    }

    // ---------- environment ----------
    QProcessEnvironment env() const
    {
        QProcessEnvironment e = QProcessEnvironment::systemEnvironment();
        e.insert(QStringLiteral("PATH"), QStringLiteral(FAKE_DIR) + u':' + e.value(QStringLiteral("PATH")));
        e.insert(QStringLiteral("CCT"), cct());
        e.insert(QStringLiteral("SOLWIND_SYS"), sysRoot());
        e.insert(QStringLiteral("SOLWIND_NBFC_ROOT"), nbfcRoot());
        e.insert(QStringLiteral("XDG_CONFIG_HOME"), settingsHome());
        return e;
    }
    // The same for code running inside the test process
    void applyToThisProcess() const
    {
        const QProcessEnvironment e = env();
        for (const QString &k : { QStringLiteral("PATH"), QStringLiteral("CCT"), QStringLiteral("SOLWIND_SYS"),
                                  QStringLiteral("SOLWIND_NBFC_ROOT"), QStringLiteral("XDG_CONFIG_HOME") })
            qputenv(k.toLocal8Bit().constData(), e.value(k).toLocal8Bit());
    }
};
