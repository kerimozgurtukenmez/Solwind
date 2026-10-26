#pragma once

#include "plugins/KeyboardPlugin.h"

#include <QString>
#include <QStringList>

#include <optional>

// Detection's answer (scripts/kbdinfo.sh / KeyboardScan lines) read the way Keyboard.qml parse() reads it, the parts
// that depend on nothing else (SPEC.md K-6..K-13, K-17): the plugin and its lights, the light's values, the notes,
// the permission rule. Which colour counts as chosen (K-14..K-16) is the controller's, it needs the settings.
struct KbdFound {
    const KeyboardPlugin *plugin = nullptr;   // none found
    QStringList zones;                        // light folders written together (hp-rgbzones: the zone files)
    QString name, driver;
    bool writable = false;
    int maxBrightness = 255;
    int brightness = 0;
    KbdFormat format;
    std::optional<QColor> shownColor;         // what the light shows, if it can be read back
    QStringList notes;                        // legion-usb | rog-usb | hp-none
    QString ruleAt;                           // "etc" | "lib" | ""
    bool ruleLegacy = false, ruleOld = false;
    // dmi / kernel lines (kept from before if a read had none, as the QML properties)
    std::optional<QStringList> dmi;           // vendor, product, family, board
    std::optional<QString> kernel;
};

namespace KeyboardParse {
// `prevVendor`: the vendor known before this read (the notes use the property, which keeps it without a dmi line)
KbdFound parse(const QString &text, const QString &prevVendor = QString());
}
