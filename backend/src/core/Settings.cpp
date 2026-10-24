#include "Settings.h"

#include "JsCompat.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// JS regex tests are anchored at the very end (QRegularExpression's `$` would also allow a final newline)
bool isHexColor(const QJsonValue &v)
{
    static const QRegularExpression re(QRegularExpression::anchoredPattern(u"#[0-9a-fA-F]{6}"_s));
    return v.isString() && re.match(v.toString()).hasMatch();
}
bool isDigits(const QString &s)
{
    static const QRegularExpression re(QRegularExpression::anchoredPattern(u"[0-9]+"_s));
    return re.match(s).hasMatch();
}
// toLocaleLowerCase(): the profile names are compared in the system locale's case rules, as in the QML engine
QString lower(const QString &s) { return QLocale().toLower(s); }

bool oneOf(const QJsonValue &v, std::initializer_list<const char16_t *> list)
{
    if (!v.isString()) return false;
    const QString s = v.toString();
    return std::any_of(list.begin(), list.end(), [&](const char16_t *x) { return s == QStringView(x); });
}

QJsonObject objectOr(const QJsonValue &v) { return v.isObject() ? v.toObject() : QJsonObject(); }

// An object, or an array as an object keyed "0".."n-1" (JS sets arr["1"] on an array; keys keep the entries)
QJsonObject objectOrIndexed(const QJsonValue &v)
{
    if (!v.isArray()) return objectOr(v);
    QJsonObject o;
    const QJsonArray a = v.toArray();
    for (qsizetype i = 0; i < a.size(); ++i) o.insert(QString::number(i), a[i]);
    return o;
}

// JSON.parse refuses a raw control character (below 0x20) inside a string; QJsonDocument accepts it
bool rawControlInString(const QByteArray &text)
{
    bool in = false, esc = false;
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        if (!in) { if (c == '"') in = true; continue; }
        if (esc) { esc = false; continue; }
        if (c == '\\') esc = true;
        else if (c == '"') in = false;
        else if (c < 0x20) return true;
    }
    return false;
}

// Array.prototype.sort as the QML engine does it: Qt's quicksort from qv4arraydata_p.h (sortHelper, Qt 6.12,
// GPL-2.0/GPL-3.0), copied unchanged. It isn't stable, and points with equal temperatures must come out in the same
// order as in 0.5.x (checked with .dev/settingsdiff.sh).
template <typename RandomAccessIterator, typename LessThan>
void qv4Sort(RandomAccessIterator start, RandomAccessIterator end, LessThan lessThan)
{
top:
    using std::swap;

    int span = int(end - start);
    if (span < 2)
        return;

    --end;
    RandomAccessIterator low = start, high = end - 1;
    RandomAccessIterator pivot = start + span / 2;

    if (lessThan(*end, *start))
        swap(*end, *start);
    if (span == 2)
        return;

    if (lessThan(*pivot, *start))
        swap(*pivot, *start);
    if (lessThan(*end, *pivot))
        swap(*end, *pivot);
    if (span == 3)
        return;

    swap(*pivot, *end);

    while (low < high) {
        while (low < high && lessThan(*low, *end))
            ++low;

        while (high > low && lessThan(*end, *high))
            --high;

        if (low < high) {
            swap(*low, *high);
            ++low;
            --high;
        } else {
            break;
        }
    }

    if (lessThan(*low, *end))
        ++low;

    swap(*end, *low);
    qv4Sort(start, low, lessThan);

    start = low + 1;
    ++end;
    goto top;
}

void writeValue(QByteArray &out, const QJsonValue &v, int level)
{
    const QByteArray pad((level + 1) * 2, ' '), end(level * 2, ' ');
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        if (o.isEmpty()) { out += "{}"; return; }
        out += "{\n";
        for (auto it = o.begin(); it != o.end(); ++it) {
            out += pad;
            writeValue(out, QJsonValue(it.key()), level + 1);
            out += ": ";
            writeValue(out, it.value(), level + 1);
            out += std::next(it) == o.end() ? "\n" : ",\n";
        }
        out += end + '}';
    } else if (v.isArray()) {
        const QJsonArray a = v.toArray();
        if (a.isEmpty()) { out += "[]"; return; }
        out += "[\n";
        for (qsizetype i = 0; i < a.size(); ++i) {
            out += pad;
            writeValue(out, a[i], level + 1);
            out += i + 1 == a.size() ? "\n" : ",\n";
        }
        out += end + ']';
    } else {
        // A scalar the way Qt writes it in compact JSON: "[x]" without the brackets
        const QByteArray c = QJsonDocument(QJsonArray{ v }).toJson(QJsonDocument::Compact);
        out += c.mid(1, c.size() - 2);
    }
}

} // namespace

