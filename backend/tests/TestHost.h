#pragma once

#include "core/I18n.h"
#include "core/Paths.h"
#include "plugins/PluginHost.h"

// What a plugin gets from solwindd (PluginHost), for the tests: an I18n and Paths of its own
struct TestHost : PluginHost {
    I18n i18n;
    Paths p;
    QString t(const QString &english) const override { return i18n.t(english); }
    const Paths &paths() const override { return p; }
};
