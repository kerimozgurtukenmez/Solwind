#pragma once

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include <functional>
#include <optional>

// A curve point: at t °C the fan runs at s %
struct CurvePoint {
    int t = 0;
    int s = 0;
    bool operator==(const CurvePoint &) const = default;
};

// One fan's setting ("st" in SPEC.md §4)
struct FanSt {
    int mode = 0;                  // 0 auto, 1 fixed, 2 curve
    double fixed = 0.5;            // 0..1
    QList<CurvePoint> curve;       // sorted by t; fewer than 2 points = none
    bool smooth = true;            // straight lines between points (else steps)
    QString src = QStringLiteral("cpu");   // cpu | gpu | max
    int hyst = 3;                  // 0..10 °C
    QJsonObject toJson() const;    // the stored form; `curve` is null without two points (as Settings.qml writes it)
    bool operator==(const FanSt &) const = default;
};

struct GlobalSetting {
    bool on = false;
    FanSt st;
    QJsonObject toJson() const;
};

// A user profile (SPEC.md PR-2)
struct Profile {
    QString name;
    QMap<QString, FanSt> fans;               // fan number as a string ("0", "1", ...)
    std::optional<GlobalSetting> global;
    QJsonObject toJson() const;
};

// Saved keyboard settings (SPEC.md K-30), already validated
struct KbdSettings {
    QString effect;                // static | breathing | cycle
    QString source;                // single | multi | theme
    QStringList colors;            // "#rrggbb", 1..6
    double speed = 0.5;            // 0..1
    double min = 0;                // lowest breathing level 0..1
    double max = 1;                // highest breathing level 0..1
    int themeCount = 3;            // 3..6
    QString color;                 // "#rrggbb" or ""
};

// ~/.config/solwind/settings.json: the same file and format as Settings.qml (0.5.x), so going back keeps working.
// Everything read is validated where it is read (as Settings.qml does); everything else in the file is kept as it is.
// Changes are written 500 ms after the last one, atomically (temp file + rename). Created on the first change.
class Settings : public QObject
{
    Q_OBJECT
public:
    explicit Settings(const QString &dir, QObject *parent = nullptr);

    QString path() const { return m_path; }
    // Reads the file (blocking). false: missing or not valid JSON (defaults are used, the file isn't touched until a
    // change is saved; an unreadable file is kept as settings.json.bad then)
    bool load();
    const QJsonObject &data() const { return m_data; }

    // ---------- validation (SPEC.md ST-4, PR-2) ----------
    static std::optional<FanSt> cleanSt(const QJsonValue &st);
    static QString cleanName(const QJsonValue &n);
    static std::optional<Profile> cleanProfile(const QJsonValue &p);

    // ---------- reading ----------
    std::optional<FanSt> fanOf(const QString &configId, int fan) const;
    std::optional<GlobalSetting> globalOf(const QString &configId) const;
    QList<Profile> profilesOf(const QString &configId) const;
    QString activeOf(const QString &configId) const;
    bool notifyOn(const QString &key) const;      // notify.<key>, default on
    bool uiOn(const QString &key) const;          // ui.<key>, default on
    QString uiLang() const;                       // "tr" | "en" | "" (follow the system)
    KbdSettings kbd() const;
    double safety() const;                        // 70..100, NaN if not set or invalid

    // ---------- writing ----------
    void setFan(const QString &configId, int fan, const FanSt &st);
    void setGlobal(const QString &configId, bool on, const FanSt &st);
    void saveProfile(const QString &configId, const Profile &p);   // replaces one with the same name in any case
    void removeProfile(const QString &configId, const QString &name);
    void setActive(const QString &configId, const QString &name);
    void setNotify(const QString &key, bool on);
    void setUi(const QString &key, bool on);
    void setUiLang(const QString &lang);
    void setKbd(const QJsonObject &patch);        // merged into kbd
    void setSafety(double v);

    bool writePending() const { return m_saveTimer.isActive(); }
    void flush();                                  // write a pending change now (before quitting)

    // JSON as Settings.qml writes it: JSON.stringify(data, null, 2) + "\n" (keys in Qt's order: sorted)
    static QByteArray toFileText(const QJsonObject &o);

signals:
    void changed();
    void writeFailed(const QString &why);

private:
    void edit(const std::function<void(QJsonObject &)> &fn);
    QJsonObject cfg(const QString &configId) const;
    void write();

    QString m_dir;
    QString m_path;
    QJsonObject m_data;
    QTimer m_saveTimer;
    bool m_unreadable = false;     // the file exists but couldn't be read: back it up before the first write
};