// ---------- stored forms ----------

QJsonObject FanSt::toJson() const
{
    QJsonValue c = QJsonValue::Null;
    if (curve.size() >= 2) {
        QJsonArray a;
        for (const CurvePoint &p : curve) a.append(QJsonObject{ { u"t"_s, p.t }, { u"s"_s, p.s } });
        c = a;
    }
    return { { u"mode"_s, mode }, { u"fixed"_s, fixed }, { u"curve"_s, c }, { u"smooth"_s, smooth },
             { u"src"_s, src }, { u"hyst"_s, hyst } };
}

QJsonObject GlobalSetting::toJson() const { return { { u"on"_s, on }, { u"st"_s, st.toJson() } }; }

QJsonObject Profile::toJson() const
{
    QJsonObject f;
    for (auto it = fans.begin(); it != fans.end(); ++it) f.insert(it.key(), it.value().toJson());
    return { { u"name"_s, name }, { u"fans"_s, f },
             { u"global"_s, global ? QJsonValue(global->toJson()) : QJsonValue(QJsonValue::Null) } };
}

// ---------- file ----------

Settings::Settings(const QString &dir, QObject *parent)
    : QObject(parent), m_dir(dir), m_path(dir + u"/settings.json"_s),
      m_data{ { u"version"_s, 1 }, { u"configs"_s, QJsonObject() } }
{
    // Rapid changes (a slider being dragged) become one write
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(500);
    connect(&m_saveTimer, &QTimer::timeout, this, &Settings::write);
}

bool Settings::load()
{
    m_unreadable = false;
    QFile f(m_path);
    if (!f.exists()) return false;               // created on the first change
    m_unreadable = true;                         // until it has been read
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("solwindd: could not read %s: %s", qPrintable(m_path), qPrintable(f.errorString()));
        return false;
    }
    // Read the way the QML app did (FileView text() + JSON.parse, measured with .dev/settingsdiff.sh): invalid
    // UTF-8 becomes U+FFFD instead of failing, and a raw control character inside a string fails
    const QByteArray text = QString::fromUtf8(f.readAll()).toUtf8();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(text, &err);
    const QString why = err.error != QJsonParseError::NoError ? err.errorString()
                      : rawControlInString(text) ? u"control character in a string"_s
                      : !doc.isObject() ? u"not a JSON object"_s : QString();
    if (!why.isEmpty()) {
        qWarning("solwindd: could not read settings.json, using defaults: %s", qPrintable(why));
        return false;
    }
    m_data = doc.object();
    m_unreadable = false;
    return true;
}

QByteArray Settings::toFileText(const QJsonObject &o)
{
    QByteArray out;
    writeValue(out, o, 0);
    return out + '\n';
}

void Settings::write()
{
    if (!QDir().mkpath(m_dir)) {
        qWarning("solwindd: could not create the settings folder: %s", qPrintable(m_dir));
        emit writeFailed(u"could not create "_s + m_dir);
        return;
    }
    // A file that was there but couldn't be read is about to be replaced: keep it as settings.json.bad (the QML app
    // just overwrote it)
    if (m_unreadable && QFile::exists(m_path)) {
        const QString bad = m_path + u".bad"_s;
        QFile::remove(bad);
        if (QFile::copy(m_path, bad)) qWarning("solwindd: the unreadable settings file was kept as %s", qPrintable(bad));
        else qWarning("solwindd: could not keep a copy of the unreadable settings file as %s", qPrintable(bad));
    }
    m_unreadable = false;
    QSaveFile f(m_path);                          // temp file + rename: never a half-written settings.json
    if (!f.open(QIODevice::WriteOnly) || f.write(toFileText(m_data)) < 0 || !f.commit()) {
        qWarning("solwindd: could not write %s: %s", qPrintable(m_path), qPrintable(f.errorString()));
        emit writeFailed(f.errorString());
    }
}

