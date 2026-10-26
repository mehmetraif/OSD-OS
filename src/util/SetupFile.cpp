#include "SetupFile.h"
#include "AtomicFile.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>
#include <cmath>

namespace setupfile {
namespace {

// The card's MAX_SIZE and SETUP_VERSION.
constexpr qint64 kMaxSize = 1024 * 1024;
constexpr double kVersion = 1;

// Anywhere in the file, a number JSON can't hold (1e400, read as infinity).
bool finite(const QJsonValue &value) {
    if (value.isDouble())
        return std::isfinite(value.toDouble());
    if (value.isArray()) {
        for (const QJsonValue &item : value.toArray()) {
            if (!finite(item))
                return false;
        }
    } else if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (!finite(it.value()))
                return false;
        }
    }
    return true;
}

QString text(const QJsonObject &object, const char *key) {
    const QJsonValue value = object.value(QLatin1String(key));
    return value.isString() ? value.toString() : QString();
}

// The card's retroachievements(): the user with a password, the key alone,
// or both; anything else of the part left out.
QJsonObject account(const QJsonObject &ra) {
    static const QRegularExpression kKey(QStringLiteral("^[A-Za-z0-9]{1,64}$"));
    QJsonObject out;
    const QString user = text(ra, "user");
    const QString password = text(ra, "password");
    if (!user.trimmed().isEmpty() && !password.isEmpty() && user.size() <= 64 && password.size() <= 256) {
        out[QStringLiteral("user")] = user.trimmed();
        out[QStringLiteral("password")] = password;
    }
    const QString key = text(ra, "key").trimmed();
    if (kKey.match(key).hasMatch())
        out[QStringLiteral("key")] = key;
    return out;
}

// What of the file a card alone sets, as its system stage does.
QStringList cardOnly(const QJsonObject &setup) {
    const QJsonObject device = setup.value(QLatin1String("device")).toObject();
    const QJsonObject display = setup.value(QLatin1String("display")).toObject();
    const QJsonObject wifi = setup.value(QLatin1String("wifi")).toObject();
    const QJsonObject system = setup.value(QLatin1String("system")).toObject();
    QStringList parts;
    if (!text(device, "name").isEmpty())
        parts << QStringLiteral("the device's name");
    if (!text(display, "preset").isEmpty())
        parts << QStringLiteral("the display output");
    if (!text(wifi, "ssid").isEmpty() || !text(wifi, "country").isEmpty())
        parts << QStringLiteral("Wi-Fi");
    if (!text(system, "password_hash").isEmpty())
        parts << QStringLiteral("the password");
    if (system.value(QLatin1String("ssh")).isObject())
        parts << QStringLiteral("SSH");
    if (!text(system, "timezone").isEmpty())
        parts << QStringLiteral("the time zone");
    if (!text(system, "keyboard").isEmpty())
        parts << QStringLiteral("the keyboard");
    return parts;
}

// config.json as it is, an empty one when there is none; false when it is
// there but can't be read, and must not be written over.
bool readConfig(const QString &path, QJsonObject *config) {
    QFile file(path);
    if (!file.exists()) {
        *config = QJsonObject();
        return true;
    }
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    *config = doc.object();
    return true;
}

} // namespace

bool parse(const QByteArray &data, Setup *setup, QString *why) {
    const auto refuse = [why](const QString &reason) {
        if (why)
            *why = reason;
        return false;
    };
    if (data.size() > kMaxSize)
        return refuse(QStringLiteral("bigger than a settings file is"));
    QByteArray json = data;
    if (json.startsWith("\xEF\xBB\xBF"))
        json.remove(0, 3);
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError)
        return refuse(QStringLiteral("not JSON"));
    const QJsonObject file = doc.object();
    if (!doc.isObject() || !file.contains(QLatin1String("osdos_setup")))
        return refuse(QStringLiteral("not OSD/OS Setup's settings"));
    if (!finite(file))
        return refuse(QStringLiteral("not JSON: a number JSON can't hold"));
    const QJsonValue version = file.value(QLatin1String("osdos_setup"));
    if (!version.isDouble())
        return refuse(QStringLiteral("not OSD/OS Setup's settings"));
    if (version.toDouble() != kVersion) {
        const double number = version.toDouble();
        if (number == std::floor(number))
            return refuse(QStringLiteral("of version %1, which this OSD/OS doesn't know").arg(qint64(number)));
        return refuse(QStringLiteral("not OSD/OS Setup's settings"));
    }
    Setup out;
    const QJsonObject config = file.value(QLatin1String("config")).toObject();
    for (const char *part : { "app", "modules" }) {
        const QJsonValue value = config.value(QLatin1String(part));
        if (value.isObject())
            out.config[QLatin1String(part)] = value;
    }
    out.account = account(file.value(QLatin1String("retroachievements")).toObject());
    out.cardOnly = cardOnly(file);
    *setup = out;
    return true;
}

QJsonObject merged(const QJsonObject &into, const QJsonObject &update) {
    QJsonObject out = into;
    for (auto it = update.begin(); it != update.end(); ++it) {
        const QJsonValue there = out.value(it.key());
        out[it.key()] = it.value().isObject() && there.isObject()
                            ? QJsonValue(merged(there.toObject(), it.value().toObject()))
                            : it.value();
    }
    return out;
}

int count(const QJsonObject &values) {
    int n = 0;
    for (auto it = values.begin(); it != values.end(); ++it)
        n += it.value().isObject() ? count(it.value().toObject()) : 1;
    return n;
}

