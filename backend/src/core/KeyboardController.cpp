#include "KeyboardController.h"

#include "I18n.h"
#include "JsCompat.h"
#include "Settings.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

using namespace Qt::StringLiterals;

KeyboardController::KeyboardController(const Paths &paths, Settings &settings, const I18n &i18n, bool control, QObject *parent)
    : QObject(parent), m_paths(paths), m_settings(settings), m_i18n(i18n), m_control(control),
      m_scheme(paths.caelestiaScheme())
{
    now = [] { return QDateTime::currentMSecsSinceEpoch(); };
    m_io = new KbdIo(paths);
    m_io->moveToThread(&m_ioThread);
    connect(&m_ioThread, &QThread::finished, m_io, &QObject::deleteLater);
    connect(m_io, &KbdIo::scanned, this, [this](quint64 tag, const QString &text) {
        if (auto f = m_scans.take(tag)) f(text);
    });
    connect(m_io, &KbdIo::written, this, [this](quint64 tag, const QString &err) {
        if (auto f = m_writes.take(tag)) f(err);
    });
    connect(m_io, &KbdIo::readDone, this, [this](quint64 tag, const QStringList &c, const QList<bool> &ok) {
        if (auto f = m_reads.take(tag)) f(c, ok);
    });
    m_ioThread.setObjectName(u"keyboard"_s);
    m_ioThread.start();

    m_flush.setSingleShot(true);
    connect(&m_flush, &QTimer::timeout, this, &KeyboardController::writeNext);
    m_recover.setSingleShot(true);
    connect(&m_recover, &QTimer::timeout, this, [this] { refresh([this] { tryRecover(); }); });
    m_healthy.setSingleShot(true);
    m_healthy.setInterval(60000);
    connect(&m_healthy, &QTimer::timeout, this, [this] { m_recoverTries = 0; });
    connect(&m_effectTimer, &QTimer::timeout, this, &KeyboardController::tick);
    m_saveSpeed.setSingleShot(true);
    m_saveSpeed.setInterval(500);
    connect(&m_saveSpeed, &QTimer::timeout, this, [this] { m_settings.setKbd({ { u"speed"_s, m_speed } }); });
    m_saveRange.setSingleShot(true);
    m_saveRange.setInterval(500);
    connect(&m_saveRange, &QTimer::timeout, this, [this] { m_settings.setKbd({ { u"min"_s, m_minLevel }, { u"max"_s, m_maxLevel } }); });
    m_wake.setInterval(5000);
    connect(&m_wake, &QTimer::timeout, this, [this] {
        const qint64 t = now();
        const qint64 gap = t - m_lastAwake;
        m_lastAwake = t;
        if (gap > wakeGap) afterWake();
    });
    connect(&m_scheme, &CaelestiaScheme::changed, this, &KeyboardController::changed);
}

KeyboardController::~KeyboardController()
{
    m_ioThread.quit();
    m_ioThread.wait();
}

void KeyboardController::start()
{
    m_lastAwake = now();
    m_t0 = now();
    refresh();
}

// ---------- reading (K-18) ----------

// One read at a time. A request during a read gets a fresh read right after it (its callback runs after that one),
// so no callback is lost and none gets a state read before it asked
void KeyboardController::refresh(std::function<void()> done)
{
    if (done) m_readWaiting << done;
    if (m_reading) { m_readAgain = true; return; }
    m_readCurrent = m_readWaiting;
    m_readWaiting.clear();
    m_readAgain = false;
    m_reading = true;
    const quint64 tag = ++m_tag;
    m_scans.insert(tag, [this](const QString &text) {
        m_reading = false;                                // (Cmd: not busy while its callback runs)
        parse(text);
        const auto cbs = m_readCurrent;
        m_readCurrent.clear();
        for (const auto &f : cbs) f();
        if (m_readAgain) refresh();
        emit changed();
    });
    QMetaObject::invokeMethod(m_io, [io = m_io, tag] { io->scan(tag); }, Qt::QueuedConnection);
}