void Settings::flush()
{
    if (!m_saveTimer.isActive()) return;
    m_saveTimer.stop();
    write();
}

void Settings::edit(const std::function<void(QJsonObject &)> &fn)
{
    QJsonObject d = m_data;
    if (!d.value(u"configs"_s).isObject()) d.insert(u"configs"_s, QJsonObject());
    fn(d);
    d.insert(u"version"_s, 1);
    m_data = d;
    m_saveTimer.start();
    emit changed();
}

// The settings of one NBFC config ("default" when no config is known); an entry that isn't an object counts as none
QJsonObject Settings::cfg(const QString &configId) const
{
    const QString k = configId.isEmpty() ? u"default"_s : configId;
    return objectOr(objectOr(m_data.value(u"configs"_s)).value(k));
}

// Runs fn on the config's entry inside d and stores it back (Settings.qml slot())
static void editSlot(QJsonObject &d, const QString &configId, const std::function<void(QJsonObject &)> &fn)
{
    const QString k = configId.isEmpty() ? u"default"_s : configId;
    QJsonObject configs = d.value(u"configs"_s).toObject();
    QJsonObject c = objectOr(configs.value(k));
    fn(c);
    configs.insert(k, c);
    d.insert(u"configs"_s, configs);
}

// ---------- validation ----------

std::optional<FanSt> Settings::cleanSt(const QJsonValue &v)
{
    // `!st || typeof st !== "object"`; an array passes that test in JS but has none of the fields, so it ends as null too
    if (!js::truthy(v) || !(v.isObject() || v.isArray())) return std::nullopt;
    const QJsonObject st = v.toObject();
    const double mode = js::round(js::num(st.value(u"mode"_s), 0, 2));
    if (!std::isfinite(mode)) return std::nullopt;
    const double fixed = js::num(st.value(u"fixed"_s), 0, 1);

    // Points: only an array gives any (a string's characters and other values have no t/s). A falsy entry
    // (null, 0, "", false) becomes the point 0 °C / 0 % - `p && p.t` is then the entry itself, and Number() of it 0.
    QList<CurvePoint> curve;
    const QJsonValue c = st.value(u"curve"_s);
    if (c.isArray()) {
        for (const QJsonValue &p : c.toArray()) {
            const double t = js::round(js::num(js::andGet(p, u"t"_s), 0, 110));
            const double s = js::round(js::num(js::andGet(p, u"s"_s), 0, 100));
            if (std::isfinite(t) && std::isfinite(s)) curve.append({ int(t), int(s) });
        }
        qv4Sort(curve.begin(), curve.end(), [](const CurvePoint &a, const CurvePoint &b) { return a.t - b.t < 0; });
    }
    if (mode == 1 && !std::isfinite(fixed)) return std::nullopt;
    if (mode == 2 && curve.size() < 2) return std::nullopt;

    FanSt r;
    r.mode = int(mode);
    r.fixed = std::isfinite(fixed) ? fixed : 0.5;
    if (curve.size() >= 2) r.curve = curve;
    const QJsonValue smooth = st.value(u"smooth"_s);
    r.smooth = !(smooth.isBool() && !smooth.toBool());          // `st.smooth !== false`
    const QJsonValue src = st.value(u"src"_s);
    r.src = oneOf(src, { u"cpu", u"gpu", u"max" }) ? src.toString() : u"cpu"_s;
    const double hyst = js::round(js::num(st.value(u"hyst"_s), 0, 10));
    r.hyst = std::isfinite(hyst) ? int(hyst) : 3;
    return r;
}

QString Settings::cleanName(const QJsonValue &n)
{
    return n.isString() ? js::trim(n.toString()).left(32) : QString();
}

