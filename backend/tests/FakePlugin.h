#pragma once

// An in-process fan plugin for the core's tests: the state is set by the test, every command is recorded and stays
// pending until the test finishes it (or `autoFinish` answers it at once), reads are simulated with read().

#include "plugins/FanPlugin.h"

#include <QStringList>

class FakePlugin : public FanPlugin
{
    Q_OBJECT
public:
    struct Command {
        QString what;            // "spd 0 40", "auto 1", "auto all", "release"
        Done done;
    };

    Trust trustLevel = Trust::WriteTested;
    bool isRunning = true, isReadOnly = false, isHolding = false, isQuiet = false;
    QString config = QStringLiteral("TestA");
    double driverTemp = 50;
    QList<Fan> fanList;
    quint64 polls = 0, reads = 0;
    QList<Command> commands;     // pending, in order
    QStringList log;             // every command ever given
    bool autoFinish = false;
    bool fixedSaved = false;     // willRestoreFixedSpeeds' answer

    explicit FakePlugin(int fans = 2)
    {
        for (int i = 0; i < fans; ++i) fanList.append({ QStringLiteral("Fan %1").arg(i + 1), 50, true, false, 30, 30 });
    }

    QString id() const override { return QStringLiteral("fake"); }
    Trust trust() const override { return trustLevel; }
    bool running() const override { return isRunning; }
    bool readOnly() const override { return isReadOnly; }
    bool holding() const override { return isHolding; }
    bool quiet() const override { return isQuiet; }
    QString configId() const override { return config; }
    double temp() const override { return driverTemp; }
    const QList<Fan> &fans() const override { return fanList; }
    quint64 pollSeq() const override { return polls; }
    quint64 statusSeq() const override { return reads; }
    QString blockedReason(bool withHolding) const override
    {
        if (!isRunning) return QStringLiteral("The NBFC service is not running");
        if (isReadOnly) return QStringLiteral("NBFC is in read-only mode, fan speeds can't be written");
        if (withHolding && isHolding) return QStringLiteral("Waiting for the NBFC service action to finish");
        return QString();
    }

    void add(const QString &what, Done done, const QString &key)
    {
        log << what;
        // A newer command with the same key replaces a waiting one (as the NBFC queue does)
        if (!key.isEmpty())
            for (qsizetype i = 0; i < commands.size(); ++i)
                if (keyOf(commands[i].what) == key) { commands.removeAt(i); break; }
        commands.append({ what, std::move(done) });
        if (autoFinish) finishAll(true);
    }
    static QString keyOf(const QString &what)
    {
        const QStringList w = what.split(u' ');
        if (w[0] == u"spd") return QStringLiteral("spd:") + w[1];
        if (w[0] == u"auto") return QStringLiteral("auto:") + w[1];
        return QString();
    }
    void setPercent(int fan, int pct, Done done) override
    {
        add(QStringLiteral("spd %1 %2").arg(fan).arg(pct), std::move(done), QStringLiteral("spd:%1").arg(fan));
    }
    void setAuto(int fan, Done done) override
    {
        const QString f = fan < 0 ? QStringLiteral("all") : QString::number(fan);
        add(QStringLiteral("auto ") + f, std::move(done), QStringLiteral("auto:") + f);
    }
    void releaseAll(Done done) override { add(QStringLiteral("release"), std::move(done), QString()); }
    void willRestoreFixedSpeeds(std::function<void(bool)> done) override { done(fixedSaved); }

    // Finish the first pending command: like the driver, a speed / auto changes the fan's state
    void finishOne(bool ok, const QString &error = QString())
    {
        if (commands.isEmpty()) return;
        Command c = commands.takeFirst();
        if (ok) {
            const QStringList w = c.what.split(u' ');
            if (w[0] == u"spd") { Fan &f = fanList[w[1].toInt()]; f.autoControl = false; f.target = w[2].toDouble(); }
            else if (w[0] == u"auto" || w[0] == u"release") {
                for (int i = 0; i < fanList.size(); ++i)
                    if (w[0] == u"release" || w[1] == u"all" || w[1].toInt() == i) { fanList[i].autoControl = true; fanList[i].target = 30; }
            }
        }
        if (c.done) c.done(ok, error);
    }
    void finishAll(bool ok = true) { while (!commands.isEmpty()) finishOne(ok); }
    QStringList pending() const
    {
        QStringList w;
        for (const Command &c : commands) w << c.what;
        return w;
    }

    // A status read: starts (pollSeq) and finishes (statusSeq); the caller runs the controller after it
    void read() { reads = ++polls; }
    void readStartedBefore() { ++polls; }          // a read that started now and finishes later
};
