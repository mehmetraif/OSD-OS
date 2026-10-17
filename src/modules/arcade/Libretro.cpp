#include "Libretro.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <QVersionNumber>
#include <QXmlStreamReader>
#include <algorithm>

namespace libretro {

namespace {

#ifdef Q_OS_MACOS
const QString kCoreFile = QStringLiteral("fbneo_libretro.dylib");
#else
const QString kCoreFile = QStringLiteral("fbneo_libretro.so");
#endif

QString firstFile(const QStringList &candidates) {
    for (const QString &candidate : candidates) {
        if (QFileInfo(candidate).isFile())
            return candidate;
    }
    return QString();
}

// A config's value, in its quotes: RetroArch's parser takes a line's value
// to the next quote, so none in it, nor a line's end.
QString quoted(QString value) {
    value.remove(QLatin1Char('"'));
    value.remove(QLatin1Char('\n'));
    value.remove(QLatin1Char('\r'));
    return QLatin1Char('"') + value + QLatin1Char('"');
}

// The lobby's oldest RetroArch the menu lists rooms of.
const QVersionNumber kOldestRetroArch(1, 9, 1);

// A name from the lobby, as whoever hosts wrote it: no markup or control
// characters (a view may take '<' for rich text), and not too long.
QString cleaned(const QString &text, int length) {
    QString out;
    for (const QChar c : text) {
        if (c.isPrint() && c != QLatin1Char('<') && c != QLatin1Char('>') && c != QLatin1Char('&'))
            out += c;
    }
    return out.trimmed().left(length);
}

// The lobby's address for a room, "address|port[|session]" as --connect
// takes it, or "" for one that isn't an address, a port and a session.
QString joinAddressOf(const QString &address, int port, const QString &session) {
    static const QRegularExpression host(QStringLiteral("^[0-9A-Za-z.:-]{1,64}$"));
    static const QRegularExpression token(QStringLiteral("^[0-9A-Za-z+/=_-]{0,128}$"));
    if (!host.match(address).hasMatch() || port <= 0 || port > 65535 || !token.match(session).hasMatch())
        return QString();
    return session.isEmpty() ? QStringLiteral("%1|%2").arg(address).arg(port)
                             : QStringLiteral("%1|%2|%3").arg(address).arg(port).arg(session);
}

} // namespace

QString programPath(const QString &dataRoot) {
    const QFileInfo dropIn(dataRoot + QStringLiteral("/bin/retroarch"));
    if (dropIn.isFile() && dropIn.isExecutable())
        return dropIn.absoluteFilePath();
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("retroarch"));
    if (!onPath.isEmpty())
        return onPath;
    for (const char *candidate : { "/usr/bin/retroarch", "/Applications/RetroArch.app/Contents/MacOS/RetroArch" }) {
        const QFileInfo program{ QString::fromLatin1(candidate) };
        if (program.isFile() && program.isExecutable())
            return program.absoluteFilePath();
    }
    return QString();
}

QString downloadedCore(const QString &dataRoot) {
    return dataRoot + QStringLiteral("/arcade/retroarch/cores/") + kCoreFile;
}

QString downloadedDat(const QString &dataRoot) {
    return dataRoot + QStringLiteral("/arcade/retroarch/fbneo-arcade.dat");
}

QString downloadedLicense(const QString &dataRoot) {
    return dataRoot + QStringLiteral("/arcade/retroarch/fbneo-license.txt");
}

QString corePath(const QString &dataRoot) {
    const QString home = QDir::homePath();
    return firstFile({ downloadedCore(dataRoot),
                       QStringLiteral("/usr/local/lib/libretro/") + kCoreFile,
                       QStringLiteral("/usr/lib/aarch64-linux-gnu/libretro/") + kCoreFile,
                       QStringLiteral("/usr/lib/x86_64-linux-gnu/libretro/") + kCoreFile,
                       QStringLiteral("/usr/lib/libretro/") + kCoreFile,
                       home + QStringLiteral("/.config/retroarch/cores/") + kCoreFile,
                       home + QStringLiteral("/Library/Application Support/RetroArch/cores/") + kCoreFile });
}

QString datPath(const QString &dataRoot) {
    return firstFile({ downloadedDat(dataRoot),
                       QStringLiteral("/usr/local/share/libretro/fbneo/fbneo-arcade.dat") });
}

QHash<QString, QString> readDat(const QString &path) {
    QHash<QString, QString> games;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return games;
    QXmlStreamReader xml(&file);
    QString name;
    bool bios = false;
    QString description;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            // FBNeo's DATs say game; MAME's own, machine.
            if (xml.name() == QLatin1String("game") || xml.name() == QLatin1String("machine")) {
                const QXmlStreamAttributes attributes = xml.attributes();
                name = attributes.value(QLatin1String("name")).toString().toLower();
                bios = attributes.value(QLatin1String("isbios")) == QLatin1String("yes");
                description.clear();
            } else if (xml.name() == QLatin1String("description") && !name.isEmpty()) {
                description = xml.readElementText();
            }
        } else if (xml.isEndElement()
                   && (xml.name() == QLatin1String("game") || xml.name() == QLatin1String("machine"))) {
            if (!name.isEmpty() && !bios)
                games.insert(name, description.isEmpty() ? name : description);
            name.clear();
        }
    }
    if (xml.hasError() && xml.error() != QXmlStreamReader::PrematureEndOfDocumentError)
        qWarning("[Arcade] FinalBurn Neo's list %s isn't whole: %s", qPrintable(path),
                 qPrintable(xml.errorString()));
    return games;
}