std::optional<Profile> Settings::cleanProfile(const QJsonValue &v)
{
    if (!js::truthy(v) || !(v.isObject() || v.isArray())) return std::nullopt;
    const QJsonObject p = v.toObject();
    Profile r;
    r.name = cleanName(p.value(u"name"_s));
    if (r.name.isEmpty()) return std::nullopt;
    // `for (const k in p.fans)`: an object's keys, or an array's indexes; only digit keys with a valid setting
    const QJsonValue fans = p.value(u"fans"_s);
    if (fans.isObject()) {
        const QJsonObject o = fans.toObject();
        for (auto it = o.begin(); it != o.end(); ++it)
            if (const auto s = cleanSt(it.value()); s && isDigits(it.key())) r.fans.insert(it.key(), *s);
    } else if (fans.isArray()) {
        const QJsonArray a = fans.toArray();
        for (qsizetype i = 0; i < a.size(); ++i)
            if (const auto s = cleanSt(a[i])) r.fans.insert(QString::number(i), *s);
    }
    const QJsonValue g = p.value(u"global"_s);
    if (js::truthy(g))
        if (const auto gs = cleanSt(js::andGet(g, u"st"_s)))
            r.global = GlobalSetting{ g.toObject().value(u"on"_s) == QJsonValue(true), *gs };
    return r;
}

// ---------- reading ----------

std::optional<FanSt> Settings::fanOf(const QString &configId, int fan) const
{
    const QJsonValue fans = cfg(configId).value(u"fans"_s);
    if (fans.isObject()) return cleanSt(fans.toObject().value(QString::number(fan)));
    if (fans.isArray()) return cleanSt(fans.toArray().at(fan));     // c.fans["0"] works on an array in JS too
    return std::nullopt;
}

std::optional<GlobalSetting> Settings::globalOf(const QString &configId) const
{
    const QJsonValue g = cfg(configId).value(u"global"_s);
    if (!js::truthy(g)) return std::nullopt;
    const auto st = cleanSt(js::andGet(g, u"st"_s));
    if (!st) return std::nullopt;
    return GlobalSetting{ g.toObject().value(u"on"_s) == QJsonValue(true), *st };
}

QList<Profile> Settings::profilesOf(const QString &configId) const
{
    QList<Profile> out;
    const QJsonValue list = cfg(configId).value(u"profiles"_s);
    if (!list.isArray()) return out;
    for (const QJsonValue &p : list.toArray())
        if (const auto c = cleanProfile(p)) out.append(*c);
    return out;
}

QString Settings::activeOf(const QString &configId) const
{
    const QJsonValue a = cfg(configId).value(u"active"_s);
    return a.isString() ? a.toString() : QString();
}

bool Settings::notifyOn(const QString &key) const
{
    return !(m_data.value(u"notify"_s).isObject() && m_data.value(u"notify"_s).toObject().value(key) == QJsonValue(false));
}

bool Settings::uiOn(const QString &key) const
{
    return !(m_data.value(u"ui"_s).isObject() && m_data.value(u"ui"_s).toObject().value(key) == QJsonValue(false));
}

QString Settings::uiLang() const
{
    const QJsonValue l = objectOr(m_data.value(u"ui"_s)).value(u"lang"_s);
    return oneOf(l, { u"tr", u"en" }) ? l.toString() : QString();
}

KbdSettings Settings::kbd() const
{
    const QJsonObject k = objectOr(m_data.value(u"kbd"_s));
    KbdSettings r;
    const QJsonValue colors = k.value(u"colors"_s);
    if (colors.isArray())
        for (const QJsonValue &c : colors.toArray())
            if (isHexColor(c) && r.colors.size() < 6) r.colors << c.toString();
    if (r.colors.isEmpty()) r.colors = { u"#ff0000"_s, u"#00ff00"_s, u"#0040ff"_s };
    const double sp = js::num(k.value(u"speed"_s), 0, 1);
    double lo = js::num(k.value(u"min"_s), 0, 1), hi = js::num(k.value(u"max"_s), 0, 1);
    if (!std::isfinite(lo)) lo = 0;
    if (!std::isfinite(hi)) hi = 1;
    if (lo > hi) lo = hi;
    const QJsonValue effect = k.value(u"effect"_s), source = k.value(u"source"_s);
    r.effect = oneOf(effect, { u"static", u"breathing", u"cycle" }) ? effect.toString() : u"static"_s;
    const double tc = js::num(k.value(u"themeCount"_s), 3, 6);
    r.themeCount = std::isfinite(tc) ? int(js::round(tc)) : 3;
    r.source = oneOf(source, { u"single", u"multi", u"theme" }) ? source.toString() : u"single"_s;
    r.speed = std::isfinite(sp) ? sp : 0.5;
    r.min = lo;
    r.max = hi;
    r.color = isHexColor(k.value(u"color"_s)) ? k.value(u"color"_s).toString() : QString();
    return r;
}

