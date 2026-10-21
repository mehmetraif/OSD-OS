#include "Libretro.h"
#include "Mame.h"
#include "../../input/ControllerProfile.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <QVersionNumber>
#include <QXmlStreamReader>
#include <algorithm>
#include <optional>
#include <utility>

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

// RetroArch's built-in Standard Gamepad (SDL2_DEFAULT_BINDS in its
// input_autodetect_builtin.c): each RetroPad input on SDL's game controller
// buttons and axes, a player's binds without "input_playerN_".
constexpr std::pair<const char *, const char *> kStandardGamepad[] = {
    { "a_btn", "1" }, { "b_btn", "0" }, { "x_btn", "3" }, { "y_btn", "2" },
    { "start_btn", "6" }, { "select_btn", "4" },
    { "up_btn", "11" }, { "down_btn", "12" }, { "left_btn", "13" }, { "right_btn", "14" },
    { "l_btn", "9" }, { "r_btn", "10" }, { "l2_axis", "+4" }, { "r2_axis", "+5" },
    { "l3_btn", "7" }, { "r3_btn", "8" },
    { "l_x_plus_axis", "+0" }, { "l_x_minus_axis", "-0" }, { "l_y_plus_axis", "+1" }, { "l_y_minus_axis", "-1" },
    { "r_x_plus_axis", "+2" }, { "r_x_minus_axis", "-2" }, { "r_y_plus_axis", "-3" }, { "r_y_minus_axis", "+3" },
};

// An arcade panel's, a JAMMA board's stick as SDL maps osdos-jamma's pad
// (buttons 1 to 6 on A, B, X, Y and the shoulders, Coin on Back, the
// joystick on the left stick's axes): the cabinet's two rows of buttons, 1
// to 3 above and 4 to 6 below, where FinalBurn Neo's 6-Button Panel has
// them (Y X L above, B A R below), and the joystick on the D-pad too, where
// the games read their directions.
constexpr std::pair<const char *, const char *> kArcadePanel[] = {
    { "y_btn", "0" }, { "x_btn", "1" }, { "l_btn", "2" },
    { "b_btn", "3" }, { "a_btn", "9" }, { "r_btn", "10" },
    { "start_btn", "6" }, { "select_btn", "4" },
    { "up_axis", "-1" }, { "down_axis", "+1" }, { "left_axis", "-0" }, { "right_axis", "+0" },
    { "l2_axis", "+4" }, { "r2_axis", "+5" },
    { "l_x_plus_axis", "+0" }, { "l_x_minus_axis", "-0" }, { "l_y_plus_axis", "+1" }, { "l_y_minus_axis", "-1" },
};

// FinalBurn Neo's name for RetroArch, which names its remap folder and file.
const QLatin1String kCoreName("FinalBurn Neo");
// FinalBurn Neo's 6-Button Panel, its RETROPAD_6PANEL:
// RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 0).
constexpr int k6ButtonPanel = (1 << 8) | 5;

// Whether player p (from 0) plays on an arcade panel's layout: off line
// only. Online, RetroArch gives every player the host's controller types,
// on which a panel's binds would scatter its buttons.
bool onPanel(const Launch &launch, int p) {
    return launch.netplay == Launch::Netplay::None && launch.panels.value(p);
}

// A config's value, in its quotes: RetroArch's parser takes a line's value
// to the next quote, so none in it, nor a line's end.
QString quoted(QString value) {
    value.remove(QLatin1Char('"'));
    value.remove(QLatin1Char('\n'));
    value.remove(QLatin1Char('\r'));
    return QLatin1Char('"') + value + QLatin1Char('"');
}

// A config's line: "key = "value"".
QString configLine(const char *key, const QString &value) {
    return QLatin1String(key) + QStringLiteral(" = ") + quoted(value);
}

