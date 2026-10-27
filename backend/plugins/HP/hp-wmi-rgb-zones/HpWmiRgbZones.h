#pragma once

#include "plugins/KeyboardPlugin.h"

// HP/hp-wmi-rgb-zones: HP four-zone keyboards with older copies of the hp-wmi-fan-and-backlight-control module (0.0.2,
// e.g. the one victus-control installs): /sys/devices/platform/hp-wmi/rgb_zones/zone00..03, hex RRGGBB. No brightness
// file: colour only, the same on all zones. No software effects (every write reads and writes the WMI colour table).
class HpWmiRgbZones : public KeyboardPlugin
{
public:
    QString id() const override { return QStringLiteral("hp-rgbzones"); }
    bool rgb() const override { return true; }
    bool hasBrightness() const override { return false; }
    QList<KbdLight> match(const KbdCandidates &c) const override;
    bool writable(const QList<KbdLight> &l) const override;
    std::optional<QColor> shownColor(const KbdLight &first, const KbdFormat &) const override;
    QString colorPath(const QString &zone) const override { return zone; }
    QString colorValue(const QColor &c, double level, const KbdFormat &) const override;
};
