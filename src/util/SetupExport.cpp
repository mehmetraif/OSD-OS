#include "util/SetupExport.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <qrcodegen.hpp>

namespace setupexport {

namespace {

// A manifest's or the page's default, as config.json may keep it: a
// toggle's "ON" or "OFF" as true or false, the page's true or false as
// the app's "On" or "Off".
bool sameValue(const QJsonValue &value, const QJsonValue &fallback) {
    if (fallback.isNull() || fallback.isUndefined())
        return false;
    const auto word = [](const QJsonValue &v) -> QString {
        if (v.isBool())
            return v.toBool() ? QStringLiteral("on") : QStringLiteral("off");
        return v.isString() ? v.toString().toLower() : QString();
    };
    if ((value.isBool() || value.isString()) && (fallback.isBool() || fallback.isString()))
        return word(value) == word(fallback);
    if (value.isDouble() && fallback.isDouble())
        return value.toDouble() == fallback.toDouble();
    return value == fallback;
}

// A list row's first value, the default where the row names none.
QJsonValue firstOption(const QJsonArray &options) {
    if (options.isEmpty())
        return QJsonValue();
    const QJsonValue first = options.first();
    return first.isArray() ? first.toArray().at(1) : first;
}

// A module's rows as build-site.py's module_settings() keeps them: those
// whose values are known without the device, submenus' rows flat under the
// module, and the page's own rows for it.
void walkRows(const QJsonArray &rows, const QJsonObject &extra, QHash<QString, QJsonValue> &out) {
    const QJsonObject options = extra.value(QLatin1String("options")).toObject();
    const QJsonObject defaults = extra.value(QLatin1String("defaults")).toObject();
    for (const QJsonValue &v : rows) {
        const QJsonObject row = v.toObject();
        const QString key = row.value(QLatin1String("key")).toString();
        const QString type = row.value(QLatin1String("type")).toString();
        if (type == QLatin1String("submenu")) {
            walkRows(row.value(QLatin1String("settings")).toArray(), extra, out);
            continue;
        }
        if (key.isEmpty())
            continue;
        if (key == QLatin1String("enabled")) {
            out.insert(key, row.value(QLatin1String("default")).toString().toUpper() == QLatin1String("ON"));
            continue;
        }
        if (type == QLatin1String("toggle")) {
            out.insert(key, row.value(QLatin1String("default")).toString(QStringLiteral("OFF")).toUpper()
                                == QLatin1String("ON"));
        } else if (type == QLatin1String("list_single")) {
            QJsonArray known = row.value(QLatin1String("options")).toArray();
            if (known.isEmpty())
                known = options.value(key).toArray();
            // The language lists (options "languages") are the page's own.
            if (known.isEmpty() && row.value(QLatin1String("options")).toString() != QLatin1String("languages")
                && !options.value(key).isString())
                continue;
            out.insert(key, row.contains(QLatin1String("default")) ? row.value(QLatin1String("default"))
                            : defaults.contains(key)                ? defaults.value(key)
                                                                    : firstOption(known));
        } else if (type == QLatin1String("multiselect_submenu")) {
            if (options.contains(key))
                out.insert(key, QJsonValue());
        } else if (type == QLatin1String("slider")) {
            out.insert(key, row.value(QLatin1String("default")).toDouble(0));
        } else if (type == QLatin1String("directory_browser")) {
            out.insert(key, row.value(QLatin1String("default")).toString());
        }
    }
}

// Whether a line of authorized_keys is a public key the card's setup takes
// (public_key() there): a type, then its key.
bool publicKey(const QString &line) {
    static const QRegularExpression kKey(
        QStringLiteral("^(ssh-(rsa|dss|ed25519)|ecdsa-sha2-nistp(256|384|521)|sk-(ssh-ed25519|ecdsa-sha2-nistp256)@openssh\\.com)"
                       "\\s+[A-Za-z0-9+/]+={0,3}(\\s.*)?$"));
    return kKey.match(line).hasMatch();
}

QByteArray readText(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.read(256 * 1024) : QByteArray();
}

} // namespace

Form formOf(const QJsonObject &extra, const QHash<QString, QJsonObject> &manifests) {
    Form form;
    for (const QJsonValue &group : extra.value(QLatin1String("app")).toArray()) {
        for (const QJsonValue &v : group.toObject().value(QLatin1String("settings")).toArray()) {
            const QJsonObject row = v.toObject();
            const QString key = row.value(QLatin1String("key")).toString();
            if (!key.isEmpty())
                form.app.insert(key, row.contains(QLatin1String("default")) ? row.value(QLatin1String("default"))
                                                                            : QJsonValue());
        }
    }
    const QJsonObject extraModules = extra.value(QLatin1String("modules")).toObject();
    for (auto it = manifests.constBegin(); it != manifests.constEnd(); ++it) {
        const QJsonObject moduleExtra = extraModules.value(it.key()).toObject();
        QHash<QString, QJsonValue> rows;
        walkRows(it.value().value(QLatin1String("settings")).toArray(), moduleExtra, rows);
        if (!rows.contains(QStringLiteral("enabled")))
            rows.insert(QStringLiteral("enabled"), QJsonValue());
        for (const QJsonValue &v : moduleExtra.value(QLatin1String("extra")).toArray()) {
            const QJsonObject row = v.toObject();
            const QString key = row.value(QLatin1String("key")).toString();
            if (!key.isEmpty())
                rows.insert(key, row.contains(QLatin1String("default")) ? row.value(QLatin1String("default"))
                                                                        : QJsonValue());
        }
        form.modules.insert(it.key(), rows);
    }
    return form;
}

void readSystem(System &system, const QString &root, const QString &home) {
    // The time zone: /etc/localtime's link into zoneinfo, as timedatectl
    // sets it, else /etc/timezone.
    static const QRegularExpression kZone(QStringLiteral("^[A-Za-z0-9_+-]+(/[A-Za-z0-9_+-]+)*$"));
    const QString link = QFileInfo(root + QStringLiteral("/etc/localtime")).symLinkTarget();
    const qsizetype at = link.indexOf(QLatin1String("/zoneinfo/"));
    QString zone = at >= 0 ? link.mid(at + 10) : QString();
    if (zone.isEmpty())
        zone = QString::fromUtf8(readText(root + QStringLiteral("/etc/timezone"))).trimmed();
    if (kZone.match(zone).hasMatch() && !zone.contains(QLatin1String("..")))
        system.timezone = zone;

    // The keyboard: the first of XKBLAYOUT's layouts, as raspi-config sets it.
    static const QRegularExpression kLayout(QStringLiteral("^\\s*XKBLAYOUT\\s*=\\s*\"?([a-z]{2,5})"),
                                            QRegularExpression::MultilineOption);
    const QRegularExpressionMatch layout =
        kLayout.match(QString::fromUtf8(readText(root + QStringLiteral("/etc/default/keyboard"))));
    if (layout.hasMatch())
        system.keyboard = layout.captured(1);

    // SSH, where it is there to be turned on: on when systemd starts it,
    // password login as the card's setup said (its own drop-in), and the
    // keys that log in.
    const bool installed = QFileInfo::exists(root + QStringLiteral("/usr/lib/systemd/system/ssh.service"))
                           || QFileInfo::exists(root + QStringLiteral("/lib/systemd/system/ssh.service"));
    if (installed) {
        const QFileInfo wanted(root + QStringLiteral("/etc/systemd/system/multi-user.target.wants/ssh.service"));
        system.ssh = wanted.exists() || wanted.isSymLink() ? 1 : 0;
        static const QRegularExpression kPasswords(QStringLiteral("^\\s*PasswordAuthentication\\s+yes\\b"),
                                                   QRegularExpression::MultilineOption
                                                       | QRegularExpression::CaseInsensitiveOption);
        system.sshPasswordLogin = kPasswords.match(QString::fromUtf8(
            readText(root + QStringLiteral("/etc/ssh/sshd_config.d/00-osdos-setup.conf")))).hasMatch();
        const QStringList lines = QString::fromUtf8(readText(home + QStringLiteral("/.ssh/authorized_keys")))
                                      .split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            const QString key = line.trimmed();
            if (publicKey(key) && !system.sshKeys.contains(key))
                system.sshKeys << key;
        }
    }
}

