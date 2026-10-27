#pragma once

#include "plugins/KeyboardPlugin.h"

// HP/hp-wmi-keyboard-zones: HP four-zone keyboards with one LED per zone, as the extra module
// hp-wmi-fan-and-backlight-control (TUXOV) exposes them: 0.0.4 and later hp::kbd_zoned_backlight-right/middle/left/wasd
// (the kernel's naming *::kbd_zoned_backlight-<zone>), 0.0.3 numbers them hp::kbd_backlight_zoneN. Every zone gets the
// same colour and brightness. No software effects: every zone write is a WMI colour-table read and write in both
// module versions, and it wasn't tried on such a keyboard.
class HpWmiKeyboardZones : public KeyboardPlugin
{
public:
    QString id() const override { return QStringLiteral("hp-zoned"); }
    bool rgb() const override { return true; }
    QList<KbdLight> match(const KbdCandidates &c) const override;
    bool writable(const QList<KbdLight> &l) const override;
    KbdFormat format(const KbdLight &first) const override { return kbd::multiFormat(first); }
    std::optional<QColor> shownColor(const KbdLight &first, const KbdFormat &f) const override { return kbd::multiColor(first, f); }
    AtStart atStart() const override { return AtStart::WhenStaticAndOn; }
};
