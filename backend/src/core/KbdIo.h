#pragma once

#include "Paths.h"

#include <QList>
#include <QObject>
#include <QStringList>

// The keyboard light's reads and writes, on a thread of their own (KeyboardController owns it): a light's file can
// go into its driver, and the fan loop must never wait for one (writes measured at ~0.1 ms on the dev machine's
// hp-wmi light; other drivers weren't measured). Everything runs in the order it was asked, so a read asked after a
// write sees it. Paths only ever come from detection.
class KbdIo : public QObject
{
    Q_OBJECT
public:
    explicit KbdIo(const Paths &paths) : m_paths(paths) {}

public slots:
    void scan(quint64 tag);                                        // -> scanned (KeyboardScan)
    // path, value, path, value, ...: each written as "value\n" (as awk's print; the file is opened without creating
    // it: nothing is ever made in /sys). The first failure stops the batch.
    void write(const QStringList &pathsAndValues, quint64 tag);    // -> written ("" or why)
    void read(const QStringList &files, quint64 tag);              // -> readDone (contents, read or not)

signals:
    void scanned(quint64 tag, const QString &text);
    void written(quint64 tag, const QString &error);
    void readDone(quint64 tag, const QStringList &contents, const QList<bool> &ok);

private:
    Paths m_paths;
};