QString onOff(bool value) {
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

// RetroArch's "RetroAchievements login failed: <why>": why, or nothing for
// any other line.
std::optional<QString> loginFailure(const QString &line) {
    static const QRegularExpression failed(QStringLiteral("RetroAchievements login failed: (.*)$"));
    const QRegularExpressionMatch match = failed.match(line);
    if (!match.hasMatch())
        return std::nullopt;
    return match.captured(1).trimmed();
}

// The lobby's oldest RetroArch the menu lists rooms of.
const QVersionNumber kOldestRetroArch(1, 9, 1);

// The most rooms of the lobby taken, the newest: it lists a few hundred, and
// an answer as big as one is let be could hold a hundred thousand.
constexpr qsizetype kMostRooms = 1000;

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

// The relay a room goes through, by its server's name: RetroArch's own by
// where it is ("us-east1"), another by its whole name; "Unknown" for none.
QString relayName(const QString &server) {
    const QString name = cleaned(server, 64);
    for (const char *own : { ".relay.retroarch.com", ".relay.libretro.com" }) {
        if (name.endsWith(QLatin1String(own)) && name.size() > static_cast<qsizetype>(qstrlen(own)))
            return name.chopped(static_cast<qsizetype>(qstrlen(own)));
    }
    return name.isEmpty() ? QStringLiteral("Unknown") : name;
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

QHash<QString, QString> readDat(const QString &path, QHash<QString, double> *aspects,
                                QHash<QString, mame::Machine> *machines) {
    QHash<QString, QString> games;
    if (aspects)
        aspects->clear();
    if (machines)
        machines->clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return games;
    QXmlStreamReader xml(&file);
    QString name;
    bool bios = false;
    QString description;
    double aspect = 0;
    mame::Machine machine;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            // FBNeo's DATs say game; MAME's own, machine.
            if (xml.name() == QLatin1String("game") || xml.name() == QLatin1String("machine")) {
                const QXmlStreamAttributes attributes = xml.attributes();
                name = attributes.value(QLatin1String("name")).toString().toLower();
                bios = attributes.value(QLatin1String("isbios")) == QLatin1String("yes");
                description.clear();
                aspect = 0;
                machine = mame::Machine();
                machine.cloneOf = attributes.value(QLatin1String("cloneof")).toString().toLower();
                // Its driver as MAME names its own (mame::systemOf()).
                const QString source = attributes.value(QLatin1String("sourcefile")).toString();
                const qsizetype file = source.lastIndexOf(QLatin1Char('/')) + 1;
                machine.sourceFile = source.mid(file, 2) == QLatin1String("d_") ? source.left(file) + source.mid(file + 2)
                                                                                 : source;
            } else if (name.isEmpty()) {
                continue;
            } else if (xml.name() == QLatin1String("description")) {
                description = xml.readElementText();
            } else if (xml.name() == QLatin1String("year")) {
                machine.year = xml.readElementText();
            } else if (xml.name() == QLatin1String("manufacturer")) {
                machine.manufacturer = xml.readElementText();
            } else if (xml.name() == QLatin1String("driver")) {
                machine.status = xml.attributes().value(QLatin1String("status")).toString();
            } else if (xml.name() == QLatin1String("video")) {
                const QXmlStreamAttributes attributes = xml.attributes();
                const double x = attributes.value(QLatin1String("aspectx")).toDouble();
                const double y = attributes.value(QLatin1String("aspecty")).toDouble();
                if (x > 0 && y > 0)
                    aspect = x / y;
                if (attributes.value(QLatin1String("orientation")) == QLatin1String("vertical"))
                    machine.rotate = 90;
            }
        } else if (xml.isEndElement()
                   && (xml.name() == QLatin1String("game") || xml.name() == QLatin1String("machine"))) {
            if (!name.isEmpty() && !bios) {
                games.insert(name, description.isEmpty() ? name : description);
                if (aspects && aspect > 0)
                    aspects->insert(name, aspect);
                if (machines) {
                    machine.name = name;
                    machine.description = games.value(name);
                    machines->insert(name, machine);
                }
            }
            name.clear();
        }
    }
    if (xml.hasError() && xml.error() != QXmlStreamReader::PrematureEndOfDocumentError)
        qWarning("[Arcade] FinalBurn Neo's list %s isn't whole: %s", qPrintable(path),
                 qPrintable(xml.errorString()));
    return games;
}

