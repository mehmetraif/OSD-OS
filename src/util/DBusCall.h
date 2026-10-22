#pragma once
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <functional>

namespace dbuscall {

// A method call on a bus, answered to done once, on context's thread, with
// the reply: an error message when the call failed or timed out. Nothing is
// answered after context is gone.
inline void call(const QDBusConnection &bus, const QString &service, const QString &path,
                 const QString &interface, const QString &method, const QVariantList &args,
                 int timeoutMs, QObject *context,
                 std::function<void(const QDBusMessage &reply)> done) {
    QDBusMessage message = QDBusMessage::createMethodCall(service, path, interface, method);
    message.setArguments(args);
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(message, timeoutMs), context);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, context,
                     [done](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (done)
            done(w->reply());
    });
}

} // namespace dbuscall