void KeyboardController::parse(const QString &text)
{
    m_lastScan = text;
    const KbdFound f = KeyboardParse::parse(text, m_dmi[0]);
    if (f.dmi) m_dmi = *f.dmi;
    if (f.kernel) m_kernel = *f.kernel;
    const bool firstRead = !m_ready;
    if (!m_ready) loadSaved();                            // the saved effect settings, on the first read (K-19)
    m_ruleAt = f.ruleAt;
    m_ruleLegacy = f.ruleLegacy;
    m_ruleOld = f.ruleOld;

    m_plugin = f.plugin;
    m_zones = f.zones;
    m_name = f.name;
    m_driver = f.driver;
    m_writable = f.writable;
    m_maxBrightness = f.maxBrightness;
    m_brightness = f.brightness;
    m_brightAt = now();
    // The light shows the chosen colour unless an effect is chosen: while one is selected (also when a write error
    // stopped it or there's no permission) it may still show one of the effect's frames; the chosen colour is kept
    // then. An effect saved on a light that has no effects (settings from another keyboard) doesn't count (K-14).
    const KbdSettings saved = m_settings.kbd();
    const bool fromHw = !effectsAllowed() || (m_effect == u"static"_s && saved.effect == u"static"_s);
    // At start-up a static light gets the saved colour again instead of being read back: the module writes colour x
    // brightness to the hardware and reads that scaled value back as the colour after a reboot, so it would darken
    // a little more on every reboot. Not while it's off (Fn): writing a colour must not turn it on (K-15).
    const bool applySaved = firstRead && fromHw && !saved.color.isEmpty() && m_brightness > 0 && m_plugin
                         && m_plugin->atStart() == KeyboardPlugin::AtStart::WhenStaticAndOn;
    m_format = f.format;
    if (fromHw && !applySaved && f.shownColor) m_color = *f.shownColor;
    m_notes = f.notes;

    // K-16: a colour that can't be read back (ASUS TUF) is applied once at start; K-15 above
    if (!m_ready && m_plugin && m_plugin->atStart() == KeyboardPlugin::AtStart::Always && m_writable && !saved.color.isEmpty())
        queue(u"color"_s, colorValue(m_color, 1));
    if (applySaved && m_writable) queue(u"color"_s, colorValue(m_color, 1));
    m_ready = true;
    updateTimers();
}

void KeyboardController::loadSaved()
{
    const KbdSettings k = m_settings.kbd();
    // Older versions could save a colour cycle on the single chosen colour (it can't cycle): the colour list then
    m_source = k.effect == u"cycle"_s && k.source == u"single"_s ? u"multi"_s : k.source;
    m_colors = k.colors;
    m_speed = k.speed;
    m_themeCount = k.themeCount;
    m_minLevel = k.min;
    m_maxLevel = k.max;
    if (!k.color.isEmpty()) m_color = QColor::fromString(k.color);
    m_effect = k.effect;
    m_t0 = now();
}

void KeyboardController::readFiles(const QStringList &files, std::function<void(const QStringList &, const QList<bool> &)> done)
{
    const quint64 tag = ++m_tag;
    m_reads.insert(tag, std::move(done));
    QMetaObject::invokeMethod(m_io, [io = m_io, files, tag] { io->read(files, tag); }, Qt::QueuedConnection);
}

// ---------- writing (K-20..K-25) ----------

QString KeyboardController::colorValue(const QColor &c, double level) const
{
    if (m_plugin) return m_plugin->colorValue(c, level, m_format);
    return KeyboardPlugin::all().last()->colorValue(c, level, m_format);   // (none found: the plain form, never written)
}

QString KeyboardController::colorPath(const QString &zone) const
{
    return m_plugin ? m_plugin->colorPath(zone) : zone + u"/multi_intensity"_s;
}

void KeyboardController::setBrightness(double b)
{
    if (!hasBrightness()) return;
    if (!m_writable) { refresh(); return; }               // no permission: nothing changes; it may have been granted meanwhile
    const double v = js::max(0, js::min(m_maxBrightness, js::round(b)));
    m_brightness = std::isfinite(v) ? int(v) : 0;         // (callers give numbers; never int(NaN))
    queue(u"brightness"_s, QString::number(m_brightness));
    emit changed();
}

void KeyboardController::setColor(const QColor &in)
{
    if (!rgb()) return;
    if (!m_writable) { refresh(); return; }
    const QColor c = Qml::lighter(in, 1);
    m_color = Qml::rgba(c.redF(), c.greenF(), c.blueF(), 1);
    m_settings.setKbd({ { u"color"_s, Qml::hex(m_color) } });
    if (!effectActive()) queue(u"color"_s, colorValue(m_color, 1));   // with an effect running, the next frame applies it
    emit changed();
    // Picking a colour at brightness 0 shows nothing: turn the light on so the user sees it. The key that turns the
    // light off (Fn) changes the brightness behind Solwind's back, so a value older than a second is read again.
    if (!hasBrightness()) return;
    const qint64 age = now() - m_brightAt;
    if (m_brightness == 0) setBrightness(m_maxBrightness);
    else if ((age < 0 || age > 1000) && !m_brightReading && !m_pendingBrightness) {
        m_brightAt = now();
        m_brightReading = true;
        readFiles({ dir() + u"/brightness"_s }, [this](const QStringList &c, const QList<bool> &ok) {
            m_brightReading = false;
            const double b = js::parseInt(c.value(0));
            // (a brightness the user set meanwhile wins over what was read before it was written)
            if (!ok.value(0) || !std::isfinite(b) || !hasBrightness() || m_pendingBrightness) return;
            m_brightness = int(b);
            if (b == 0) setBrightness(m_maxBrightness);
            emit changed();
        });
    }
}

