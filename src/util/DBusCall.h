#pragma once
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

// What the app's mirrors of a system service's objects over D-Bus share
// (BluetoothManager's of BlueZ, WifiManager's of NetworkManager): the
// service followed as it comes and goes, its objects' properties kept as
// its signals change them, D-Bus's values made plain Qt, and calls answered
// once.
namespace dbuscall {

// The interfaces such a service's objects have besides their own: their
// properties (Get, Set, PropertiesChanged), and, on its root, its objects
// (GetManagedObjects, InterfacesAdded, InterfacesRemoved).
inline const QString kProperties = QStringLiteral("org.freedesktop.DBus.Properties");
inline const QString kObjectManager = QStringLiteral("org.freedesktop.DBus.ObjectManager");

// How long a call waits for its answer, unless it takes longer by its nature
// (pairing, joining a network).
constexpr int kCallTimeoutMs = 10 * 1000;

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

// The service on the bus followed: onUp each time it comes, and at once if
// it is there already (a desktop's, or the app started again: asking for it
// before it is there would start it, by D-Bus activation), onDown each time
// it goes. Called once its signals are connected, so that nothing it says
// after the first look at its objects is missed. The watcher is context's.
inline QDBusServiceWatcher *watchService(const QDBusConnection &bus, const QString &service, QObject *context,
                                         std::function<void()> onUp, std::function<void()> onDown) {
    auto *watcher = new QDBusServiceWatcher(service, bus,
        QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, context);
    QObject::connect(watcher, &QDBusServiceWatcher::serviceRegistered, context, [onUp] { onUp(); });
    QObject::connect(watcher, &QDBusServiceWatcher::serviceUnregistered, context, [onDown] { onDown(); });
    if (bus.interface() && bus.interface()->isServiceRegistered(service))
        onUp();
    return watcher;
}

inline QVariant plain(const QVariant &value);

// A D-Bus value with what QtDBus leaves wrapped (a container stays a
// QDBusArgument, to be read once) made plain Qt: a map a QVariantMap, an
// array or a structure a QVariantList, an object path a string. Bytes stay
// bytes (an SSID) and strings a QStringList.
inline QVariant plainArgument(const QDBusArgument &arg) {
    switch (arg.currentType()) {
    case QDBusArgument::BasicType:
    case QDBusArgument::VariantType:
        return plain(arg.asVariant());
    case QDBusArgument::ArrayType: {
        QVariantList list;
        arg.beginArray();
        while (!arg.atEnd())
            list.append(plainArgument(arg));
        arg.endArray();
        return list;
    }
    case QDBusArgument::StructureType: {
        QVariantList fields;
        arg.beginStructure();
        while (!arg.atEnd())
            fields.append(plainArgument(arg));
        arg.endStructure();
        return fields;
    }
    case QDBusArgument::MapType: {
        QVariantMap map;
        arg.beginMap();
        while (!arg.atEnd()) {
            arg.beginMapEntry();
            const QString key = plainArgument(arg).toString();
            map.insert(key, plainArgument(arg));
            arg.endMapEntry();
        }
        arg.endMap();
        return map;
    }
    default:
        return QVariant();
    }
}

inline QVariant plain(const QVariant &value) {
    const int type = value.userType();
    if (type == qMetaTypeId<QDBusArgument>())
        return plainArgument(value.value<QDBusArgument>());
    if (type == qMetaTypeId<QDBusVariant>())
        return plain(value.value<QDBusVariant>().variant());
    if (type == qMetaTypeId<QDBusObjectPath>())
        return value.value<QDBusObjectPath>().path();
    return value;
}

// An object path, "" for none ("/").
inline QString pathOf(const QVariant &value) {
    const QString path = value.toString();
    return path == QLatin1String("/") ? QString() : path;
}

// An object's properties as a PropertiesChanged signal leaves them: the
// changed ones set, the invalidated ones (changed, their values not sent)
// dropped. True when one of `watched` was among them.
inline bool mergeProperties(QVariantMap &properties, const QVariantMap &changed, const QStringList &invalidated,
                            const QStringList &watched = {}) {
    bool seen = false;
    for (auto it = changed.cbegin(); it != changed.cend(); ++it) {
        properties.insert(it.key(), it.value());
        seen = seen || watched.contains(it.key());
    }
    for (const QString &key : invalidated) {
        properties.remove(key);
        seen = seen || watched.contains(key);
    }
    return seen;
}

} // namespace dbuscall
