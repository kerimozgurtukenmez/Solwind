#include "KeyboardApi.h"

#include "core/I18n.h"
#include "core/JsCompat.h"
#include "core/KeyboardController.h"

#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace {
const QString errUsage = u"solwind.Error.Usage"_s;
const QString errFailed = u"solwind.Error.Failed"_s;
const QString errObserve = u"solwind.Error.Observe"_s;
QStringList hexes(const QList<QColor> &l)
{
    QStringList out;
    for (const QColor &c : l) out << Qml::hex(c);
    return out;
}
} // namespace

KeyboardApi::KeyboardApi(KeyboardController &kbd, const I18n &i18n, QObject *parent) : QObject(parent), m_kbd(kbd), m_i18n(i18n) {}

bool KeyboardApi::available() const { return m_kbd.available(); }
bool KeyboardApi::ready() const { return m_kbd.ready(); }
QString KeyboardApi::backend() const { return m_kbd.backend(); }
QString KeyboardApi::name() const { return m_kbd.name(); }
QString KeyboardApi::driver() const { return m_kbd.driver(); }
int KeyboardApi::zones() const { return int(m_kbd.zones().size()); }
QVariantMap KeyboardApi::dmi() const
{
    const QStringList d = m_kbd.dmi();
    return { { u"vendor"_s, d.value(0) }, { u"product"_s, d.value(1) }, { u"family"_s, d.value(2) }, { u"board"_s, d.value(3) } };
}
QString KeyboardApi::kernel() const { return m_kbd.kernel(); }
QStringList KeyboardApi::notes() const { return m_kbd.notes(); }
bool KeyboardApi::rgb() const { return m_kbd.rgb(); }
bool KeyboardApi::hasBrightness() const { return m_kbd.hasBrightness(); }
bool KeyboardApi::effectsAllowed() const { return m_kbd.effectsAllowed(); }
bool KeyboardApi::tested() const { return m_kbd.tested(); }
bool KeyboardApi::writable() const { return m_kbd.writable(); }
QString KeyboardApi::ruleAt() const { return m_kbd.ruleAt(); }
bool KeyboardApi::ruleLegacy() const { return m_kbd.ruleLegacy(); }
bool KeyboardApi::ruleOld() const { return m_kbd.ruleOld(); }
bool KeyboardApi::permBusy() const { return m_kbd.permBusy(); }
QString KeyboardApi::error() const { return m_kbd.error(); }
int KeyboardApi::maxBrightness() const { return m_kbd.maxBrightness(); }
int KeyboardApi::brightness() const { return m_kbd.brightness(); }
QString KeyboardApi::color() const { return Qml::hex(m_kbd.color()); }
DoubleList KeyboardApi::colorF() const
{
    const QColor c = m_kbd.color();
    return { double(c.redF()), double(c.greenF()), double(c.blueF()) };
}
QString KeyboardApi::effect() const { return m_kbd.effect(); }
QString KeyboardApi::source() const { return m_kbd.source(); }
QStringList KeyboardApi::colors() const { return m_kbd.colors(); }
double KeyboardApi::speed() const { return m_kbd.speed(); }
double KeyboardApi::period() const { return m_kbd.period(); }
double KeyboardApi::minLevel() const { return m_kbd.minLevel(); }
double KeyboardApi::maxLevel() const { return m_kbd.maxLevel(); }
int KeyboardApi::themeCount() const { return m_kbd.themeCount(); }
QStringList KeyboardApi::themeColors() const { return hexes(m_kbd.themeColors()); }
bool KeyboardApi::effectActive() const { return m_kbd.effectActive(); }
bool KeyboardApi::paused() const { return m_kbd.paused(); }
int KeyboardApi::cycleIndex() const { return m_kbd.cycleIndex(); }
QString KeyboardApi::kindText() const { return m_kbd.kindText(); }
QString KeyboardApi::supportText() const { return m_kbd.supportText(); }
QString KeyboardApi::noPermissionText() const { return m_kbd.noPermissionText(); }
QStringList KeyboardApi::noteTexts() const
{
    QStringList out;
    for (const QString &n : m_kbd.notes()) out << m_kbd.noteText(n);
    return out;
}

// Changes are refused in observe mode (the QML app next to it drives the light), and once quitting has begun
bool KeyboardApi::changeRefused()
{
    if (!m_kbd.control()) {
        if (calledFromDBus()) sendErrorReply(errObserve, m_i18n.t(u"Solwind only watches the keyboard light now"_s));
        return true;
    }
    if (m_kbd.quitting()) {
        if (calledFromDBus()) sendErrorReply(errFailed, m_i18n.t(u"Solwind is quitting"_s));
        return true;
    }
    return false;
}
QString KeyboardApi::usage(const QString &msg) { if (calledFromDBus()) sendErrorReply(errUsage, msg); return QString(); }
QString KeyboardApi::failed(const QString &msg) { if (calledFromDBus()) sendErrorReply(errFailed, msg); return QString(); }

void KeyboardApi::Refresh() { m_kbd.refresh(); }
QString KeyboardApi::StatusText() const { return m_kbd.statusText(); }
QString KeyboardApi::InfoText() const { return m_kbd.infoText(); }
QString KeyboardApi::DetectionText() const { return m_kbd.detectionText(); }

