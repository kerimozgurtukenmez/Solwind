#pragma once

#include <QColor>
#include <QHash>
#include <QList>
#include <QSet>
#include <QStringList>
#include <QString>

#include <optional>

// One keyboard light (or HP rgb_zones file) as detection reports it (SPEC.md K-2, K-3)
struct KbdLight {
    QString name, driver, path;
    QHash<QString, QString> v;        // readable values by file ("color" for an rgb_zones file)
    QSet<QString> w, x;               // writable / existing files
};

// What detection found, sorted out once for every plugin (SPEC.md K-6, K-7)
struct KbdCandidates {
    QList<KbdLight> leds;             // LED class lights, in detection order
    QList<KbdLight> rgbZones;         // hp-wmi rgb_zones files
    QList<KbdLight> zoned;            // one keyboard of several zone lights (named, or numbered of one driver)
    std::optional<KbdLight> single;   // the single light, if any
    static bool isRgb(const KbdLight &l);   // multi_intensity, and multi_index is red, green, blue in any order (K-6)
    static bool isHex(const KbdLight &l);   // an rgb_zones value RRGGBB
};

// How a colour is written: per hardware channel, which colour it shows and its top value (K-13)
struct KbdFormat {
    QList<double> maxIntensity{ 255, 255, 255 };
    QList<int> channels{ 0, 1, 2 };   // hardware channel k shows colour part channels[k] (0 red, 1 green, 2 blue)
};

// A kind of keyboard light, per kernel interface (ARCH-PLAN.md "Plugin contracts", rule 3): which lights it takes,
// whether they can be written, and how a colour is written to them. It never writes itself: the core does every
// write (KeyboardController), and only software effects depend on a plugin being tested (effectsTested).
class KeyboardPlugin
{
public:
    // When the saved colour is written again at start-up (K-15, K-16)
    enum class AtStart { Never, WhenStaticAndOn, Always };

    virtual ~KeyboardPlugin() = default;
    virtual QString id() const = 0;           // the kind of light (Keyboard.Backend): led-rgb, hp-zoned, hp-rgbzones ...
    virtual bool rgb() const = 0;
    virtual bool hasBrightness() const { return true; }
    // Software effects write many times a second: only on the combination they were tested with (K-12)
    virtual bool effectsTested(const QString &driver) const { Q_UNUSED(driver) return false; }
    virtual bool tested(const QString &driver) const { Q_UNUSED(driver) return false; }
    // The lights it drives, empty if it doesn't take this keyboard (asked in a fixed order, K-8; read only)
    virtual QList<KbdLight> match(const KbdCandidates &c) const = 0;
    virtual bool writable(const QList<KbdLight> &lights) const = 0;            // K-10
    virtual KbdFormat format(const KbdLight &) const { return {}; }
    // The colour the light shows, if it can be read back (K-14)
    virtual std::optional<QColor> shownColor(const KbdLight &, const KbdFormat &) const { return std::nullopt; }
    virtual AtStart atStart() const { return AtStart::Never; }
    // Where a colour goes for one zone (a light's folder, or an rgb_zones file), and the value written (K-20)
    virtual QString colorPath(const QString &zone) const { return zone + QStringLiteral("/multi_intensity"); }
    virtual QString colorValue(const QColor &c, double level, const KbdFormat &f) const;
};

// QML's colour helpers, to the bit (qtdeclarative qqmlbuiltinfunctions.cpp / qquickvaluetypes.cpp, Qt 6.10 and dev):
// Qt.rgba clamps with qBound, Qt.lighter(c, f) is QColor::lighter(qRound(f * 100)), "#rrggbb" -> QColor::fromString
namespace Qml {
QColor rgba(double r, double g, double b, double a = 1);
QColor hsva(double h, double s, double v, double a = 1);
QColor lighter(const QColor &c, double factor = 1);
QString hex(const QColor &c);                  // Keyboard.qml hex(): "#RRGGBB"
QString number(double v);                      // a JS number as String() writes it
}

// The keyboard plugins' shared pieces (the multicolor LED class: brightness + multi_intensity)
namespace kbd {
QStringList words(const QString &s);           // s.trim().split(/\s+/)
// colour x level per hardware channel, clamped and rounded (Keyboard.qml intensity(), K-20)
QString intensity(const QColor &c, double level, const KbdFormat &f);
KbdFormat multiFormat(const KbdLight &first);  // channel order and top values (K-13)
std::optional<QColor> multiColor(const KbdLight &first, const KbdFormat &f);   // the colour it shows (K-14)
bool every(const QList<KbdLight> &l, bool (*ok)(const KbdLight &));            // all, and at least one
}