// Changes show at once; the files are written at most every 80 ms (one zone) / 250 ms (more), colour first
void KeyboardController::queue(const QString &kind, const QString &value)
{
    if (!available() || m_quitting) return;
    if (kind == u"color"_s) m_pendingColor = value;
    else m_pendingBrightness = value;
    if (!m_flush.isActive()) {
        m_flush.setInterval(m_zones.size() > 1 ? 250 : 80);
        m_flush.start();
    }
}

void KeyboardController::writeNext()
{
    // Colour first, then brightness (so the light never shows the new colour at the old brightness)
    QStringList pv;
    if (m_pendingColor) for (const QString &z : m_zones) pv << colorPath(z) << *m_pendingColor;
    if (m_pendingBrightness && hasBrightness()) for (const QString &z : m_zones) pv << z + u"/brightness"_s << *m_pendingBrightness;
    m_pendingColor.reset();
    m_pendingBrightness.reset();
    if (!pv.isEmpty()) write(pv, nullptr);
}

void KeyboardController::write(const QStringList &pv, std::function<void(const QString &)> done, bool frame)
{
    if (!m_control) { if (done) done(QString()); return; }   // observe mode: never written
    const quint64 tag = ++m_tag;
    if (frame) m_frameInFlight = true;
    m_writes.insert(tag, [this, frame, done, pv](const QString &err) {
        if (frame) m_frameInFlight = false;
        emit wrote(pv, err);
        if (!err.isEmpty()) writeFailed(err);
        if (done) done(err);
    });
    QMetaObject::invokeMethod(m_io, [io = m_io, pv, tag] { io->write(pv, tag); }, Qt::QueuedConnection);
}

void KeyboardController::sendColor(const QString &value, bool frame)
{
    QStringList pv;
    for (const QString &z : m_zones) pv << colorPath(z) << value;
    if (!pv.isEmpty()) write(pv, nullptr, frame);
}

void KeyboardController::writeFailed(const QString &why)
{
    m_error = m_i18n.t(u"Could not write to the keyboard light"_s) + (why.isEmpty() ? QString() : u": "_s + why);
    if (m_effect != u"static"_s) m_lostEffect = true;
    m_effect = u"static"_s;                               // the effect stops (not saved to settings)
    m_healthy.stop();
    updateTimers();
    emit changed();
    refresh([this] { scheduleRecover(); });               // the real value and permission, read again
}

// ---------- recovery (K-26) ----------
// After a write error (the module was reloaded, the light went away, permission changed) the light is looked for
// again: after 2, 4, 8, 16, 32 s, then every minute while it is missing. Once it's there and writable, a stopped
// effect comes back, or the chosen colour is written again; the error goes once that works. A light that is there but
// keeps failing is left alone after 5 tries (until a permission action); a minute without errors after a recovery
// resets the count, and so does a light that comes back after being missing.

void KeyboardController::scheduleRecover()
{
    if (m_quitting) return;
    if (m_recoverTries >= 5 && available()) return;
    m_recover.setInterval(int(std::min(60000.0, 2000 * std::pow(2, m_recoverTries))));
    m_recoverTries++;
    m_recover.start();
}

void KeyboardController::tryRecover()
{
    if (!available()) { m_lightMissing = true; scheduleRecover(); return; }
    if (m_lightMissing) {
        m_lightMissing = false;
        m_recoverTries = 0;
        // It has just appeared: udev may not have handed it to the user yet, so look once more before giving up
        if (!m_writable) { scheduleRecover(); return; }
    }
    if (!m_writable) return;                              // no permission: granting it brings the effect back (permDone)
    m_error.clear();
    const KbdSettings saved = m_settings.kbd();
    if (m_lostEffect) {
        m_lostEffect = false;
        restoreSavedEffect();
    } else if (rgb() && !saved.color.isEmpty()) {
        // A static write failed: write the chosen colour again (if it fails again, the error comes back)
        m_color = QColor::fromString(saved.color);
        queue(u"color"_s, colorValue(m_color, 1));
    }
    m_healthy.start();
    updateTimers();
    emit changed();
}

// ---------- effects (K-27..K-32) ----------

void KeyboardController::setEffect(const QString &e)
{
    if (e == m_effect && !m_lostEffect) return;
    m_lostEffect = false;                                 // the user chose (also "static" after an error stopped the effect)
    if (e == u"cycle"_s && m_source == u"single"_s) setSource(u"multi"_s);   // a cycle needs more than one colour
    m_effect = e;
    m_settings.setKbd({ { u"effect"_s, e } });
    if (e == u"static"_s) queue(u"color"_s, colorValue(m_color, 1));   // effect off: back to the chosen colour
    else { m_t0 = now(); m_lastFrame.clear(); }
    updateTimers();
    emit changed();
}

