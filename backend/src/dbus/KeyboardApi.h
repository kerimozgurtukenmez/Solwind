#pragma once

#include <QDBusContext>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

class I18n;
class KeyboardController;

// D-Bus: /solwind/Keyboard, interface solwind.Keyboard - the keyboard light: what was found, its colour, brightness,
// effect and permission. Changes come as PropertiesChanged. The methods check what the terminal commands checked
// (shell.qml kbd*, SPEC.md §20) with the same messages, and answer the same texts ("Color: #FF0000", ...).
// Errors: solwind.Error.Usage (a wrong argument), solwind.Error.Failed (refused now: no light, not RGB, no permission,
// effects not available, quitting), solwind.Error.Observe (the daemon only reads). Messages in the UI language.
class KeyboardApi : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "solwind.Keyboard")
    Q_PROPERTY(bool Available READ available)
    Q_PROPERTY(bool Ready READ ready)                        // the first read is done
    Q_PROPERTY(QString Backend READ backend)                 // led-rgb | hp-zoned | hp-rgbzones | asus-tuf | led-mono | ""
    Q_PROPERTY(QString Name READ name)
    Q_PROPERTY(QString Driver READ driver)
    Q_PROPERTY(int Zones READ zones)
    Q_PROPERTY(QVariantMap Dmi READ dmi)                     // vendor, product, family, board
    Q_PROPERTY(QString Kernel READ kernel)
    Q_PROPERTY(QStringList Notes READ notes)                 // legion-usb | rog-usb | hp-none
    Q_PROPERTY(bool Rgb READ rgb)
    Q_PROPERTY(bool HasBrightness READ hasBrightness)
    Q_PROPERTY(bool EffectsAllowed READ effectsAllowed)      // software effects (only where tested)
    Q_PROPERTY(bool Tested READ tested)
    Q_PROPERTY(bool Writable READ writable)                  // write permission
    Q_PROPERTY(QString RuleAt READ ruleAt)                   // the permission rule: etc | lib | ""
    Q_PROPERTY(bool RuleLegacy READ ruleLegacy)              // an older, less strict rule is installed
    Q_PROPERTY(bool RuleOld READ ruleOld)                    // the rule has the old name (Ccenter's)
    Q_PROPERTY(bool PermBusy READ permBusy)                  // granting / removing the permission
    Q_PROPERTY(QString Error READ error)
    Q_PROPERTY(int MaxBrightness READ maxBrightness)
    Q_PROPERTY(int Brightness READ brightness)
    Q_PROPERTY(QString Color READ color)                     // the chosen colour, "#RRGGBB"
    Q_PROPERTY(QString Effect READ effect)                   // static | breathing | cycle
    Q_PROPERTY(QString Source READ source)                   // single | multi | theme
    Q_PROPERTY(QStringList Colors READ colors)               // the colour list ("#rrggbb", 1..6)
    Q_PROPERTY(double Speed READ speed)                      // 0..1
    Q_PROPERTY(double Period READ period)                    // seconds per breath / colour
    Q_PROPERTY(double MinLevel READ minLevel)
    Q_PROPERTY(double MaxLevel READ maxLevel)
    Q_PROPERTY(int ThemeCount READ themeCount)
    Q_PROPERTY(QStringList ThemeColors READ themeColors)     // Caelestia's colours as the effect uses them
    Q_PROPERTY(bool EffectActive READ effectActive)
    Q_PROPERTY(bool Paused READ paused)
    Q_PROPERTY(int CycleIndex READ cycleIndex)               // the colour the effect shows now (the UI highlights it)

public:
    static constexpr const char *path = "/solwind/Keyboard";
    KeyboardApi(KeyboardController &kbd, const I18n &i18n, QObject *parent = nullptr);

    bool available() const;
    bool ready() const;
    QString backend() const;
    QString name() const;
    QString driver() const;
    int zones() const;
    QVariantMap dmi() const;
    QString kernel() const;
    QStringList notes() const;
    bool rgb() const;
    bool hasBrightness() const;
    bool effectsAllowed() const;
    bool tested() const;
    bool writable() const;
    QString ruleAt() const;
    bool ruleLegacy() const;
    bool ruleOld() const;
    bool permBusy() const;
    QString error() const;
    int maxBrightness() const;
    int brightness() const;
    QString color() const;
    QString effect() const;
    QString source() const;
    QStringList colors() const;
    double speed() const;
    double period() const;
    double minLevel() const;
    double maxLevel() const;
    int themeCount() const;
    QStringList themeColors() const;
    bool effectActive() const;
    bool paused() const;
    int cycleIndex() const;

public slots:
    Q_SCRIPTABLE void Refresh();                             // detection again (the Keyboard page opened, ...)
    Q_SCRIPTABLE QString StatusText() const;                 // `solwind kbd`
    Q_SCRIPTABLE QString InfoText() const;                   // `solwind kbd info`
    Q_SCRIPTABLE QString SetColor(const QString &hex);       // "#rrggbb" or "rrggbb"
    Q_SCRIPTABLE QString SetBrightness(int percent);         // 0..100 (clamped)
    Q_SCRIPTABLE QString SetEffect(const QString &name);     // static | breathing | cycle
    Q_SCRIPTABLE QString SetSource(const QString &name);     // single | multi | theme
    Q_SCRIPTABLE QString SetSpeed(int percent);              // 0..100 (clamped)
    Q_SCRIPTABLE QString SetThemeCount(int n);               // 3..6 (clamped)
    Q_SCRIPTABLE QString SetRange(int low, int high);        // breathing levels, percent
    Q_SCRIPTABLE void SetColors(const QStringList &colors);  // 1..6 colours "#rrggbb"
    Q_SCRIPTABLE void GrantPermission();                     // the admin password dialog (pkexec)
    Q_SCRIPTABLE void RevokePermission();

private:
    bool changeRefused();
    QString usage(const QString &msg);
    QString failed(const QString &msg);

    KeyboardController &m_kbd;
    const I18n &m_i18n;
};