double Settings::safety() const
{
    return js::num(m_data.value(u"safety"_s), 70, 100);   // NaN if missing or invalid (90 is used then)
}

// ---------- writing ----------

void Settings::setFan(const QString &configId, int fan, const FanSt &st)
{
    edit([&](QJsonObject &d) {
        editSlot(d, configId, [&](QJsonObject &c) {
            QJsonObject fans = objectOrIndexed(c.value(u"fans"_s));
            fans.insert(QString::number(fan), st.toJson());
            c.insert(u"fans"_s, fans);
        });
    });
}

void Settings::setGlobal(const QString &configId, bool on, const FanSt &st)
{
    edit([&](QJsonObject &d) {
        editSlot(d, configId, [&](QJsonObject &c) { c.insert(u"global"_s, GlobalSetting{ on, st }.toJson()); });
    });
}

void Settings::saveProfile(const QString &configId, const Profile &p)
{
    edit([&](QJsonObject &d) {
        editSlot(d, configId, [&](QJsonObject &c) {
            // Names are matched case-insensitively (FanState.applyByName): "gece" replaces "Gece"
            const QString key = lower(p.name);
            const QJsonValue active = c.value(u"active"_s);
            if (active.isString() && lower(active.toString()) == key) c.insert(u"active"_s, p.name);
            QJsonArray list;
            if (c.value(u"profiles"_s).isArray())
                for (const QJsonValue &x : c.value(u"profiles"_s).toArray()) {
                    const QJsonValue n = x.isObject() ? x.toObject().value(u"name"_s) : QJsonValue();
                    const bool same = n.isString() && lower(n.toString()) == key;
                    if (js::truthy(x) && !same) list.append(x);
                }
            list.append(p.toJson());
            c.insert(u"profiles"_s, list);
        });
    });
}

void Settings::removeProfile(const QString &configId, const QString &name)
{
    edit([&](QJsonObject &d) {
        editSlot(d, configId, [&](QJsonObject &c) {
            if (c.value(u"profiles"_s).isArray()) {
                QJsonArray list;
                for (const QJsonValue &x : c.value(u"profiles"_s).toArray()) {
                    const bool same = x.isObject() && x.toObject().value(u"name"_s) == QJsonValue(name);
                    if (js::truthy(x) && !same) list.append(x);
                }
                c.insert(u"profiles"_s, list);
            }
            if (c.value(u"active"_s) == QJsonValue(name)) c.insert(u"active"_s, QString());
        });
    });
}

void Settings::setActive(const QString &configId, const QString &name)
{
    if (activeOf(configId) == name) return;
    edit([&](QJsonObject &d) { editSlot(d, configId, [&](QJsonObject &c) { c.insert(u"active"_s, name); }); });
}

void Settings::setNotify(const QString &key, bool on)
{
    edit([&](QJsonObject &d) {
        QJsonObject n = objectOr(d.value(u"notify"_s));
        n.insert(key, on);
        d.insert(u"notify"_s, n);
    });
}

void Settings::setUi(const QString &key, bool on)
{
    edit([&](QJsonObject &d) {
        QJsonObject u = objectOr(d.value(u"ui"_s));
        u.insert(key, on);
        d.insert(u"ui"_s, u);
    });
}

void Settings::setUiLang(const QString &lang)
{
    edit([&](QJsonObject &d) {
        QJsonObject u = objectOr(d.value(u"ui"_s));
        u.insert(u"lang"_s, lang);
        d.insert(u"ui"_s, u);
    });
}

void Settings::setKbd(const QJsonObject &patch)
{
    edit([&](QJsonObject &d) {
        QJsonObject k = objectOr(d.value(u"kbd"_s));
        for (auto it = patch.begin(); it != patch.end(); ++it) k.insert(it.key(), it.value());
        d.insert(u"kbd"_s, k);
    });
}

void Settings::setSafety(double v)
{
    edit([&](QJsonObject &d) { d.insert(u"safety"_s, v); });
}
