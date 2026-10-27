#include "KeyboardParse.h"

#include "JsCompat.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {
struct Usb { QString vid, pid, name; };
}

KbdFound KeyboardParse::parse(const QString &text, const QString &prevVendor, const QList<const KeyboardPlugin *> &plugins)
{
    KbdFound r;
    QList<KbdLight> leds, rgbZones;
    QHash<QString, std::pair<bool, qsizetype>> byName;     // name -> (rgb_zones?, index): the latest with that name
    QList<Usb> usb;
    QString rule, ruleLib;
    bool libLegacy = false;
    for (const QString &line : text.split(u'\n')) {
        const QStringList p = line.split(u'|');
        const QString k = p[0];
        auto at = [&](qsizetype i) { return i < p.size() ? p[i] : QString(); };
        if (k == u"dmi"_s && p.size() >= 6) r.dmi = QStringList{ p[1], p[2], p[4], p[5] };
        else if (k == u"kernel"_s) r.kernel = at(1);
        else if ((k == u"led"_s || k == u"zone"_s) && p.size() >= 4) {
            KbdLight l{ p[1], p[2], p.mid(3).join(u'|'), {}, {}, {} };
            QList<KbdLight> &list = k == u"led"_s ? leds : rgbZones;
            list << l;
            byName.insert(p[1], { k == u"zone"_s, list.size() - 1 });
        } else if ((k == u"v"_s || k == u"w"_s || k == u"x"_s) && p.size() >= 2 && byName.contains(p[1])) {
            const auto [zone, i] = byName.value(p[1]);
            KbdLight &l = zone ? rgbZones[i] : leds[i];
            // (a missing file name is "undefined" in the QML: a key nobody reads)
            const QString file = p.size() >= 3 ? p[2] : u"undefined"_s;
            if (k == u"v"_s) l.v.insert(file, p.mid(3).join(u'|'));
            else (k == u"w"_s ? l.w : l.x).insert(file);
        } else if (k == u"usb"_s) {
            usb.append({ at(1), at(2), at(3) });
        } else if (k == u"rule"_s && at(1) == u"etc"_s) {
            if (rule.isEmpty()) rule = at(2) == u"legacy"_s ? u"etc-legacy"_s : at(2) == u"old"_s ? u"etc-old"_s : u"etc"_s;
        } else if (k == u"rule"_s && at(1) == u"lib"_s) {
            ruleLib = u"lib"_s;
            if (at(2) == u"legacy"_s) libLegacy = true;
        }
    }
    r.ruleAt = rule.startsWith(u"etc"_s) ? u"etc"_s : ruleLib;
    r.ruleLegacy = rule == u"etc-legacy"_s || libLegacy;
    r.ruleOld = rule == u"etc-old"_s;

    // Numbered zone lights of one driver are one keyboard too, never a single light (that would write one zone only)
    KbdCandidates c;
    c.leds = leds;
    c.rgbZones = rgbZones;
    static const QRegularExpression numberedRe(u"::kbd_backlight_zone[0-9]+\\z"_s);
    QList<KbdLight> numbered, named;
    for (const KbdLight &l : leds) {
        if (numberedRe.match(l.name).hasMatch()) numbered << l;
        if (l.name.contains(u"::kbd_zoned_backlight-"_s)) named << l;
    }
    const bool oneDriver = std::all_of(numbered.begin(), numbered.end(), [&](const KbdLight &l) { return l.driver == numbered[0].driver; });
    c.zoned = !named.isEmpty() ? named : !numbered.isEmpty() && oneDriver ? numbered : QList<KbdLight>();
    auto inZoned = [&](const KbdLight &l) {
        // (the QML compares objects: the same entry, not just the same name)
        return std::any_of(c.zoned.begin(), c.zoned.end(), [&](const KbdLight &z) { return z.path == l.path && z.name == l.name && z.v == l.v; });
    };
    for (const KbdLight &l : leds) if (l.name.endsWith(u"::kbd_backlight"_s)) { c.single = l; break; }
    if (!c.single)
        for (const KbdLight &l : leds) if (l.name.contains(u"::kbd_backlight"_s) && !inZoned(l)) { c.single = l; break; }

    QList<KbdLight> use;
    for (const KeyboardPlugin *pl : plugins) {
        use = pl->match(c);
        if (!use.isEmpty()) { r.plugin = pl; break; }
    }
    for (const KbdLight &l : use) r.zones << l.path;
    if (r.plugin && r.plugin->id() == u"hp-rgbzones"_s) r.name = u"hp-wmi rgb_zones ×%1"_s.arg(use.size());
    else if (use.size() > 1) {
        static const QRegularExpression suffix(u"(-[^-]*|_zone[0-9]+)\\z"_s);
        QString n = use[0].name;
        const QRegularExpressionMatch m = suffix.match(n);
        if (m.hasMatch()) n.remove(m.capturedStart(), m.capturedLength());
        r.name = n + u" ×%1"_s.arg(use.size());
    } else r.name = use.isEmpty() ? QString() : use[0].name;
    r.driver = use.isEmpty() ? QString() : use[0].driver;
    r.writable = r.plugin && r.plugin->writable(use);
    if (!use.isEmpty()) {
        const KbdLight &first = use[0];
        const double mb = js::parseInt(first.v.value(u"max_brightness"_s));
        r.maxBrightness = mb == 0 || std::isnan(mb) ? 255 : int(mb);
        if (!r.plugin->hasBrightness()) r.brightness = r.maxBrightness;   // (no brightness: always full)
        else {
            const double b = js::parseInt(first.v.value(u"brightness"_s));
            r.brightness = b == 0 || std::isnan(b) ? 0 : int(b);
        }
        r.format = r.plugin->format(first);
        r.shownColor = r.plugin->shownColor(first, r.format);
    }

    // Findings worth explaining (RGB the kernel doesn't expose)
    const QString vendor = (r.dmi ? r.dmi->at(0) : prevVendor).toLower();
    static const QRegularExpression c9(u"\\Ac9"_s, QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression nkey(u"n-key"_s, QRegularExpression::CaseInsensitiveOption);
    const QString id = r.plugin ? r.plugin->id() : QString();
    if (vendor.contains(u"lenovo"_s) && std::any_of(usb.begin(), usb.end(), [](const Usb &u) { return u.vid == u"048d"_s && c9.match(u.pid).hasMatch(); }))
        r.notes << u"legion-usb"_s;
    if (vendor.contains(u"asus"_s) && id != u"asus-tuf"_s
        && std::any_of(usb.begin(), usb.end(), [](const Usb &u) { return u.vid == u"0b05"_s && nkey.match(u.name).hasMatch(); }))
        r.notes << u"rog-usb"_s;
    // A light is there, but no installed plugin takes it (since 1.0: plugins are installed one by one). Otherwise, on an
    // HP without any light: the extra module is missing.
    const std::optional<KbdLight> found = c.single ? c.single
                                        : !c.zoned.isEmpty() ? std::optional<KbdLight>(c.zoned[0])
                                        : !c.rgbZones.isEmpty() ? std::optional<KbdLight>(c.rgbZones[0]) : std::nullopt;
    if (id.isEmpty() && found) {
        r.notes << u"no-plugin"_s;
        r.unsupportedName = found->name;
        r.unsupportedDriver = found->driver;
    } else if ((vendor == u"hp"_s || vendor.contains(u"hewlett"_s)) && id.isEmpty()) {
        r.notes << u"hp-none"_s;
    }
    return r;
}