double viewportAspect(double gameAspect, const QSize &screen, double screenAspect) {
    if (gameAspect <= 0 || screenAspect <= 0 || screen.width() <= 0 || screen.height() <= 0)
        return 0;
    return gameAspect * screen.width() / screen.height() / screenAspect;
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
    const auto set = [&lines](const char *key, const QString &value) { lines << configLine(key, value); };
    const auto on = onOff;
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
    // The picture in its shape on a screen whose pixels aren't square
    // (aspect_ratio_index 20, Config: the ratio as it is).
    if (launch.aspect > 0) {
        set("aspect_ratio_index", QStringLiteral("20"));
        set("video_aspect_ratio", QString::number(launch.aspect, 'f', 6));
        set("video_aspect_ratio_auto", on(false));
    }
    // Its lines whole, the width to the shape (axis 0), fewer times rather
    // than cut (scaling 0); a picture with more lines than the screen is
    // spread over them.
    set("video_scale_integer", on(launch.integerScale));
    if (launch.integerScale) {
        set("video_scale_integer_axis", QStringLiteral("0"));
        set("video_scale_integer_scaling", QStringLiteral("0"));
    }
    // Pads as SDL knows them, the menus' own, each player's buttons those of
    // RetroArch's built-in Standard Gamepad: the profile it falls back on
    // for a pad it has none of, Debian shipping none. Written out for every
    // player, with RetroArch's own look-up off, so that it no longer says
    // over the game that each pad has no profile ("not configured, using
    // fallback"); the reservations below still give each player their pad.
    // A player on an arcade panel has its two rows instead (kArcadePanel),
    // off line (onPanel()).
    set("input_joypad_driver", QStringLiteral("sdl2"));
    set("input_autodetect_enable", on(false));
    for (int p = 1; p <= controllers::kPlayers; ++p) {
        const QByteArray player = QByteArray("input_player") + QByteArray::number(p) + '_';
        const auto bind = [&set, &player](const auto &binds) {
            for (const auto &[name, value] : binds)
                set((player + name).constData(), QLatin1String(value));
        };
        if (onPanel(launch, p - 1))
            bind(kArcadePanel);
        else
            bind(kStandardGamepad);
    }
    // FinalBurn Neo's remap file, written with this config (remapText()):
    // loaded without a word over the game, and never written back.
    set("input_remapping_directory", launch.remapDir);
    set("auto_remaps_enable", on(true));
    set("input_remap_sort_by_controller_enable", on(false));
    set("remap_save_on_exit", on(false));
    set("notification_show_remap_load", on(false));
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

QString coreRemapPath(const QString &remapDir) {
    return remapDir + QLatin1Char('/') + kCoreName + QLatin1Char('/') + kCoreName + QStringLiteral(".rmp");
}

QString remapText(const Launch &launch) {
    QStringList lines;
    for (int p = 0; p < launch.panels.size() && p < controllers::kPlayers; ++p) {
        if (onPanel(launch, p))
            lines << configLine(QByteArray("input_libretro_device_p" + QByteArray::number(p + 1)).constData(),
                                QString::number(k6ButtonPanel));
    }
    if (lines.isEmpty())
        return QString();
    lines.prepend(QStringLiteral("# Written by OSD/OS for each FinalBurn Neo game: what is changed here is lost at the next."));
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
        if (const std::optional<QString> failure = loginFailure(line))
            run.loginFailure = *failure;
        if (line.contains(QLatin1String("[ERROR]")))
            run.error = line.section(QLatin1String("[ERROR]"), 1).trimmed();
    }
    return run;
}