bool KeyboardController::setSource(const QString &s)
{
    if (s == u"single"_s && m_effect == u"cycle"_s) return false;
    m_source = s;
    m_settings.setKbd({ { u"source"_s, s } });
    m_lastFrame.clear();
    emit changed();
    return true;
}

// The saved effect back (after a write error or a permission change); a cycle can't run on a single colour
void KeyboardController::restoreSavedEffect()
{
    const QString e = m_settings.kbd().effect;
    if (e == u"cycle"_s && m_source == u"single"_s) setSource(u"multi"_s);
    m_effect = e;
    m_lastFrame.clear();
    updateTimers();
}

void KeyboardController::setThemeCount(double n)
{
    m_themeCount = int(js::max(3, js::min(6, js::round(n))));
    m_settings.setKbd({ { u"themeCount"_s, m_themeCount } });
    emit changed();
}

void KeyboardController::setColors(const QStringList &list)
{
    m_colors = list.mid(0, 6);
    m_settings.setKbd({ { u"colors"_s, QJsonArray::fromStringList(m_colors) } });
    emit changed();
}

void KeyboardController::setSpeed(double v)
{
    // When the speed changes, continue from the same point of the breath (keep the phase)
    const qint64 t = now();
    const double p = ((t - m_t0) / 1000) / period();
    m_speed = js::max(0, js::min(1, v));
    m_t0 = t - p * period() * 1000;
    m_saveSpeed.start();
    updateTimers();
    emit changed();
}

// Lowest/highest level; if one passes the other, it pushes it along
void KeyboardController::setRange(double lo, double hi)
{
    lo = js::max(0, js::min(1, lo));
    hi = js::max(0, js::min(1, hi));
    if (lo > hi) { if (lo != m_minLevel) hi = lo; else lo = hi; }
    m_minLevel = lo;
    m_maxLevel = hi;
    m_saveRange.start();
    emit changed();
}

void KeyboardController::flushSaves()
{
    if (m_saveSpeed.isActive()) { m_saveSpeed.stop(); m_settings.setKbd({ { u"speed"_s, m_speed } }); }
    if (m_saveRange.isActive()) { m_saveRange.stop(); m_settings.setKbd({ { u"min"_s, m_minLevel }, { u"max"_s, m_maxLevel } }); }
}

QColor KeyboardController::vivid(const QColor &in)
{
    const QColor c = Qml::lighter(in, 1);
    return c.hsvSaturationF() < 0.05 ? Qml::rgba(1, 1, 1, 1)
                                     : Qml::hsva(c.hsvHueF(), js::max(c.hsvSaturationF(), 0.85), 1, 1);
}

// Theme colours: the theme's primary colour first, then from secondary/tertiary and the theme's accent colours the
// hues furthest from the ones already picked (count in total), so look-alikes come last. Grey/white (no hue, vivid()
// makes it white) counts as far from every hue and the same as another white.
QList<QColor> KeyboardController::themeColorsOf(const QColor &primary, const QColor &secondary, const QColor &tertiary,
                                                const QStringList &accents, int count)
{
    QList<QColor> out{ vivid(primary) };
    QList<QColor> pool{ vivid(secondary), vivid(tertiary) };
    for (const QString &a : accents) pool << vivid(QColor::fromString(a));
    auto grey = [](const QColor &c) { return c.hsvHueF() < 0 || c.hsvSaturationF() < 0.05; };
    auto dist = [&](const QColor &a, const QColor &b) -> double {
        if (grey(a) || grey(b)) return grey(a) && grey(b) ? 0 : 0.5;
        const double x = std::fabs(double(a.hsvHueF()) - double(b.hsvHueF()));
        return js::min(x, 1 - x);
    };
    while (out.size() < count && !pool.isEmpty()) {
        qsizetype best = 0;
        double bestD = -1;
        for (qsizetype i = 0; i < pool.size(); ++i) {
            double d = std::numeric_limits<double>::infinity();   // Math.min.apply(null, ...)
            for (const QColor &o : out) d = js::min(d, dist(o, pool[i]));
            if (d > bestD) { bestD = d; best = i; }
        }
        out << pool.takeAt(best);
    }
    return out;
}

QList<QColor> KeyboardController::themeColors() const
{
    return themeColorsOf(m_scheme.primary(), m_scheme.secondary(), m_scheme.tertiary(), m_scheme.accents(), m_themeCount);
}

QList<QColor> KeyboardController::cycle() const
{
    if (m_source == u"multi"_s) {
        QList<QColor> l;
        for (const QString &x : m_colors) l << Qml::lighter(QColor::fromString(x), 1);
        return l;
    }
    if (m_source == u"theme"_s) return themeColors();
    return { m_color };
}