QString reservedDevice(const QString &guid, const QString &name) {
    // SDL's GUID of a USB or Bluetooth pad: its bus, then its vendor and its
    // product, each two bytes little-endian with two of zeros after; any
    // other kind of device has none, and is kept by its name.
    if (guid.size() == 32 && guid.mid(12, 4) == QLatin1String("0000") && guid.mid(20, 4) == QLatin1String("0000")) {
        const QString vendor = guid.mid(10, 2) + guid.mid(8, 2);
        const QString product = guid.mid(18, 2) + guid.mid(16, 2);
        if (vendor != QLatin1String("0000") || product != QLatin1String("0000"))
            return (vendor + QLatin1Char(':') + product).toLower() + QLatin1Char(' ') + name;
    }
    return name;
}

QString configText(const Launch &launch) {
    QStringList lines{ QStringLiteral("# Written by OSD/OS for each game it starts: what is changed here is lost at the next.") };
    const auto set = [&lines](const char *key, const QString &value) {
        lines << QLatin1String(key) + QStringLiteral(" = ") + quoted(value);
    };
    const auto on = [](bool value) { return value ? QStringLiteral("true") : QStringLiteral("false"); };
    const bool achievements = !launch.achievementsUser.isEmpty()
                              && (!launch.achievementsToken.isEmpty() || !launch.achievementsPassword.isEmpty());
    // The password only until there is a token, which RetroArch swaps it for.
    const bool swapping = achievements && launch.achievementsToken.isEmpty();
    // A config RetroArch saves on exit only for that token: the rest is
    // written here again each time.
    set("config_save_on_exit", on(swapping));
    set("video_fullscreen", on(true));
    set("pause_nonactive", on(false));
    // RGUI: its menu reads at a CRT's 240 lines.
    set("menu_driver", QStringLiteral("rgui"));
    set("ui_companion_enable", on(false));
    set("ui_companion_start_on_boot", on(false));
    if (launch.console) {
        // The console's own: no X or Wayland to draw in, read keys from or
        // play sound through (Debian's defaults are a desktop's).
        set("video_driver", QStringLiteral("gl"));
        set("video_context_driver", QStringLiteral("kms"));
        set("input_driver", QStringLiteral("udev"));
        set("audio_driver", QStringLiteral("alsa"));
        // ALSA's default: the card Settings → Audio Output chose (ALSA_CARD).
        set("audio_device", QString());
        set("libretro_info_path", QStringLiteral("/usr/share/libretro/info"));
        set("assets_directory", QStringLiteral("/usr/share/libretro/assets"));
    }
    set("audio_enable", on(launch.sound));
    // Pads as SDL knows them, the menus' own (with the built-in Standard
    // Gamepad for any RetroArch has no profile of): Debian ships none.
    set("input_joypad_driver", QStringLiteral("sdl2"));
    set("input_autodetect_enable", on(true));
    // Coin and Start together, or Esc, at once: MAME's way out.
    set("input_quit_gamepad_combo", QStringLiteral("4"));
    set("input_menu_toggle_gamepad_combo", QStringLiteral("0"));
    set("input_exit_emulator", QStringLiteral("escape"));
    set("quit_press_twice", on(false));
    for (int p = 0; p < launch.pads.size(); ++p) {
        if (launch.pads.at(p).isEmpty())
            continue;
        const QByteArray player = QByteArray("input_player") + QByteArray::number(p + 1);
        set((player + "_device_reservation_type").constData(), QStringLiteral("2"));
        set((player + "_reserved_device").constData(), launch.pads.at(p));
    }
    set("system_directory", launch.systemDir);
    set("savefile_directory", launch.saveDir);
    set("savestate_directory", launch.stateDir);
    set("rgui_config_directory", launch.configDir);
    set("libretro_log_level", QStringLiteral("1"));

    set("cheevos_enable", on(achievements));
    if (achievements) {
        set("cheevos_username", launch.achievementsUser);
        set("cheevos_token", launch.achievementsToken);
        set("cheevos_password", swapping ? launch.achievementsPassword : QString());
        // RetroAchievements' leaderboards take only hardcore's scores.
        set("cheevos_hardcore_mode_enable", on(true));
    }

    if (launch.netplay != Launch::Netplay::None) {
        set("netplay_nickname", launch.nickname);
        set("netplay_start_as_spectator", on(false));
    }
    if (launch.netplay == Launch::Netplay::Host) {
        // In the lobby, through a relay: a home connection takes no calls.
        set("netplay_public_announce", on(true));
        set("netplay_use_mitm_server", on(true));
        set("netplay_mitm_server", launch.relay.isEmpty() ? QStringLiteral("nyc") : launch.relay);
    }
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QStringList arguments(const Launch &launch) {
    // The log only with -v: --log-file alone writes nothing.
    QStringList args{ QStringLiteral("-L"), launch.core, QStringLiteral("--config"), launch.config,
                      QStringLiteral("--verbose"), QStringLiteral("--log-file=") + launch.log };
    if (launch.netplay == Launch::Netplay::Host)
        args << QStringLiteral("--host");
    else if (launch.netplay == Launch::Netplay::Join)
        args << QStringLiteral("--connect") << launch.joinAddress;
    args << launch.content;
    return args;
}

Run readLog(const QString &path) {
    Run run;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return run;
    static const QRegularExpression rom(QStringLiteral("\\[FBNeo\\] ROM at index \\d+ with name (\\S+) and CRC"));
    static const QRegularExpression login(QStringLiteral("RetroAchievements login failed: (.*)$"));
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        run.logged = true;
        if (line.contains(QLatin1String("[FBNeo] Driver successfully started"))) {
            run.started = true;
            continue;
        }
        const QRegularExpressionMatch missing = rom.match(line);
        if (missing.hasMatch()) {
            if (!run.missing.contains(missing.captured(1)))
                run.missing << missing.captured(1);
            continue;
        }
        const QRegularExpressionMatch failed = login.match(line);
        if (failed.hasMatch())
            run.loginFailure = failed.captured(1).trimmed();
        if (line.contains(QLatin1String("[ERROR]")))
            run.error = line.section(QLatin1String("[ERROR]"), 1).trimmed();
    }
    return run;
}

