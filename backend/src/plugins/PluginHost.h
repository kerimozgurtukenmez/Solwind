#pragma once

#include "core/Paths.h"

#include <QString>

// What solwindd gives the plugins it loads (SDK): texts in the user's language - from Solwind's translations.js, the
// key is the English text, so a plugin's messages are translated like the core's - and the folders it reads (the real
// /sys and NBFC files, or the fake ones of the tests)
class PluginHost
{
public:
    virtual ~PluginHost() = default;
    virtual QString t(const QString &english) const = 0;
    virtual const Paths &paths() const = 0;
};