// (QColor's parts are floats; JS takes them as doubles before any arithmetic, so the sums here are in double too)
QColor KeyboardController::mix(const QColor &a, const QColor &b, double f)
{
    const double ar = a.redF(), ag = a.greenF(), ab = a.blueF(), br = b.redF(), bg = b.greenF(), bb = b.blueF();
    return Qml::rgba(ar + (br - ar) * f, ag + (bg - ag) * f, ab + (bb - ab) * f, 1);
}

// Mix along the hue: no dark/muddy midpoint between vivid colours (red -> yellow -> green)
QColor KeyboardController::mixHsv(const QColor &a, const QColor &b, double f)
{
    const double ah = a.hsvHueF(), as = a.hsvSaturationF(), av = a.valueF();
    const double bh = b.hsvHueF(), bs = b.hsvSaturationF(), bv = b.valueF();
    if (as < 0.05 || bs < 0.05) return mix(a, b, f);     // white/grey: plain RGB mix
    double d = bh - ah;
    if (d > 0.5) d -= 1;
    if (d < -0.5) d += 1;
    const double h = std::fmod(std::fmod(ah + d * f, 1) + 1, 1);   // ((h % 1) + 1) % 1
    return Qml::hsva(h, as + (bs - as) * f, av + (bv - av) * f, 1);
}

// At very low levels the channels round to tiny integers and the hue shifts (pink -> [1 0 0] red, for example): when
// the strongest channel drops below lowCut the light turns off instead of showing a wrong colour
QString KeyboardController::frame(const QColor &c, double level) const
{
    const double p[3] = { c.redF(), c.greenF(), c.blueF() };
    QList<double> v;
    for (qsizetype k = 0; k < m_format.channels.size(); ++k)
        v << js::max(0, js::min(1, p[m_format.channels[k]] * level)) * (k < m_format.maxIntensity.size() ? m_format.maxIntensity[k] : 255);
    double top = -std::numeric_limits<double>::infinity();
    for (double x : v) top = js::max(top, x);
    if (top < lowCut) return u"0 0 0"_s;
    QStringList out;
    for (double x : v) out << Qml::number(js::round(x));
    return out.join(u' ');
}

void KeyboardController::tick()
{
    const QList<QColor> cy = cycle();
    const qsizetype n = cy.size();
    if (n == 0) return;
    if (m_frameInFlight) return;                          // (the last frame isn't written yet: this one is skipped)
    const qint64 t = now();
    if (t < m_t0) m_t0 = t;                               // the clock jumped back: start over instead of freezing
    const double tt = (t - m_t0) / 1000 / period();
    const double k = std::floor(tt);
    const double f = tt - k;
    const int idx = int(std::fmod(k, double(n)));
    if (m_cycleIndex != idx) { m_cycleIndex = idx; emit changed(); }
    QString v;
    if (m_effect == u"cycle"_s) {
        // Colour cycle: from colour k to the next with an ease-in/ease-out transition; the light never dims
        const double e = (1 - std::cos(M_PI * f)) / 2;
        v = frame(n > 1 ? mixHsv(cy[idx], cy[int(std::fmod(k + 1, double(n)))], e) : cy[0], 1);
    } else {
        // Breathing: one colour per breath; the colour changes at the darkest point (f = 0), nothing in between
        const double l = std::pow((1 - std::cos(2 * M_PI * f)) / 2, 1.8);
        v = frame(cy[idx], m_minLevel + (m_maxLevel - m_minLevel) * l);
    }
    if (v != m_lastFrame) {
        m_lastFrame = v;
        sendColor(v, true);
        emit wroteFrame(v);
    }
}

// The effect timer runs while an effect is active: ~80 frames per breath, 25 frames/s when fast, fewer when slow;
// zoned keyboards half that (one write per zone). The wake check runs while a light with a brightness is there.
void KeyboardController::updateTimers()
{
    if (effectActive()) {
        const int iv = int(js::max(40, js::min(100, period() * 1000 / 80)) * (m_zones.size() > 1 ? 2 : 1));
        if (!m_effectTimer.isActive()) { m_effectTimer.setInterval(iv); m_effectTimer.start(); }
        else if (m_effectTimer.interval() != iv) m_effectTimer.setInterval(iv);   // (restarts it, as QML's Timer)
    } else {
        m_effectTimer.stop();
    }
    const bool wake = available() && hasBrightness() && !m_quitting;
    if (wake && !m_wake.isActive()) m_wake.start();
    else if (!wake) m_wake.stop();
}

