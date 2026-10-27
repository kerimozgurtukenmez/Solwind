#pragma once

#include "I18n.h"
#include "Paths.h"
#include "plugins/PluginHost.h"

// solwindd's side of PluginHost: its I18n (the UI language, translations.js) and its Paths
class CoreHost : public PluginHost
{
public:
    CoreHost(const I18n &i18n, const Paths &paths) : m_i18n(i18n), m_paths(paths) {}
    QString t(const QString &english) const override { return m_i18n.t(english); }
    const Paths &paths() const override { return m_paths; }

private:
    const I18n &m_i18n;
    const Paths &m_paths;
};
