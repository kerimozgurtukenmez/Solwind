#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QString>

#include <functional>

// Desktop notifications (SPEC.md NT-1), sent the way `notify-send -a Solwind -u <urgency> -i <icon> <title> <body>`
// (libnotify 0.8.9) sends them - measured on a private bus: Notify("Solwind", 0, "", title, body, [],
// {image-path: icon, urgency: byte, sender-pid: int64}, -1). The call doesn't wait for the answer.
class Notifier : public QObject
{
    Q_OBJECT
public:
    enum Urgency { Low = 0, Normal = 1, Critical = 2 };

    explicit Notifier(QDBusConnection bus, QObject *parent = nullptr);

    // Notifications on (settings notify.enabled); read at every send
    std::function<bool()> enabled;

    // `always`: also with notifications off - only for a warning the user can't see anywhere else and must act on.
    // `wait`: wait (at most 2 s) until the server has it - for a warning sent just before the daemon quits
    void send(const QString &title, const QString &body, Urgency urgency, const QString &icon, bool always = false,
              bool wait = false);

signals:
    void sent(const QString &title, const QString &body, int urgency, const QString &icon);   // (for tests and logs)

private:
    QDBusConnection m_bus;
    bool m_warned = false;
};