void KeyboardController::restoreForQuit(std::function<void()> done, bool writeColour)
{
    flushSaves();
    const bool active = effectActive();
    const std::optional<QString> c = active ? std::optional<QString>(colorValue(m_color, 1)) : m_pendingColor;
    QStringList pv;
    if (c) for (const QString &z : m_zones) pv << colorPath(z) << *c;
    if (m_pendingBrightness && hasBrightness()) for (const QString &z : m_zones) pv << z + u"/brightness"_s << *m_pendingBrightness;
    m_flush.stop();
    m_pendingColor.reset();
    m_pendingBrightness.reset();
    if (active) m_effect = u"static"_s;                   // not saved: the effect resumes on the next start
    // Nothing more is written after this (deliberate): no frame, no recovery, no wake fix
    m_quitting = true;
    m_recover.stop();
    m_healthy.stop();
    updateTimers();
    emit changed();
    if (pv.isEmpty() || !m_writable || !m_control || !writeColour) { done(); return; }
    write(pv, [done](const QString &) { done(); });
}

// ---------- after the computer wakes up (K-33) ----------
// Going to sleep, the LED core turns the light off through the driver, and the HP module then keeps brightness 0
// (hp-wmi.c hp_kbd_set_brightness, LED_OFF path). On waking up the firmware lights it again, but the module still
// says 0, and every colour written afterwards is applied at that brightness (led-class-multicolor.c
// multi_intensity_store): the light stays on with its last colour and an effect looks frozen. So after a wake-up the
// light's brightness is read again; if it says 0 although it was on before sleeping and the key that turns it off
// (Fn) wasn't used (brightness_hw_changed isn't "0"), the brightness from before is written back. A wake-up shows as
// a jump of the wall clock between two runs of this check (timers don't count while asleep).
void KeyboardController::afterWake()
{
    const int before = m_brightness;
    if (!m_control || !available() || !hasBrightness() || !m_writable || before == 0 || m_wakeReading) return;
    m_wakeReading = true;
    readFiles({ dir() + u"/brightness"_s, dir() + u"/brightness_hw_changed"_s }, [this, before](const QStringList &c, const QList<bool> &ok) {
        m_wakeReading = false;
        // (as `cat "$1/brightness"; cat "$1/brightness_hw_changed" 2>/dev/null || echo none`)
        const QString out = (ok.value(0) ? c.value(0) : QString()) + (ok.value(1) ? c.value(1) : u"none\n"_s);
        const QStringList v = js::trim(out).split(u'\n');
        const double b = js::parseInt(v.value(0));
        const QString hw = js::trim(v.value(1));
        if (!std::isfinite(b) || m_pendingBrightness) return;
        if (b == 0 && hw != u"0"_s) {
            std::fprintf(stderr, "[solwind] after waking up the keyboard light says brightness 0; setting it back to %d\n", before);
            setBrightness(before);
        } else {
            m_brightness = int(b);                        // (turned off with Fn, or nothing to fix)
            m_brightAt = now();
            emit changed();
        }
    });
}

// ---------- write permission (K-34..K-36) ----------
// Only when the user asks, through the system password dialog. The rule template ships with the app
// (dist/90-solwind.rules); @UID@ becomes the user who authenticated (PKEXEC_UID, set by pkexec itself), so only that
// user gets write access. The rendered rule is written to a root-owned file first and checked line by line
// (scripts/kbd-rule-check.sh: only the expected chown lines); nothing is changed if the check fails. Then the old
// world-writable permissions (0666, earlier versions) are reset, the rule goes to /etc/udev/rules.d and udev applies
// it right away. The scripts are 0.5.x's, unchanged.

namespace {
// The rule old Makefiles (Ccenter) installed under /usr/lib or /usr/local/lib: removed with ours, but only if its first
// line says it is Ccenter's (every version starts with "# Ccenter: ")
const QString oldLibRules =
    u"{ for l in /usr/lib/udev/rules.d/90-ccenter.rules /usr/local/lib/udev/rules.d/90-ccenter.rules; do "_s
    u"[ -f \"$l\" ] && head -n 1 \"$l\" | grep -q \"^# Ccenter: \" && rm -f \"$l\"; done; true; } && "_s;
// Only files our rule (owner changed) or an older one (0666) touched go back to root; a permission another rule gave
// (a group, root-owned) stays. "m": also mode 0644 (kbd_rgb_mode keeps its write-only mode).
const QString resetPerms =
    u"rp() { [ -e \"$1\" ] || return 0; case \"$(stat -c \"%u %a\" \"$1\")\" in \"0 666\") ;; \"0 \"*) return 0 ;; esac; "_s
    u"chown root:root \"$1\"; [ -z \"${2:-}\" ] || chmod 0644 \"$1\"; }; "_s
    u"for d in /sys/class/leds/*::kbd_backlight /sys/class/leds/*::kbd_backlight_* /sys/class/leds/*::kbd_zoned_backlight-*; do "_s
    u"[ -d \"$d\" ] || continue; rp \"$d/brightness\" m; rp \"$d/multi_intensity\" m; rp \"$d/kbd_rgb_mode\"; done; "_s
    u"for f in /sys/devices/platform/hp-wmi/rgb_zones/zone*; do rp \"$f\" m; done; "_s;
} // namespace

