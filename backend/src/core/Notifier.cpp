#include "Notifier.h"

#include <QCoreApplication>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QVariantMap>

using namespace Qt::StringLiterals;

Notifier::Notifier(QDBusConnection bus, QObject *parent) : QObject(parent), m_bus(bus) {}

void Notifier::send(const QString &title, const QString &body, Urgency urgency, const QString &icon, bool always, bool wait)
{
    if (!always && enabled && !enabled()) return;
    QDBusMessage m = QDBusMessage::createMethodCall(u"org.freedesktop.Notifications"_s, u"/org/freedesktop/Notifications"_s,
                                                    u"org.freedesktop.Notifications"_s, u"Notify"_s);
    const QVariantMap hints{ { u"image-path"_s, icon.isEmpty() ? u"dialog-information"_s : icon },
                             { u"urgency"_s, QVariant::fromValue(uchar(urgency)) },
                             { u"sender-pid"_s, QVariant::fromValue(qint64(QCoreApplication::applicationPid())) } };
    m << u"Solwind"_s << uint(0) << QString() << title << body << QStringList() << hints << int(-1);
    qInfo("[solwind] notification: %s%s%s", qPrintable(title), body.isEmpty() ? "" : " - ", qPrintable(body));
    emit sent(title, body, urgency, icon);
    if (wait) {
        const QDBusMessage r = m_bus.call(m, QDBus::Block, 2000);
        if (r.type() == QDBusMessage::ErrorMessage) qWarning("solwindd: could not send a notification: %s", qPrintable(r.errorMessage()));
        return;
    }
    // Not waited for (notify-send ran detached in the QML app); a missing notification server is logged once
    auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(m), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        if (call->isError() && !m_warned) {
            m_warned = true;
            qWarning("solwindd: could not send a notification: %s", qPrintable(call->error().message()));
        } else if (!call->isError()) {
            m_warned = false;
        }
        call->deleteLater();
    });
}