QString KeyboardApi::SetColor(const QString &hex)
{
    if (changeRefused()) return QString();
    if (!m_kbd.available()) return failed(m_i18n.t(u"No keyboard light found"_s));
    if (!m_kbd.rgb()) return failed(m_i18n.t(u"This keyboard light is not RGB"_s));
    static const QRegularExpression re(u"\\A#?[0-9a-fA-F]{6}\\z"_s);
    if (!re.match(hex).hasMatch()) return usage(m_i18n.t(u"The color must look like #rrggbb (e.g. #ff0000)"_s));
    if (!m_kbd.writable()) { m_kbd.refresh(); return failed(m_kbd.noPermissionText()); }   // (it may have been granted meanwhile)
    m_kbd.setColor(QColor::fromString(hex.startsWith(u'#') ? hex : u"#"_s + hex));
    return m_i18n.t(u"Color: %1"_s).arg(Qml::hex(m_kbd.color()));
}

QString KeyboardApi::SetEffect(const QString &name)
{
    if (changeRefused()) return QString();
    if (!QStringList{ u"static"_s, u"breathing"_s, u"cycle"_s }.contains(name))
        return usage(m_i18n.t(u"Effect: %1"_s).arg(u"static | breathing | cycle"_s));
    if (!m_kbd.effectsAllowed()) return failed(m_i18n.t(u"Effects are not available for this keyboard"_s));
    if (!m_kbd.writable()) { m_kbd.refresh(); return failed(m_kbd.noPermissionText()); }
    m_kbd.setEffect(name);
    return m_i18n.t(u"Effect: %1"_s).arg(name);
}

QString KeyboardApi::SetSource(const QString &name)
{
    if (changeRefused()) return QString();
    if (!QStringList{ u"single"_s, u"multi"_s, u"theme"_s }.contains(name))
        return usage(m_i18n.t(u"Color source: single | multi | theme"_s));
    if (!m_kbd.setSource(name)) return failed(m_i18n.t(u"A color cycle needs several colors: multi | theme"_s));
    return m_i18n.t(u"Color source: %1"_s).arg(name);
}

QString KeyboardApi::SetSpeed(int percent)
{
    if (changeRefused()) return QString();
    if (!m_kbd.effectsAllowed()) return failed(m_i18n.t(u"Effects are not available for this keyboard"_s));
    m_kbd.setSpeed(qBound(0, percent, 100) / 100.0);
    return m_i18n.t(u"Speed: %1% (%2 s per breath / color)"_s)
        .arg(Qml::number(js::round(m_kbd.speed() * 100)), m_i18n.num(m_kbd.period(), 1));
}

QString KeyboardApi::SetThemeCount(int n)
{
    if (changeRefused()) return QString();
    if (!m_kbd.effectsAllowed()) return failed(m_i18n.t(u"Effects are not available for this keyboard"_s));
    m_kbd.setThemeCount(n);
    return m_i18n.t(u"Caelestia colors: %1 (%2)"_s).arg(QString::number(m_kbd.themeCount()), hexes(m_kbd.themeColors()).join(u' '));
}

QString KeyboardApi::SetRange(int low, int high)
{
    if (changeRefused()) return QString();
    if (!m_kbd.effectsAllowed()) return failed(m_i18n.t(u"Effects are not available for this keyboard"_s));
    m_kbd.setRange(low / 100.0, high / 100.0);
    return m_i18n.t(u"Breathing range: %1% – %2%"_s)
        .arg(Qml::number(js::round(m_kbd.minLevel() * 100)), Qml::number(js::round(m_kbd.maxLevel() * 100)));
}

QString KeyboardApi::SetBrightness(int percent)
{
    if (changeRefused()) return QString();
    if (!m_kbd.available()) return failed(m_i18n.t(u"No keyboard light found"_s));
    if (!m_kbd.hasBrightness()) return failed(m_i18n.t(u"This keyboard light has no brightness setting (color only)"_s));
    if (!m_kbd.writable()) { m_kbd.refresh(); return failed(m_kbd.noPermissionText()); }
    const int p = qBound(0, percent, 100);
    m_kbd.setBrightness(double(p) * m_kbd.maxBrightness() / 100);
    return m_i18n.t(u"Brightness: %1%"_s).arg(p);
}

// The colour list (the UI's colour editor): 1..6 colours "#rrggbb" (the QML took any list; a wrong colour here would
// break the effect, so it is checked)
void KeyboardApi::SetColors(const QStringList &colors)
{
    if (changeRefused()) return;
    static const QRegularExpression re(u"\\A#[0-9a-fA-F]{6}\\z"_s);
    if (colors.isEmpty() || colors.size() > 6) return void(usage(u"1 to 6 colors"_s));
    for (const QString &c : colors) if (!re.match(c).hasMatch()) return void(usage(u"not a color #rrggbb: "_s + c));
    m_kbd.setColors(colors);
}

// (NaN and infinities can come over D-Bus; the UI never sends them)
void KeyboardApi::SetColorF(double r, double g, double b)
{
    if (changeRefused()) return;
    if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b)) return void(usage(u"not a number"_s));
    m_kbd.setColor(QColor::fromRgbF(float(qBound(0.0, r, 1.0)), float(qBound(0.0, g, 1.0)), float(qBound(0.0, b, 1.0))));
}

void KeyboardApi::SetBrightnessRaw(double level)
{
    if (changeRefused()) return;
    if (!std::isfinite(level)) return void(usage(u"not a number"_s));
    m_kbd.setBrightness(level);
}

void KeyboardApi::SetSpeedF(double v)
{
    if (changeRefused()) return;
    if (!std::isfinite(v)) return void(usage(u"not a number"_s));
    m_kbd.setSpeed(v);
}

void KeyboardApi::SetRangeF(double low, double high)
{
    if (changeRefused()) return;
    if (!std::isfinite(low) || !std::isfinite(high)) return void(usage(u"not a number"_s));
    m_kbd.setRange(low, high);
}

void KeyboardApi::GrantPermission() { if (!changeRefused()) m_kbd.grantPermission(); }
void KeyboardApi::RevokePermission() { if (!changeRefused()) m_kbd.revokePermission(); }