void KeyboardController::grantPermission()
{
    if (!m_control || m_perm.busy()) return;
    // While the files change hands a running effect must not write, or it hits "Permission denied" in between.
    // Pause it; permDone() resumes it once the new state has been read.
    if (effectActive()) m_paused = true;
    m_permBusy = true;
    m_error.clear();
    updateTimers();
    emit changed();
    m_perm.go(permissionCommand(true),
              [this](int code, const QByteArray &out, const QByteArray &err) { permDone(code, out, err); });
}

void KeyboardController::revokePermission()
{
    if (!m_control || m_perm.busy()) return;
    // Without permission the effect can't write: the keyboard goes to the chosen static colour and the effect pauses.
    // The saved choice is kept, so the effect resumes once permission is granted again.
    if (effectActive()) { m_paused = true; sendColor(colorValue(m_color, 1)); }
    m_permBusy = true;
    m_error.clear();
    updateTimers();
    emit changed();
    m_perm.go(permissionCommand(false),
              [this](int code, const QByteArray &out, const QByteArray &err) { permDone(code, out, err); });
}

QStringList KeyboardController::permissionCommand(bool grant) const
{
    if (grant)
        return { u"pkexec"_s, u"sh"_s, u"-c"_s,
               u"uid=$PKEXEC_UID; case \"$uid\" in \"\"|*[!0-9]*) echo \"unknown user\" >&2; exit 1 ;; esac; "_s
               u"new=/etc/udev/rules.d/90-solwind.rules.new; "_s
               u"sed \"s/@UID@/$uid/g\" \"$1\" > \"$new\" && sh \"$2\" \"$new\" \"$uid\" || { rm -f \"$new\"; exit 1; }; "_s
               + resetPerms +
               u"chmod 644 \"$new\" && mv \"$new\" /etc/udev/rules.d/90-solwind.rules && rm -f /etc/udev/rules.d/90-ccenter.rules && "_s
               + oldLibRules +
               u"udevadm control --reload-rules && udevadm trigger --action=change --subsystem-match=leds && "_s
               u"udevadm trigger --action=change --subsystem-match=platform --sysname-match=hp-wmi && udevadm settle"_s,
               u"sh"_s, m_paths.appDir + u"/dist/90-solwind.rules"_s, m_paths.appDir + u"/scripts/kbd-rule-check.sh"_s };
    return { u"pkexec"_s, u"sh"_s, u"-c"_s,
               u"rm -f /etc/udev/rules.d/90-solwind.rules /etc/udev/rules.d/90-ccenter.rules && "_s + oldLibRules
                   + u"udevadm control --reload-rules; "_s + resetPerms + u"true"_s };
}

void KeyboardController::permDone(int code, const QByteArray &out, const QByteArray &err)
{
    m_permBusy = false;
    // 126/127: password dialog dismissed / not authorised
    if (code != 0) {
        const QString text = js::trim(QString::fromUtf8(err.isEmpty() ? out : err));
        m_error = code == 126 || code == 127 ? m_i18n.t(u"Cancelled."_s) : m_i18n.t(u"Failed: %1"_s).arg(text.section(u'\n', 0, 0));
    } else {
        restoreSavedEffect();                             // a write that failed earlier may have stopped the effect
        m_lostEffect = false;
        m_lightMissing = false;
        m_recoverTries = 0;
    }
    emit changed();
    // Unpause only after the new permission state has been read: if permission is gone, writable=false stops the
    // effect; if it was cancelled, the effect continues
    refresh([this] { m_paused = false; updateTimers(); emit changed(); });
}

// ---------- texts (K-39) ----------

QString KeyboardController::kindText() const
{
    const QString b = backend();
    if (b == u"hp-zoned"_s || b == u"hp-rgbzones"_s) return m_i18n.t(u"RGB, %1 zones (same color on all)"_s).arg(m_zones.size());
    if (b == u"asus-tuf"_s) return m_i18n.t(u"RGB, single zone, ASUS TUF"_s);
    if (b == u"led-rgb"_s) return m_i18n.t(u"RGB, single zone"_s);
    return m_i18n.t(u"single color, %1 brightness levels"_s).arg(m_maxBrightness);
}