QString savedToken(const QString &configPath) {
    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    static const QRegularExpression token(QStringLiteral("^\\s*cheevos_token\\s*=\\s*\"([^\"]*)\""));
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QRegularExpressionMatch match = token.match(in.readLine());
        if (match.hasMatch())
            return match.captured(1);
    }
    return QString();
}

QList<Room> readLobby(const QByteArray &json) {
    QList<Room> rooms;
    const QJsonArray list = QJsonDocument::fromJson(json).array();
    for (const QJsonValue &entry : list) {
        const QJsonObject fields = entry.toObject().value(QLatin1String("fields")).toObject();
        if (fields.value(QLatin1String("core_name")).toString() != QLatin1String("FinalBurn Neo"))
            continue;
        // As RetroArch's menu reads them: connectable unless it says not.
        if (!fields.value(QLatin1String("connectable")).toBool(true)
            || !fields.value(QLatin1String("is_retroarch")).toBool(true))
            continue;
        const QVersionNumber version = QVersionNumber::fromString(fields.value(QLatin1String("retroarch_version")).toString());
        if (version < kOldestRetroArch)
            continue;
        Room room;
        room.id = fields.value(QLatin1String("id")).toInt();
        // The game as its file is named, the set: a short name, lower case.
        static const QRegularExpression set(QStringLiteral("^[a-z0-9_]{1,32}$"));
        room.game = fields.value(QLatin1String("game_name")).toString().toLower();
        for (const char *extension : { ".zip", ".7z" }) {
            if (room.game.endsWith(QLatin1String(extension)))
                room.game.chop(static_cast<int>(qstrlen(extension)));
        }
        room.host = cleaned(fields.value(QLatin1String("username")).toString(), 32);
        static const QRegularExpression code(QStringLiteral("^[a-z]{2}$"));
        room.country = fields.value(QLatin1String("country")).toString().toLower();
        if (!code.match(room.country).hasMatch())
            room.country.clear();
        room.password = fields.value(QLatin1String("has_password")).toBool();
        // Relayed (host_method 3, MITM): through the relay, its session the
        // lobby's; else straight to the host.
        if (fields.value(QLatin1String("host_method")).toInt() == 3)
            room.joinAddress = joinAddressOf(fields.value(QLatin1String("mitm_ip")).toString(),
                                             fields.value(QLatin1String("mitm_port")).toInt(),
                                             fields.value(QLatin1String("mitm_session")).toString());
        else
            room.joinAddress = joinAddressOf(fields.value(QLatin1String("ip")).toString(),
                                             fields.value(QLatin1String("port")).toInt(), QString());
        if (!set.match(room.game).hasMatch() || room.joinAddress.isEmpty())
            continue;
        rooms << room;
    }
    std::sort(rooms.begin(), rooms.end(), [](const Room &a, const Room &b) { return a.id > b.id; });
    return rooms;
}

QString achievementsHash(const QString &fileName) {
    return QString::fromLatin1(QCryptographicHash::hash(QFileInfo(fileName).completeBaseName().toUtf8(),
                                                        QCryptographicHash::Md5).toHex());
}

} // namespace libretro
