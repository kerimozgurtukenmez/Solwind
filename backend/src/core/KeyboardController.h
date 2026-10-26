#pragma once

#include "CaelestiaScheme.h"
#include "Cmd.h"
#include "KbdIo.h"
#include "KeyboardParse.h"
#include "Paths.h"

#include <QColor>
#include <QHash>
#include <QObject>
#include <QThread>
#include <QTimer>

#include <functional>
#include <optional>

class I18n;
class Settings;

// The keyboard light - port of services/Keyboard.qml (SPEC.md §16, K-14..K-39) on top of the keyboard plugins:
// detection (one read at a time), the chosen colour and brightness, writing (colour first, then brightness), software
// effects (breathing, colour cycle), recovery after a write error, the brightness fix after waking up, granting and
// removing the write permission (pkexec, the same scripts and rule as 0.5.x), the texts of `solwind kbd`.
// In observe mode (control false) it only reads: nothing is ever written, no effect runs.
// Deliberate differences (SPEC.md §21d): writes go through a thread of the daemon instead of an awk process (no
// lost writes while it restarts; nothing is created in /sys; an effect skips a frame while the last one is still being
// written), and once quitting has begun nothing is written but the static colour.
class KeyboardController : public QObject
{
    Q_OBJECT
public:
    KeyboardController(const Paths &paths, Settings &settings, const I18n &i18n, bool control, QObject *parent = nullptr);
    ~KeyboardController() override;

    void start();                                         // the first read (Keyboard.qml: Component.onCompleted)
    std::function<qint64()> now;                          // the clock (tests)

    // ---------- what was found (K-6..K-13, K-17) ----------
    const KeyboardPlugin *plugin() const { return m_plugin; }
    QString backend() const { return m_plugin ? m_plugin->id() : QString(); }
    bool available() const { return m_plugin; }
    QStringList zones() const { return m_zones; }
    QString name() const { return m_name; }
    QString driver() const { return m_driver; }
    QStringList dmi() const { return m_dmi; }             // vendor, product, family, board
    QString kernel() const { return m_kernel; }
    QStringList notes() const { return m_notes; }
    bool rgb() const { return m_plugin && m_plugin->rgb(); }
    bool hasBrightness() const { return m_plugin && m_plugin->hasBrightness(); }
    bool effectsAllowed() const { return rgb() && m_plugin->effectsTested(m_driver); }
    bool tested() const { return m_plugin && m_plugin->tested(m_driver); }
    bool writable() const { return m_writable; }
    bool ruleLegacy() const { return m_ruleLegacy; }
    bool ruleOld() const { return m_ruleOld; }
    QString ruleAt() const { return m_ruleAt; }
    bool ready() const { return m_ready; }
    bool permBusy() const { return m_permBusy; }
    bool control() const { return m_control; }
    bool quitting() const { return m_quitting; }

    // ---------- state ----------
    int maxBrightness() const { return m_maxBrightness; }
    int brightness() const { return m_brightness; }
    QColor color() const { return m_color; }
    QString error() const { return m_error; }
    QString effect() const { return m_effect; }
    QString source() const { return m_source; }
    QStringList colors() const { return m_colors; }
    double speed() const { return m_speed; }
    double period() const { return 8 - 7 * m_speed; }     // one breath / one colour (s): 8 .. 1
    bool paused() const { return m_paused; }
    bool effectActive() const { return m_control && !m_quitting && m_effect != QLatin1String("static") && effectsAllowed() && m_writable && !m_paused; }
    int cycleIndex() const { return m_cycleIndex; }
    int themeCount() const { return m_themeCount; }
    double minLevel() const { return m_minLevel; }
    double maxLevel() const { return m_maxLevel; }
    bool lostEffect() const { return m_lostEffect; }
    QList<QColor> themeColors() const;
    QList<QColor> cycle() const;                          // the colours the effect goes through

    // ---------- actions (Keyboard.qml) ----------
    void refresh(std::function<void()> done = nullptr);   // K-18
    void setBrightness(double b);                         // K-21
    void setColor(const QColor &c);                       // K-21 (also "#rrggbb" through QColor::fromString)
    void setEffect(const QString &e);                     // K-28
    bool setSource(const QString &s);                     // K-28
    void setThemeCount(double n);
    void setColors(const QStringList &list);
    void setSpeed(double v);
    void setRange(double lo, double hi);
    void grantPermission();                               // K-34, K-36
    void revokePermission();                              // K-35, K-36
    QStringList permissionCommand(bool grant) const;      // the pkexec command line (also compared with the QML's)
    void flushSaves();
    // Before quitting: back to the chosen static colour, pending writes done (K-25); then done (also at once).
    // write false: nothing more is written at all (the QML app is taking the light over)
    void restoreForQuit(std::function<void()> done, bool write = true);