// Detection report (solwind kbd info): what was found, step by step, for bug reports
QString KeyboardController::infoText() const
{
    QStringList laptop;
    for (const QString &x : { m_dmi[0], m_dmi[1] }) if (!x.isEmpty()) laptop << x;
    const QString perm = !available() ? u"-"_s
                       : m_writable ? m_i18n.t(u"yes"_s) + (m_ruleLegacy ? u" ("_s + m_i18n.t(u"old, less strict rule"_s) + u")"_s
                                                              : m_ruleOld ? u" ("_s + m_i18n.t(u"Ccenter's rule, old name"_s) + u")"_s : QString())
                       : m_i18n.t(u"no"_s);
    QStringList lines{
        m_i18n.t(u"Laptop: %1"_s).arg(laptop.isEmpty() ? u"?"_s : laptop.join(u" · "_s)) + (m_dmi[3].isEmpty() ? QString() : u" ("_s + m_dmi[3] + u")"_s),
        m_i18n.t(u"Kernel: %1"_s).arg(m_kernel.isEmpty() ? u"?"_s : m_kernel),
        m_i18n.t(u"Driver: %1"_s).arg(m_driver.isEmpty() ? u"-"_s : m_driver),
        m_i18n.t(u"Light: %1"_s).arg(available() ? m_name + u" ("_s + kindText() + u")"_s : m_i18n.t(u"none found"_s)),
        m_i18n.t(u"Permission: %1"_s).arg(perm),
        m_i18n.t(u"Support: %1"_s).arg(supportText()),
    };
    for (const QString &n : m_notes) lines << u"- "_s + noteText(n);
    return lines.join(u'\n');
}

QString KeyboardController::supportText() const
{
    if (!available()) return m_i18n.t(u"not supported"_s);
    if (tested()) return m_i18n.t(u"full (tested)"_s);
    if (backend() == u"hp-rgbzones"_s) return m_i18n.t(u"color only, same on all zones (not tested on this keyboard type)"_s);
    if (rgb()) return m_i18n.t(u"color and brightness (not tested on this model)"_s);
    return m_i18n.t(u"brightness only"_s);
}

QString KeyboardController::noteText(const QString &n) const
{
    if (n == u"legion-usb"_s)
        return m_i18n.t(u"This Lenovo has a USB RGB keyboard controller (ITE). Its colors are not exposed by the kernel; tools like L5P-Keyboard-RGB or OpenRGB control it."_s);
    if (n == u"rog-usb"_s)
        return m_i18n.t(u"This ASUS has an Aura (N-KEY) USB keyboard. Its colors are controlled by asusctl, not through the kernel light."_s);
    if (n == u"hp-none"_s)
        return m_i18n.t(u"No HP keyboard light found. If this laptop has an RGB keyboard: the official kernel doesn't support HP keyboard lights yet, they need the extra hp-wmi-fan-and-backlight-control module (see the README). White (single-color) backlights and per-key RGB keyboards aren't supported by it either; their light only changes with the Fn key."_s);
    return n;
}

QString KeyboardController::noPermissionText() const
{
    return m_i18n.t(u"No write permission: Keyboard tab > 'Grant permission' (or: sudo make udev)"_s);
}

QString KeyboardController::statusText() const
{
    if (!available()) return m_i18n.t(u"No keyboard light found"_s);
    const QString src = m_source == u"single"_s ? m_i18n.t(u"chosen color"_s)
                      : m_source == u"multi"_s ? m_i18n.t(u"%1 colors"_s).arg(m_colors.size())
                      : m_source == u"theme"_s ? m_i18n.t(u"Caelestia theme, %1 colors"_s).arg(m_themeCount)
                                               : u"undefined"_s;   // (src[source] of an unknown source)
    const QString fx = !effectsAllowed() ? m_i18n.t(u"static"_s)
        : m_effect == u"breathing"_s
            ? m_i18n.t(u"breathing (%1, %2 s, %3%–%4%)"_s).arg(src, m_i18n.num(period(), 1))
                  .arg(Qml::number(js::round(m_minLevel * 100)), Qml::number(js::round(m_maxLevel * 100)))
        : m_effect == u"cycle"_s ? m_i18n.t(u"color cycle (%1, %2 s per color)"_s).arg(src, m_i18n.num(period(), 1))
                                 : m_i18n.t(u"static"_s);
    return m_i18n.t(u"Keyboard light: %1"_s).arg(m_name) + u" ("_s + kindText() + u")"_s
        + u"\n"_s + m_i18n.t(u"Color: %1"_s).arg(rgb() ? Qml::hex(m_color) : u"-"_s)
        + (hasBrightness() ? u"\n"_s + m_i18n.t(u"Brightness: %1%"_s).arg(Qml::number(js::round(m_brightness * 100.0 / m_maxBrightness))) : QString())
        + u"\n"_s + m_i18n.t(u"Effect: %1"_s).arg(fx)
        + (m_writable ? QString() : u"\n"_s + noPermissionText());
}
