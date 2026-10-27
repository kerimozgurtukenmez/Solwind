#include "DaemonApi.h"

#include "ClientHints.h"

#include "core/I18n.h"
#include "core/PluginLoader.h"
#include "core/Paths.h"
#include "core/Settings.h"

#include <QCoreApplication>
#include <QDBusMessage>
#include <QTimer>

using namespace Qt::StringLiterals;

DaemonApi::DaemonApi(const Paths &paths, Settings &settings, const I18n &i18n, bool settingsLoaded, ClientHints *hints,
                     QObject *parent)
    : QObject(parent), m_paths(paths), m_settings(settings), m_i18n(i18n), m_settingsLoaded(settingsLoaded), m_hints(hints)
{
}

QString DaemonApi::version() const { return QStringLiteral(SOLWIND_VERSION); }
QString DaemonApi::language() const { return m_i18n.lang(); }
QString DaemonApi::savedLanguage() const { return m_settings.uiLang(); }
bool DaemonApi::notifyEnabled() const { return m_settings.notifyOn(u"enabled"_s); }
bool DaemonApi::notifyProfile() const { return m_settings.notifyOn(u"profile"_s); }
bool DaemonApi::trayEnabled() const { return m_settings.uiOn(u"tray"_s); }

VariantMapList DaemonApi::plugins() const
{
    VariantMapList out;
    if (loader)
        for (const PluginLoader::Plugin &p : loader->plugins())
            out.append({ { u"id"_s, p.id }, { u"brand"_s, p.brand }, { u"module"_s, p.module }, { u"kind"_s, p.kind },
                         { u"version"_s, p.version }, { u"description"_s, p.description }, { u"file"_s, p.file },
                         { u"used"_s, p.used } });
    return out;
}

VariantMapList DaemonApi::pluginFailures() const
{
    VariantMapList out;
    if (loader) for (const PluginLoader::Failure &f : loader->failures()) out.append({ { u"file"_s, f.file }, { u"why"_s, f.why } });
    return out;
}

QVariantMap DaemonApi::Status() const
{
    QVariantMap m{
        { u"version"_s, version() },
        { u"pid"_s, QCoreApplication::applicationPid() },
        { u"settingsPath"_s, m_settings.path() },
        { u"settingsLoaded"_s, m_settingsLoaded },
        { u"sysRoot"_s, m_paths.sysRoot },
        { u"mode"_s, controlMode ? u"control"_s : u"observe"_s },
    };
    if (extraStatus) m.insert(extraStatus());
    return m;
}

void DaemonApi::Watch(const QString &topic)
{
    if (!calledFromDBus() || !m_hints) return;
    if (!ClientHints::validTopic(topic)) {
        sendErrorReply(QDBusError::InvalidArgs, u"unknown topic: "_s + topic + u" (window | fans)"_s);
        return;
    }
    m_hints->watch(message().service(), topic);
}

void DaemonApi::Unwatch(const QString &topic)
{
    if (!calledFromDBus() || !m_hints) return;
    if (!ClientHints::validTopic(topic)) {
        sendErrorReply(QDBusError::InvalidArgs, u"unknown topic: "_s + topic + u" (window | fans)"_s);
        return;
    }
    m_hints->unwatch(message().service(), topic);
}

void DaemonApi::SetNotify(const QString &key, bool on)
{
    if (!calledFromDBus()) return;
    if (!controlMode) { sendErrorReply(u"solwind.Error.Observe"_s, u"observe mode: nothing is written"_s); return; }
    if (key != u"enabled"_s && key != u"profile"_s) {
        sendErrorReply(u"solwind.Error.Usage"_s, u"notify: enabled | profile"_s);
        return;
    }
    m_settings.setNotify(key, on);
}

void DaemonApi::SetLanguage(const QString &lang)
{
    if (!calledFromDBus()) return;
    if (!controlMode) { sendErrorReply(u"solwind.Error.Observe"_s, u"observe mode: nothing is written"_s); return; }
    if (!lang.isEmpty() && lang != u"tr"_s && lang != u"en"_s) {
        sendErrorReply(u"solwind.Error.Usage"_s, u"tr | en"_s);
        return;
    }
    m_settings.setUiLang(lang);
}

void DaemonApi::SetUi(const QString &key, bool on)
{
    if (!calledFromDBus()) return;
    if (!controlMode) { sendErrorReply(u"solwind.Error.Observe"_s, u"observe mode: nothing is written"_s); return; }
    if (key != u"tray"_s) {
        sendErrorReply(u"solwind.Error.Usage"_s, u"ui: tray"_s);
        return;
    }
    m_settings.setUi(key, on);
}

void DaemonApi::Quit()
{
    // Queued: the D-Bus reply to this call goes out first
    QTimer::singleShot(0, this, &DaemonApi::quitRequested);
}
