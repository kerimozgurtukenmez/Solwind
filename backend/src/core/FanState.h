#pragma once

#include "Settings.h"

#include <QObject>
#include <QStringList>

class FanController;
class FanPlugin;
class I18n;
class Notifier;

// Fan settings and profiles - port of services/FanState.qml (SPEC.md §4 ST-1..ST-3, §5 F-1..F-8, PR-1..PR-11).
// The one source of the fans' settings: what the clients show and change, restored at start for the config the fans
// belong to, saved per config in settings.json.
class FanState : public QObject
{
    Q_OBJECT
public:
    FanState(FanPlugin &plugin, FanController &control, Settings &settings, const I18n &i18n, Notifier &notifier,
             QObject *parent = nullptr);

    // A built-in or user profile (PR-1, PR-2). Built-ins: `name` is the id saved as active, `label` is translated
    struct ProfileEntry {
        QString name;
        QString label;
        bool builtin = false;
        QString icon;
        Profile profile;              // fans + global (built-ins: global only; "auto": nothing)
    };
    struct Result {
        bool ok = false;
        bool usage = false;           // the name matched no profile (a wrong argument)
        QString text;
    };

    // ---------- the defaults (ST-2, ST-3) ----------
    static QList<CurvePoint> defaultCurve();
    static FanSt autoSt();
    static FanSt full(const FanSt &s);   // a curve with fewer than 2 points becomes the default curve

    // ---------- state ----------
    const QList<FanSt> &fanSt() const { return m_fanSt; }
    const FanSt &globalSt() const { return m_globalSt; }
    bool globalOn() const { return m_globalOn; }
    bool restored() const { return m_restored; }

    // Startup and every status read (deferred): the saved settings of the config the fans belong to (F-1..F-4)
    void restore();
    void unrestore() { m_restored = false; }                     // another config: restored on the next read

    // ---------- changes from clients (F-5..F-8) ----------
    void setFan(int i, const FanSt &st);
    void setGlobal(const FanSt &st);
    void setGlobalOn(bool on);

    // ---------- profiles (PR-1..PR-10) ----------
    QList<ProfileEntry> builtins() const;
    QList<ProfileEntry> allProfiles() const;
    QString displayName(const ProfileEntry &p) const;
    QString activeId() const;
    QString activeName() const;
    QStringList namesOf(const ProfileEntry &p) const;
    bool reservedName(const QString &name) const;
    Profile snapshot(const QString &name) const;
    void applyProfile(const ProfileEntry &p);
    // By any name it answers to (UI, terminal, tray); fromClient: a terminal / keybind, notified if notify.profile
    Result applyByName(const QString &name, bool fromClient);
    QString profileList() const;
    // SaveProfile from a client: name trimmed, 1..32 characters, not a built-in's (UI-7); "" = saved, else why not
    QString saveProfile(const QString &name);
    QString removeProfile(const QString &name);                 // "" = removed (built-ins can't be)

signals:
    void changed();

private:
    void reapply();
    void manualEdit();

    FanPlugin &m_plugin;
    FanController &m_control;
    Settings &m_settings;
    const I18n &m_i18n;
    Notifier &m_notifier;
    QList<FanSt> m_fanSt;
    FanSt m_globalSt;
    bool m_globalOn = false;
    bool m_restored = false;
    bool m_applying = false;          // loading from settings / a profile: not a manual edit
};