    // ---------- texts (K-39) ----------
    QString kindText() const;
    QString infoText() const;
    QString supportText() const;
    QString noteText(const QString &n) const;
    QString noPermissionText() const;
    QString statusText() const;

    // ---------- the maths (pure, as in Keyboard.qml; also for the differential check) ----------
    QString colorValue(const QColor &c, double level) const;
    QString frame(const QColor &c, double level) const;   // K-31
    static QColor mix(const QColor &a, const QColor &b, double f);
    static QColor mixHsv(const QColor &a, const QColor &b, double f);
    static QColor vivid(const QColor &c);
    static QList<QColor> themeColorsOf(const QColor &primary, const QColor &secondary, const QColor &tertiary,
                                       const QStringList &accents, int count);
    static constexpr int lowCut = 8;                      // below this the light turns off instead of a wrong hue
    static constexpr int wakeGap = 15000;                 // a wall-clock jump this long between wake checks = slept

signals:
    void changed();
    void wroteFrame(const QString &value);                // (tests)
    void wrote(const QStringList &pathsAndValues, const QString &error);   // (tests: every batch, in order)

private:
    friend struct KeyboardProbe;                          // (tests/phase4_probe.cpp: sets the state the QML is given)
    void parse(const QString &text);
    void loadSaved();
    void queue(const QString &kind, const QString &value);
    void writeNext();
    void write(const QStringList &pathsAndValues, std::function<void(const QString &)> done, bool frame = false);
    void sendColor(const QString &value, bool frame = false);
    void writeFailed(const QString &why);
    void scheduleRecover();
    void tryRecover();
    void restoreSavedEffect();
    void tick();
    void afterWake();
    void permDone(int code, const QByteArray &out, const QByteArray &err);
    void updateTimers();
    QString colorPath(const QString &zone) const;
    QString dir() const { return m_zones.isEmpty() ? QString() : m_zones[0]; }
    void readFiles(const QStringList &files, std::function<void(const QStringList &, const QList<bool> &)> done);

    Paths m_paths;
    Settings &m_settings;
    const I18n &m_i18n;
    const bool m_control;
    CaelestiaScheme m_scheme;

    QThread m_ioThread;
    KbdIo *m_io = nullptr;
    quint64 m_tag = 0;
    QHash<quint64, std::function<void(const QString &)>> m_scans, m_writes;
    QHash<quint64, std::function<void(const QStringList &, const QList<bool> &)>> m_reads;

    // found
    const KeyboardPlugin *m_plugin = nullptr;
    QStringList m_zones;
    QString m_name, m_driver, m_kernel, m_ruleAt, m_error;
    QStringList m_dmi{ QString(), QString(), QString(), QString() }, m_notes;
    bool m_writable = false, m_ruleLegacy = false, m_ruleOld = false, m_ready = false, m_permBusy = false;
    int m_maxBrightness = 255, m_brightness = 0;
    KbdFormat m_format;
    QColor m_color{ Qt::white };

    // effect settings
    QString m_effect = QStringLiteral("static"), m_source = QStringLiteral("single");
    QStringList m_colors{ QStringLiteral("#ff0000"), QStringLiteral("#00ff00"), QStringLiteral("#0040ff") };
    double m_speed = 0.5, m_minLevel = 0, m_maxLevel = 1;
    bool m_paused = false;
    int m_cycleIndex = 0, m_themeCount = 3;

    // reads (K-18)
    QList<std::function<void()>> m_readWaiting, m_readCurrent;
    bool m_reading = false, m_readAgain = false;
    // writes (K-22)
    std::optional<QString> m_pendingColor, m_pendingBrightness;
    bool m_frameInFlight = false;
    qint64 m_brightAt = 0;
    bool m_brightReading = false, m_wakeReading = false;
    // recovery (K-26)
    bool m_lostEffect = false, m_lightMissing = false;
    int m_recoverTries = 0;
    // effects (K-31)
    double m_t0 = 0;                                      // (a double, as the QML property: setSpeed keeps the phase)
    QString m_lastFrame;
    qint64 m_lastAwake = 0;
    bool m_quitting = false;

    QTimer m_flush, m_recover, m_healthy, m_effectTimer, m_saveSpeed, m_saveRange, m_wake;
    Cmd m_perm;
};