bool writeAccount(const QJsonObject &account, const QString &dataRoot, QString *why) {
    const QString folder = dataRoot + QStringLiteral("/arcade");
    if (!QDir().mkpath(folder)) {
        if (why)
            *why = QStringLiteral("its folder couldn't be made");
        return false;
    }
    if (!writeFileAtomically(folder + QStringLiteral("/retroachievements-setup.json"),
                             QJsonDocument(account).toJson(QJsonDocument::Compact) + '\n',
                             QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        if (why)
            *why = QStringLiteral("it couldn't be written");
        return false;
    }
    return true;
}

QString settingsLine(int settings) {
    return QStringLiteral("OSD/OS settings: %1 set").arg(settings);
}

QString accountLine(const QJsonObject &account) {
    QStringList parts;
    if (account.contains(QLatin1String("user")))
        parts << account.value(QLatin1String("user")).toString() + QStringLiteral(", signed in with the password");
    if (account.contains(QLatin1String("key")))
        parts << QStringLiteral("Web API key kept");
    return QStringLiteral("RetroAchievements: ") + parts.join(QStringLiteral("; "));
}

QString cardOnlyLine(const QStringList &parts) {
    QString list = parts.value(0);
    if (parts.size() > 1)
        list = parts.mid(0, parts.size() - 1).join(QStringLiteral(", ")) + QStringLiteral(" and ") + parts.last();
    return QStringLiteral("Not set here: %1, which an OSD/OS card alone sets").arg(list);
}

QString noticeTitle(const Result &result) {
    if (result.lines.isEmpty())
        return QString();
    if (result.applied == 0)
        return result.ok ? QStringLiteral("Nothing set from OSD/OS Setup") : QStringLiteral("OSD/OS Setup not applied");
    return result.ok ? QStringLiteral("OSD/OS Setup applied") : QStringLiteral("OSD/OS Setup partly applied");
}

QStringList places(const QString &dataRoot) {
    QStringList folders{ dataRoot };
#if defined(Q_OS_WIN)
    folders << QCoreApplication::applicationDirPath();
#elif defined(Q_OS_MACOS)
    const QString binDir = QCoreApplication::applicationDirPath();
    if (binDir.endsWith(QStringLiteral(".app/Contents/MacOS")))
        folders << QFileInfo(binDir + QStringLiteral("/../../..")).absoluteFilePath();
#else
    if (qEnvironmentVariableIsSet("APPIMAGE"))
        folders << QFileInfo(qEnvironmentVariable("APPIMAGE")).absolutePath();
#endif
    QStringList out;
    for (const QString &folder : std::as_const(folders)) {
        const QString path = QDir::cleanPath(QDir(folder).absoluteFilePath(kFileName));
        if (!out.contains(path, Qt::CaseInsensitive))
            out << path;
    }
    return out;
}

Result applyAtStart(const QString &dataRoot) {
    Result result;
    const auto say = [&result](const QString &line, bool ok = true) {
        result.lines << line;
        result.ok = result.ok && ok;
        if (ok)
            qInfo("[Setup] %s", qPrintable(line));
        else
            qWarning("[Setup] %s", qPrintable(line));
    };
    const QStringList found = places(dataRoot);
    for (int i = 0; i < found.size(); ++i) {
        QFile file(found.at(i));
        if (!file.exists())
            continue;
        // Named by where it was put, short enough for a TV; the log has its
        // path.
        qInfo("[Setup] %s", qPrintable(QDir::toNativeSeparators(found.at(i))));
        const QString name = i == 0 ? QStringLiteral("osdos-setup.json in the data folder")
                                    : QStringLiteral("osdos-setup.json beside the app");
        if (!file.open(QIODevice::ReadOnly)) {
            say(QStringLiteral("%1 couldn't be read: %2").arg(name, file.errorString()), false);
            continue;
        }
        const QByteArray data = file.read(kMaxSize + 1);
        file.close();
        // Off the disk first, with its passwords, so that nothing of it is
        // set twice: one that stays would be applied at every start.
        if (!file.remove()) {
            say(QStringLiteral("%1 couldn't be deleted, so it isn't applied, as it would be at every start: %2")
                    .arg(name, file.errorString()), false);
            continue;
        }
        Setup setup;
        QString why;
        if (!parse(data, &setup, &why)) {
            say(QStringLiteral("%1 deleted, not applied: %2").arg(name, why), false);
            continue;
        }
        say(QStringLiteral("From %1").arg(name));
        if (!setup.config.isEmpty()) {
            const QString configPath = dataRoot + QStringLiteral("/config.json");
            QJsonObject config;
            if (!readConfig(configPath, &config))
                say(QStringLiteral("OSD/OS settings: not set: config.json couldn't be read, and is left as it is"), false);
            else if (!writeFileAtomically(configPath, QJsonDocument(merged(config, setup.config)).toJson(QJsonDocument::Indented)))
                say(QStringLiteral("OSD/OS settings: not set: config.json couldn't be written"), false);
            else {
                say(settingsLine(count(setup.config)));
                ++result.applied;
            }
        }
        if (!setup.account.isEmpty()) {
            if (writeAccount(setup.account, dataRoot, &why)) {
                say(accountLine(setup.account));
                ++result.applied;
            } else {
                say(QStringLiteral("RetroAchievements: not set: ") + why, false);
            }
        }
        if (!setup.cardOnly.isEmpty())
            say(cardOnlyLine(setup.cardOnly));
        if (setup.config.isEmpty() && setup.account.isEmpty() && setup.cardOnly.isEmpty())
            say(QStringLiteral("Nothing in it to set"));
    }
    return result;
}

} // namespace setupfile
