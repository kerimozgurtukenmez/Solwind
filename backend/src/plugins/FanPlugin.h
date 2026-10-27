#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>
#include <limits>

class FanService;

// What a fan plugin offers the core (ARCH-PLAN.md "Plugin contracts"). A plugin only reads the fans' state and
// carries out three kinds of command: a speed, "back to automatic", "everything back to automatic". Every decision -
// curves, the safety limit, "did it take", handing fans back, max fan - is made by the core (FanController), never
// here (ARCH-PLAN rule 2). Writing is allowed only when the core sees `trust() == WriteTested` (rule 4).
class FanPlugin : public QObject
{
    Q_OBJECT
public:
    // Trust level (ARCH-PLAN rule 4): a new plugin is ReadOnly until someone with that hardware has tested it
    enum class Trust { ReadOnly, Untested, WriteTested };

    struct Fan {
        QString name;
        double temp = std::numeric_limits<double>::quiet_NaN();   // the temperature the driver uses for this fan
        bool autoControl = true;          // the driver / firmware decides the speed
        bool critical = false;            // it runs the fan at 100% by itself (e.g. NBFC's critical mode)
        double current = 0;               // %
        double target = 0;                // %
    };
    // A command's result: ok, and the first line of what went wrong
    using Done = std::function<void(bool ok, const QString &error)>;

    using QObject::QObject;

    virtual QString id() const = 0;       // never empty ("" is the core's NoFanPlugin: no fan plugin installed)
    virtual Trust trust() const = 0;

    // ---------- running ----------
    virtual void start() = 0;             // begin reading (after the core connected to its signals)
    // Read often (a window is open, or the core drives fans) or relaxed
    virtual void setFast(bool fast) = 0;
    virtual int interval() const = 0;     // how often it reads now (ms)
    virtual void setQuitting(bool on) { Q_UNUSED(on) }   // the core is quitting (no new admin actions)
    // What it offers beyond the fans (NBFC: its service, configs); nullptr if nothing
    virtual FanService *service() { return nullptr; }
    virtual const FanService *service() const { return nullptr; }

    // ---------- state (updated by reads; statusRead after each one) ----------
    virtual bool running() const = 0;     // the driver / service answers
    virtual bool readOnly() const = 0;    // it answers but takes no speeds now (e.g. NBFC's read-only mode)
    virtual bool holding() const = 0;     // speeds must not be written now (an action that handed the fans back runs)
    virtual bool quiet() const = 0;       // an action of the user's explains failures right now: don't report them
    virtual QString configId() const = 0; // fan settings are saved per this id (NBFC: the config the fans belong to)
    virtual double temp() const = 0;      // the driver's own temperature (NaN if none)
    virtual const QList<Fan> &fans() const = 0;
    // "Did the driver see it yet?" is judged by counting reads, not by the clock (SPEC.md N-9, C-3)
    virtual quint64 pollSeq() const = 0;  // reads started so far
    virtual quint64 statusSeq() const = 0;   // the number of the read last finished
    // Why speeds can't be written now, in the UI language ("" = they can): not running, read-only, or - withHolding -
    // an action that handed the fans back is still running
    virtual QString blockedReason(bool withHolding = true) const = 0;

    // ---------- commands (carried out one after another, in order) ----------
    // A newer speed or auto for the same fan replaces one that hasn't run yet
    virtual void setPercent(int fan, int pct, Done done) = 0;
    virtual void setAuto(int fan, Done done) = 0;            // fan -1: all of them
    // Every fan back to automatic; never replaced, runs after the commands already waiting (quit, hand-back)
    virtual void releaseAll(Done done) = 0;
    // After quitting without being able to hand the fans back (the driver doesn't run): true if the driver will set
    // fixed speeds again by itself when it starts (NBFC: /var/lib/nbfc/state.json, SPEC.md QT-3)
    virtual void willRestoreFixedSpeeds(std::function<void(bool)> done) = 0;

signals:
    void statusRead(bool ok);             // after every read
    void changed();
    void configIdChanged();
    void serviceLost();                   // it stopped answering, and nothing the user did explains it (SPEC.md L-2)
};
