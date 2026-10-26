#include "CaelestiaScheme.h"

#include "JsCompat.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonObject>

using namespace Qt::StringLiterals;

CaelestiaScheme::CaelestiaScheme(const QString &path, QObject *parent) : QObject(parent), m_path(path)
{
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { watch(); load(); });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { watch(); load(); });
    watch();
    load();
}

void CaelestiaScheme::watch()
{
    const QString dir = QFileInfo(m_path).absolutePath();
    if (QFileInfo(dir).isDir() && !m_watcher.directories().contains(dir)) m_watcher.addPath(dir);
    if (QFileInfo::exists(m_path) && !m_watcher.files().contains(m_path)) m_watcher.addPath(m_path);
}

void CaelestiaScheme::load()
{
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly)) return;
    const auto doc = js::parseJson(f.read(4 << 20));
    if (!doc || !doc->isObject()) return;
    const QJsonValue c = doc->toObject().value(u"colours"_s);
    if (!c.isObject()) return;
    const QJsonObject o = c.toObject();
    // (Colours.qml: a key that is there and truthy is "#" + its value; only real colours are taken here)
    auto colour = [&](const QString &key, QColor *out) {
        const QColor v = QColor::fromString(u"#"_s + o.value(key).toString());
        if (o.value(key).isString() && v.isValid()) *out = v;
    };
    const QColor p = m_primary, s = m_secondary, t = m_tertiary;
    colour(u"primary"_s, &m_primary);
    colour(u"secondary"_s, &m_secondary);
    colour(u"tertiary"_s, &m_tertiary);
    QStringList accents;
    for (const char16_t *k : { u"mauve", u"pink", u"red", u"maroon", u"peach", u"yellow", u"green", u"teal", u"sky",
                               u"sapphire", u"blue", u"lavender", u"flamingo", u"rosewater" }) {
        const QJsonValue v = o.value(QString::fromUtf16(k));
        if (v.isString() && QColor::fromString(u"#"_s + v.toString()).isValid()) accents << u"#"_s + v.toString();
    }
    const bool same = p == m_primary && s == m_secondary && t == m_tertiary && accents == m_accents;
    m_accents = accents;
    if (!same) emit changed();
}
