#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QObject>
#include <QStringList>

// The colours of Caelestia's current scheme the keyboard's "theme" source uses (services/Colours.qml, only read:
// ~/.local/state/caelestia/scheme.json -> colours; Caelestia's files are never written). Followed as the QML's
// FileView did: edits, a replace by rename, a delete and a re-create. A file that can't be read or parsed keeps the
// colours from before; until one is read, Colours.qml's defaults.
class CaelestiaScheme : public QObject
{
    Q_OBJECT
public:
    explicit CaelestiaScheme(const QString &path, QObject *parent = nullptr);

    QColor primary() const { return m_primary; }
    QColor secondary() const { return m_secondary; }
    QColor tertiary() const { return m_tertiary; }
    QStringList accents() const { return m_accents; }   // "#rrggbb", in Colours.qml's accentKeys order

signals:
    void changed();

private:
    void load();
    void watch();

    QString m_path;
    QFileSystemWatcher m_watcher;
    QColor m_primary{ QColor::fromString(u"#d0bcff") }, m_secondary{ QColor::fromString(u"#ccc2dc") },
        m_tertiary{ QColor::fromString(u"#efb8c8") };
    QStringList m_accents;
};
