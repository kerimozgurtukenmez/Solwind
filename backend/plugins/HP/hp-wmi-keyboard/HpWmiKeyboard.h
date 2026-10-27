#pragma once

#include "plugins/KeyboardPlugin.h"

// HP/hp-wmi-keyboard: the single RGB keyboard light of HP's hp-wmi driver, as the extra module
// hp-wmi-fan-and-backlight-control (TUXOV, DKMS) exposes it: hp::kbd_backlight, the LED class's multicolor files
// (brightness + multi_intensity). The official kernel's hp-wmi has no keyboard light. Software effects run here: tested
// on a Victus 16-r0 (board 8BBD) with module 0.0.4.
class HpWmiKeyboard : public KeyboardPlugin
{
public:
    QString id() const override { return QStringLiteral("led-rgb"); }
    bool rgb() const override { return true; }
    bool effectsTested(const QString &driver) const override;
    bool tested(const QString &driver) const override;
    QList<KbdLight> match(const KbdCandidates &c) const override;
    bool writable(const QList<KbdLight> &l) const override;
    KbdFormat format(const KbdLight &first) const override { return kbd::multiFormat(first); }
    std::optional<QColor> shownColor(const KbdLight &first, const KbdFormat &f) const override { return kbd::multiColor(first, f); }
    AtStart atStart() const override { return AtStart::WhenStaticAndOn; }
};