bool loginRefused(const QString &failure) {
    // RetroAchievements' own words for its invalid_credentials and
    // expired_token answers ("Invalid user/token combination.", "Invalid
    // User/Password combination. Please try again", "The access token has
    // expired"), which rcheevos passes on as they are.
    static const QRegularExpression refused(QStringLiteral("invalid|expired|password|token|credential"),
                                            QRegularExpression::CaseInsensitiveOption);
    return refused.match(failure).hasMatch();
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

QString signInConfigText(const SignIn &signIn) {
    QStringList lines{ QStringLiteral("# Written by OSD/OS to check a RetroAchievements sign-in, and removed after it.") };
    const auto set = [&lines](const char *key, const QString &value) { lines << configLine(key, value); };
    // Saved on exit for the token, as after a game; and ended, not left in
    // its menu with its dummy core, as the core ends it should nothing else.
    set("config_save_on_exit", onOff(true));
    set("load_dummy_on_core_shutdown", onOff(false));
    // Nothing drawn, heard or read: the menus keep the screen and the pads.
    set("video_driver", QStringLiteral("null"));
    set("audio_driver", QStringLiteral("null"));
    set("audio_enable", onOff(false));
    set("input_driver", QStringLiteral("null"));
    set("input_joypad_driver", QStringLiteral("null"));
    set("menu_driver", QStringLiteral("rgui"));
    set("pause_nonactive", onOff(false));
    set("ui_companion_enable", onOff(false));
    set("system_directory", signIn.dir);
    set("savefile_directory", signIn.dir);
    set("savestate_directory", signIn.dir);
    set("cheevos_enable", onOff(true));
    set("cheevos_username", signIn.user);
    set("cheevos_password", signIn.password);
    set("cheevos_token", QString());
    set("cheevos_hardcore_mode_enable", onOff(false));
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QStringList signInArguments(const SignIn &signIn) {
    return { QStringLiteral("-L"), signIn.core, QStringLiteral("--config"), signIn.config,
             QStringLiteral("--verbose"), signIn.content };
}

SignInLine readSignInLine(const QString &line, QString *failure) {
    // rcheevos' "<user> logged in successfully" and RetroArch's "RetroAchievements
    // login failed: <why>", under its [RCHEEVOS] tag; its words for a server
    // that didn't answer, which leave the password to try at the next game.
    if (!line.contains(QLatin1String("[RCHEEVOS]")))
        return SignInLine::None;
    if (line.contains(QLatin1String(" logged in successfully")))
        return SignInLine::SignedIn;
    if (const std::optional<QString> why = loginFailure(line)) {
        if (failure)
            *failure = *why;
        return loginRefused(*why) ? SignInLine::Failed : SignInLine::Unreachable;
    }
    if (line.contains(QLatin1String("Unable to communicate with RetroAchievements server")))
        return SignInLine::Unreachable;
    return SignInLine::None;
}

QList<Room> readWholeLobby(const QByteArray &json) {
    QList<Room> rooms;
    const QJsonArray list = QJsonDocument::fromJson(json).array();
    // The game as its file is named, the set: a short name, lower case.
    static const QRegularExpression set(QStringLiteral("^[a-z0-9_]{1,32}$"));
    static const QRegularExpression code(QStringLiteral("^[a-z]{2}$"));
    static const QRegularExpression crc(QStringLiteral("^[0-9A-Fa-f]{8}$"));
    static const QRegularExpression stamp(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}"));
    // The newest first, kMostRooms at most: only those made into rooms.
    QList<std::pair<int, QJsonObject>> entries;
    for (const QJsonValue &entry : list) {
        const QJsonObject fields = entry.toObject().value(QLatin1String("fields")).toObject();
        if (!fields.isEmpty())
            entries.append({ fields.value(QLatin1String("id")).toInt(), fields });
    }
    const auto newer = [](const std::pair<int, QJsonObject> &a, const std::pair<int, QJsonObject> &b) {
        return a.first > b.first;
    };
    if (entries.size() > kMostRooms) {
        std::nth_element(entries.begin(), entries.begin() + kMostRooms, entries.end(), newer);
        entries.resize(kMostRooms);
    }
    std::sort(entries.begin(), entries.end(), newer);
    for (const auto &entry : std::as_const(entries)) {
        const QJsonObject &fields = entry.second;
        const auto text = [&fields](const char *key) { return fields.value(QLatin1String(key)).toString(); };
        Room room;
        room.id = entry.first;
        room.rom = cleaned(text("game_name"), 64);
        room.game = text("game_name").toLower();
        for (const char *extension : { ".zip", ".7z" }) {
            if (room.game.endsWith(QLatin1String(extension)))
                room.game.chop(static_cast<int>(qstrlen(extension)));
        }
        if (!set.match(room.game).hasMatch())
            room.game.clear();
        if (crc.match(text("game_crc")).hasMatch())
            room.crc = text("game_crc").toUpper();
        room.core = cleaned(text("core_name"), 32);
        room.coreVersion = cleaned(text("core_version"), 32);
        room.subsystem = cleaned(text("subsystem_name"), 32);
        if (room.subsystem == QLatin1String("N/A"))
            room.subsystem.clear();
        room.retroarch = cleaned(text("retroarch_version"), 16);
        room.frontend = cleaned(text("frontend"), 32);
        room.host = cleaned(text("username"), 32);
        room.country = text("country").toLower();
        if (!code.match(room.country).hasMatch())
            room.country.clear();
        if (stamp.match(text("created")).hasMatch())
            room.created = QDateTime::fromString(text("created").left(19) + QLatin1Char('Z'), Qt::ISODate);
        room.players = std::max(-1, fields.value(QLatin1String("player_count")).toInt(-1));
        room.spectators = std::max(-1, fields.value(QLatin1String("spectator_count")).toInt(-1));
        room.password = fields.value(QLatin1String("has_password")).toBool();
        room.spectatePassword = fields.value(QLatin1String("has_spectate_password")).toBool();
        // As RetroArch's menu reads them: connectable unless it says not.
        room.connectable = fields.value(QLatin1String("connectable")).toBool(true);
        // Relayed (host_method 3, MITM): through the relay, its session the
        // lobby's; else straight to the host.
        if (fields.value(QLatin1String("host_method")).toInt() == 3) {
            room.joinAddress = joinAddressOf(text("mitm_ip"), fields.value(QLatin1String("mitm_port")).toInt(),
                                             text("mitm_session"));
            room.relay = relayName(text("mitm_ip"));
        } else {
            room.joinAddress = joinAddressOf(text("ip"), fields.value(QLatin1String("port")).toInt(), QString());
        }
        room.joinable = room.core == QLatin1String("FinalBurn Neo") && room.connectable
                        && fields.value(QLatin1String("is_retroarch")).toBool(true)
                        && QVersionNumber::fromString(text("retroarch_version")) >= kOldestRetroArch
                        && !room.game.isEmpty() && !room.joinAddress.isEmpty();
        rooms << room;
    }
    return rooms;
}

QList<Room> readLobby(const QByteArray &json) {
    QList<Room> rooms = readWholeLobby(json);
    rooms.erase(std::remove_if(rooms.begin(), rooms.end(), [](const Room &room) { return !room.joinable; }),
                rooms.end());
    return rooms;
}

CoreInfo loadCoreInfo(const QString &path, QString *error) {
    // As libretro.h declares it (RETRO_API_VERSION 1).
    struct SystemInfo {
        const char *library_name;
        const char *library_version;
        const char *valid_extensions;
        bool need_fullpath;
        bool block_extract;
    };
    QLibrary core(path);
    if (!core.load()) {
        if (error)
            *error = core.errorString();
        return {};
    }
    using GetSystemInfo = void (*)(SystemInfo *);
    const auto getSystemInfo = reinterpret_cast<GetSystemInfo>(core.resolve("retro_get_system_info"));
    if (!getSystemInfo) {
        if (error)
            *error = QStringLiteral("%1 is not a libretro core").arg(path);
        return {};
    }
    SystemInfo info{};
    getSystemInfo(&info);
    return { QString::fromUtf8(info.library_name ? info.library_name : ""),
             QString::fromUtf8(info.library_version ? info.library_version : "") };
}

CoreInfo readCoreInfo(const QByteArray &output) {
    CoreInfo info;
    for (const QByteArray &line : output.split('\n')) {
        if (line.startsWith("name="))
            info.name = cleaned(QString::fromUtf8(line.mid(5)), 32);
        else if (line.startsWith("version="))
            info.version = cleaned(QString::fromUtf8(line.mid(8)), 32);
    }
    return info;
}

QString readVersion(const QByteArray &output) {
    static const QRegularExpression version(QStringLiteral("(?:Version: |-- v)(\\d+\\.\\d+(?:\\.\\d+)?)"));
    const QRegularExpressionMatch match = version.match(QString::fromUtf8(output));
    return match.hasMatch() ? match.captured(1) : QString();
}

QString achievementsHash(const QString &fileName) {
    return QString::fromLatin1(QCryptographicHash::hash(QFileInfo(fileName).completeBaseName().toUtf8(),
                                                        QCryptographicHash::Md5).toHex());
}

} // namespace libretro