QJsonObject build(const QJsonObject &config, const Form &form, const QJsonObject &account, const System &system) {
    QJsonObject setup{ { QStringLiteral("osdos_setup"), 1 } };

    QJsonObject device;
    if (!system.name.isEmpty())
        device.insert(QStringLiteral("name"), system.name);
    // The board with the output alone, as the page has them.
    if (!system.preset.isEmpty() && !system.model.isEmpty())
        device.insert(QStringLiteral("model"), system.model);
    if (!device.isEmpty())
        setup.insert(QStringLiteral("device"), device);
    if (!system.preset.isEmpty())
        setup.insert(QStringLiteral("display"), QJsonObject{ { QStringLiteral("preset"), system.preset } });

    QJsonObject wifi;
    if (!system.wifiCountry.isEmpty())
        wifi.insert(QStringLiteral("country"), system.wifiCountry);
    if (!system.ssid.isEmpty()) {
        wifi.insert(QStringLiteral("ssid"), system.ssid);
        wifi.insert(QStringLiteral("password"), system.wifiPassword);
        wifi.insert(QStringLiteral("hidden"), system.wifiHidden);
    }
    if (!wifi.isEmpty())
        setup.insert(QStringLiteral("wifi"), wifi);

    QJsonObject sys;
    if (system.ssh >= 0) {
        QJsonObject ssh{ { QStringLiteral("enabled"), system.ssh == 1 },
                         { QStringLiteral("password_login"), system.sshPasswordLogin } };
        if (system.ssh == 1 && !system.sshKeys.isEmpty())
            ssh.insert(QStringLiteral("authorized_keys"), QJsonArray::fromStringList(system.sshKeys));
        sys.insert(QStringLiteral("ssh"), ssh);
    }
    if (!system.timezone.isEmpty())
        sys.insert(QStringLiteral("timezone"), system.timezone);
    if (!system.keyboard.isEmpty())
        sys.insert(QStringLiteral("keyboard"), system.keyboard);
    if (!sys.isEmpty())
        setup.insert(QStringLiteral("system"), sys);

    QJsonObject ra;
    for (const char *key : { "user", "token", "key" }) {
        const QString value = account.value(QLatin1String(key)).toString().trimmed();
        if (!value.isEmpty())
            ra.insert(QLatin1String(key), value);
    }
    if (!ra.isEmpty())
        setup.insert(QStringLiteral("retroachievements"), ra);

    // The settings on the form, those at their defaults left out.
    QJsonObject app;
    const QJsonObject savedApp = config.value(QLatin1String("app")).toObject();
    for (auto it = savedApp.constBegin(); it != savedApp.constEnd(); ++it) {
        const auto row = form.app.constFind(it.key());
        if (row != form.app.constEnd() && !sameValue(it.value(), row.value()))
            app.insert(it.key(), it.value());
    }
    QJsonObject modules;
    const QJsonObject savedModules = config.value(QLatin1String("modules")).toObject();
    for (auto m = savedModules.constBegin(); m != savedModules.constEnd(); ++m) {
        const auto rows = form.modules.constFind(m.key());
        if (rows == form.modules.constEnd())
            continue;
        QJsonObject settings;
        const QJsonObject saved = m.value().toObject();
        for (auto it = saved.constBegin(); it != saved.constEnd(); ++it) {
            const auto row = rows->constFind(it.key());
            if (row != rows->constEnd() && !sameValue(it.value(), row.value()))
                settings.insert(it.key(), it.value());
        }
        if (!settings.isEmpty())
            modules.insert(m.key(), settings);
    }
    QJsonObject settings;
    if (!app.isEmpty())
        settings.insert(QStringLiteral("app"), app);
    if (!modules.isEmpty())
        settings.insert(QStringLiteral("modules"), modules);
    if (!settings.isEmpty())
        setup.insert(QStringLiteral("config"), settings);
    return setup;
}

int capacity(int version) {
    // ISO/IEC 18004's table: bytes at error correction L, versions 1 to 40.
    static const int kBytes[40] = { 17,   32,   53,   78,   106,  134,  154,  192,  230,  271,
                                    321,  367,  425,  458,  520,  586,  644,  718,  792,  858,
                                    929,  1003, 1091, 1171, 1273, 1367, 1465, 1528, 1628, 1732,
                                    1840, 1952, 2068, 2188, 2303, 2431, 2563, 2699, 2809, 2953 };
    return version >= 1 && version <= 40 ? kBytes[version - 1] : 0;
}

QStringList links(const QJsonObject &setup, int maxBytes) {
    // zlib's own stream, as a browser's DecompressionStream("deflate")
    // reads it: qCompress()'s, less the length it puts first.
    const QByteArray packed = qCompress(QJsonDocument(setup).toJson(QJsonDocument::Compact), 9).mid(4);
    const QString text = QString::fromLatin1(
        packed.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    const QString id = QString::fromLatin1(QCryptographicHash::hash(packed, QCryptographicHash::Sha1).toHex().left(6));
    const auto head = [&id](int part, int parts) {
        return kPage + QStringLiteral("#s=%1.%2.%3.").arg(part).arg(parts).arg(id);
    };
    // As many parts as it takes, each with its head: more parts may make
    // the head longer, so counted again until they hold it.
    int parts = 1;
    for (;;) {
        const int room = maxBytes - int(head(parts, parts).size());
        if (room <= 0)
            return {};
        const int needed = std::max<int>(1, int((text.size() + room - 1) / room));
        if (needed <= parts)
            break;
        parts = needed;
    }
    const int per = int((text.size() + parts - 1) / parts);
    QStringList out;
    for (int i = 0; i < parts; ++i)
        out << head(i + 1, parts) + text.mid(i * per, per);
    return out;
}

QImage codeImage(const QString &text, int quiet) {
    try {
        const QByteArray utf8 = text.toUtf8();
        const qrcodegen::QrCode code = qrcodegen::QrCode::encodeText(utf8.constData(), qrcodegen::QrCode::Ecc::LOW);
        const int size = code.getSize();
        QImage image(size + 2 * quiet, size + 2 * quiet, QImage::Format_Grayscale8);
        image.fill(255);
        for (int y = 0; y < size; ++y) {
            uchar *line = image.scanLine(y + quiet);
            for (int x = 0; x < size; ++x) {
                if (code.getModule(x, y))
                    line[x + quiet] = 0;
            }
        }
        return image;
    } catch (const std::exception &) {
        return QImage();
    }
}

} // namespace setupexport
