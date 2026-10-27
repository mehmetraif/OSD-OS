#include "Libretro.h"
#include "Mame.h"
#include "../../input/ControllerProfile.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>
#include <QVersionNumber>
#include <QXmlStreamReader>
#include <QtEndian>
#include <algorithm>
#include <iterator>
#include <optional>
#include <utility>

namespace libretro {

namespace {

#if defined(Q_OS_MACOS)
const QString kCoreFile = QStringLiteral("fbneo_libretro.dylib");
const QString kLanCoreFile = QStringLiteral("fbneo_cv1k_libretro.dylib");
#elif defined(Q_OS_WIN)
const QString kCoreFile = QStringLiteral("fbneo_libretro.dll");
const QString kLanCoreFile = QStringLiteral("fbneo_cv1k_libretro.dll");
#else
const QString kCoreFile = QStringLiteral("fbneo_libretro.so");
const QString kLanCoreFile = QStringLiteral("fbneo_cv1k_libretro.so");
#endif
// Their info files, named as RetroArch looks for them: the core's, without
// the extension.
const QString kCoreInfoFile = QStringLiteral("fbneo_libretro.info");
const QString kLanCoreInfoFile = QStringLiteral("fbneo_cv1k_libretro.info");

QString firstFile(const QStringList &candidates) {
    for (const QString &candidate : candidates) {
        if (QFileInfo(candidate).isFile())
            return candidate;
    }
    return QString();
}

// The prefix the app is installed under: the folder above its own
// (/usr/local for /usr/local/bin/osdos).
QString packagePrefix() {
    return QDir::cleanPath(QCoreApplication::applicationDirPath() + QStringLiteral("/.."));
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
// (buttons 1 to 6 on A, B, X, Y and the shoulders, 7 and 8 on the
// triggers, Coin on Back, the joystick on the left stick's axes): the
// cabinet's two rows of buttons, 1 to 3 above and 4 to 6 below, where
// FinalBurn Neo's 6-Button Panel has them (Y X L above, B A R below), 7 and
// 8 where it reads them (R2, then L2), and the joystick on the D-pad too,
// where the games read their directions.
constexpr std::pair<const char *, const char *> kArcadePanel[] = {
    { "y_btn", "0" }, { "x_btn", "1" }, { "l_btn", "2" },
    { "b_btn", "3" }, { "a_btn", "9" }, { "r_btn", "10" },
    { "start_btn", "6" }, { "select_btn", "4" },
    { "up_axis", "-1" }, { "down_axis", "+1" }, { "left_axis", "-0" }, { "right_axis", "+0" },
    { "r2_axis", "+4" }, { "l2_axis", "+5" },
    { "l_x_plus_axis", "+0" }, { "l_x_minus_axis", "-0" }, { "l_y_plus_axis", "+1" }, { "l_y_minus_axis", "-1" },
};

// The same panel under RetroArch's Classic controller types (online, from a
// host without a panel, or one that isn't OSD/OS): FinalBurn Neo then reads
// a game's buttons 1 to 6 on B, A, Y, X, R and L (its FIRE01 to FIRE06: the
// first column's bottom and top, the second's, then the third's bottom, R,
// and top, L), 7 and 8 on R2 and L2 as the panel does. So numbered, the
// cabinet's buttons play a game's buttons by number; a fighter's punches
// and kicks, which Classic lays over a pad's two rows, aren't the panel's
// rows, which only the 6-Button Panel has.
constexpr std::pair<const char *, const char *> kArcadePanelClassic[] = {
    { "b_btn", "0" }, { "a_btn", "1" }, { "y_btn", "2" }, { "x_btn", "3" },
    { "r_btn", "9" }, { "l_btn", "10" },
    { "start_btn", "6" }, { "select_btn", "4" },
    { "up_axis", "-1" }, { "down_axis", "+1" }, { "left_axis", "-0" }, { "right_axis", "+0" },
    { "r2_axis", "+4" }, { "l2_axis", "+5" },
    { "l_x_plus_axis", "+0" }, { "l_x_minus_axis", "-0" }, { "l_y_plus_axis", "+1" }, { "l_y_minus_axis", "-1" },
};

// A pad's left stick as the joystick beside its D-pad, as a panel's is: the
// stick's directions pressed as the D-pad's. Off line FinalBurn Neo reads
// the stick itself, but online RetroArch sends only the buttons of a port
// the host has as a plain RetroPad (every port but the 6-Button Panel's), so
// a stick reaches the game only as these.
constexpr std::pair<const char *, const char *> kStickAsJoystick[] = {
    { "up_axis", "-1" }, { "down_axis", "+1" }, { "left_axis", "-0" }, { "right_axis", "+0" },
};

// FinalBurn Neo's name for RetroArch, which names its remap folder and file.
const QLatin1String kCoreName("FinalBurn Neo");
// How the remap file OSD/OS writes begins (remapText()), by which it knows
// the file is its own (ownsRemap()).
const QLatin1String kOwnRemap("# Written by OSD/OS");
// FinalBurn Neo's 6-Button Panel, its RETROPAD_6PANEL:
// RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 0).
constexpr int k6ButtonPanel = (1 << 8) | 5;

// Where a player's Arcade Buttons go, Button 1 to 8, Coin and Start: on the
// RetroPad's buttons FinalBurn Neo's 6-Button Panel reads as Button 1 to 8
// (Y X L above, B A R below, as kArcadePanel has a cabinet's; 7 on R2, 8 on
// L2).
const char *const kArcadeSlots[controllers::kArcadeInputs] = { "y", "x", "l", "b", "a", "r", "r2", "l2", "select", "start" };

// Whether the players' own layouts play, the 6-Button Panel's rows: off line,
// and online on the Panel layout, where every port has that type. Online on
// Classic, RetroArch gives every player the host's Classic types, on which a
// panel's binds would scatter its buttons.
bool panelsPlay(const Launch &launch) {
    return launch.netplay == Launch::Netplay::None || launch.layout == Launch::Layout::Panel;
}

// Whether player p (from 0) has Arcade Buttons of their own to play with:
// where the panels play, as one.
bool withArcade(const Launch &launch, int p) {
    return panelsPlay(launch) && !launch.arcade.value(p).isEmpty();
}

// Whether player p (from 0) plays on an arcade panel's layout, a JAMMA
// stick's or their Arcade Buttons'.
bool onPanel(const Launch &launch, int p) {
    return panelsPlay(launch) && (launch.panels.value(p) || withArcade(launch, p));
}

// A player's binds with their Arcade Buttons (withArcade()): each on its slot
// (kArcadeSlots), a button's number or an axis pushed ("nul" the other, and
// both for none), over the binds the player has else, of which any other on
// one of the pad's inputs those take is let go: one input, one of the game's.
QList<std::pair<QByteArray, QString>> arcadeBinds(QList<std::pair<QByteArray, QString>> binds, const QStringList &arcade) {
    const QLatin1String nul("nul");
    QSet<QString> taken;
    for (const QString &bind : arcade) {
        if (!bind.isEmpty())
            taken.insert(bind);
    }
    for (auto &[name, value] : binds) {
        if (taken.contains(value))
            value = nul;
    }
    for (int i = 0; i < controllers::kArcadeInputs; ++i) {
        const QString bind = arcade.value(i);
        const bool axis = bind.startsWith(QLatin1Char('+')) || bind.startsWith(QLatin1Char('-'));
        const QByteArray slot(kArcadeSlots[i]);
        const std::pair<QByteArray, QString> own[] = {
            { slot + "_btn", !bind.isEmpty() && !axis ? bind : QString(nul) },
            { slot + "_axis", axis ? bind : QString(nul) },
        };
        for (const auto &one : own) {
            const auto it = std::find_if(binds.begin(), binds.end(), [&one](const auto &b) { return b.first == one.first; });
            if (it != binds.end())
                it->second = one.second;
            else
                binds.append(one);
        }
    }
    return binds;
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

// The set a room plays, by its file's name as its host has it: a short name,
// in lower case, without .zip or .7z; "" for a name that can't be one.
QString setOf(const QString &name) {
    static const QRegularExpression set(QStringLiteral("^[a-z0-9_]{1,32}$"));
    QString game = name.toLower();
    for (const char *extension : { ".zip", ".7z" }) {
        if (game.endsWith(QLatin1String(extension)))
            game.chop(static_cast<int>(qstrlen(extension)));
    }
    return set.match(game).hasMatch() ? game : QString();
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
#ifdef Q_OS_WIN
    const QString exe = QStringLiteral("retroarch.exe");
#else
    const QString exe = QStringLiteral("retroarch");
#endif
    const QFileInfo dropIn(dataRoot + QStringLiteral("/bin/") + exe);
    if (dropIn.isFile() && dropIn.isExecutable())
        return dropIn.absoluteFilePath();
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("retroarch"));
    if (!onPath.isEmpty())
        return onPath;
    QStringList places;
#ifdef Q_OS_WIN
    // The one Settings → Arcade → FinalBurn Neo downloaded, RetroArch's own
    // folder unpacked beside OSD/OS, or where its builds and installers
    // commonly put it.
    const QString appDir = QCoreApplication::applicationDirPath();
    places << downloadedRetroArch(dataRoot)
           << appDir + QStringLiteral("/retroarch.exe") << appDir + QStringLiteral("/RetroArch/retroarch.exe")
           << appDir + QStringLiteral("/RetroArch-Win64/retroarch.exe")
           << QStringLiteral("C:/RetroArch-Win64/retroarch.exe") << QStringLiteral("C:/RetroArch/retroarch.exe")
           << qEnvironmentVariable("ProgramFiles") + QStringLiteral("/RetroArch/retroarch.exe")
           << qEnvironmentVariable("ProgramFiles(x86)") + QStringLiteral("/Steam/steamapps/common/RetroArch/retroarch.exe");
#else
    // Debian's; the BSDs' packages, off a PATH that doesn't reach them; the
    // Mac's RetroArch.app, for everyone or for the user.
    places << QStringLiteral("/usr/bin/retroarch") << QStringLiteral("/usr/local/bin/retroarch")
           << QStringLiteral("/Applications/RetroArch.app/Contents/MacOS/RetroArch")
           << QDir::homePath() + QStringLiteral("/Applications/RetroArch.app/Contents/MacOS/RetroArch");
#endif
    for (const QString &place : std::as_const(places)) {
        const QFileInfo program(place);
        if (program.isFile() && program.isExecutable())
            return program.absoluteFilePath();
    }
    return QString();
}

QString downloadedRetroArch(const QString &dataRoot) {
    return dataRoot + QStringLiteral("/arcade/retroarch/RetroArch-Win64/retroarch.exe");
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

QString packagedCore() {
    return packagePrefix() + QStringLiteral("/lib/osdos/") + kCoreFile;
}

QString packagedDat() {
    return packagePrefix() + QStringLiteral("/share/osdos/fbneo/fbneo-arcade.dat");
}

QString packagedLicense() {
#if defined(Q_OS_MACOS)
    return packagePrefix() + QStringLiteral("/Resources/fbneo-license.txt");
#elif defined(Q_OS_WIN)
    return QCoreApplication::applicationDirPath() + QStringLiteral("/fbneo-license.txt");
#else
    return packagePrefix() + QStringLiteral("/share/osdos/fbneo/license.txt");
#endif
}

QString lanCorePath(const QString &dataRoot) {
#if defined(Q_OS_MACOS)
    // In the bundle's Frameworks, where its signing takes a library in.
    const QString own = packagePrefix() + QStringLiteral("/Frameworks/");
#elif defined(Q_OS_WIN)
    const QString own = QCoreApplication::applicationDirPath() + QLatin1Char('/');
#else
    const QString own = packagePrefix() + QStringLiteral("/lib/osdos/");
#endif
    return firstFile({ QFileInfo(downloadedCore(dataRoot)).path() + QLatin1Char('/') + kLanCoreFile, own + kLanCoreFile });
}

qint64 netplayStateSize(const QString &sourceFile, bool lanCore) {
    // The boards whose states are 8 MiB or more, at their largest set's:
    // - Cave's CV1000: 136.75 to 144.75 MiB on FinalBurn Neo's core, its
    //   128 MiB of video memory among them, and 8.75 to 16.75 MiB on
    //   OSD/OS's own, which plays its games alone, without it.
    // - Killer Instinct's: its 8.5 MiB of RAM and, under 64 KiB, its CPUs'
    //   and sound's, as its driver's DrvScan() has them, since it doesn't
    //   start on zero-filled ROMs to be measured.
    // The next are Neo Geo CD's (7.8 MiB), SSV's (5.6), and Taito's Under
    // Fire and F3 boards' (4.6).
    static const struct { const char *sourceFile; qint64 size; qint64 lanSize; } boards[] = {
        { "cave/cv1k.cpp", 151780188, 17562464 },
        { "midway/kinst.cpp", 0x890000, 0x890000 },
    };
    for (const auto &board : boards) {
        if (sourceFile == QLatin1String(board.sourceFile))
            return lanCore ? board.lanSize : board.size;
    }
    return 0;
}

qint64 netplayMemory(qint64 stateSize, bool hosting) {
    const qint64 states = stateSize * (hosting ? kHostStates : kJoinStates);
    return std::min(states + (qint64(1) << 30), states * 2);
}

QString corePath(const QString &dataRoot) {
    const QString home = QDir::homePath();
    return firstFile({ downloadedCore(dataRoot),
                       packagedCore(),
                       QStringLiteral("/usr/local/lib/libretro/") + kCoreFile,
                       QStringLiteral("/usr/lib/aarch64-linux-gnu/libretro/") + kCoreFile,
                       QStringLiteral("/usr/lib/x86_64-linux-gnu/libretro/") + kCoreFile,
                       QStringLiteral("/usr/lib/libretro/") + kCoreFile,
                       home + QStringLiteral("/.config/retroarch/cores/") + kCoreFile,
                       home + QStringLiteral("/Library/Application Support/RetroArch/cores/") + kCoreFile });
}

QString datPath(const QString &dataRoot) {
    return firstFile({ downloadedDat(dataRoot),
                       packagedDat(),
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

QString reservedDevice(const QString &guid, const QString &name, bool byVendor) {
    if (!byVendor) {
        // The mappings are kept without the GUID's checksum (mappingWithoutCrc()).
        const QString key = controllers::mappingWithoutCrc(guid + QLatin1Char(','));
        for (const QString &mapping : controllers::padMappings()) {
            const QString mapped = mapping.section(QLatin1Char(','), 1, 1);
            if (mapping.startsWith(key) && !mapped.isEmpty())
                return mapped;
        }
        return name;
    }
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
    // English (RETRO_LANGUAGE_ENGLISH), the language OSD/OS reads RetroArch's
    // log in (readLog(), NetplayLog): left to itself, RetroArch speaks a
    // Windows' own (a German, French, Spanish or Japanese one's, say), and
    // logs its netplay lines in it too.
    set("user_language", QStringLiteral("0"));
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
        set("assets_directory", QStringLiteral("/usr/share/libretro/assets"));
    }
    // RetroArch knows a core by its info file, which it looks for by the
    // name of each core in libretro_directory: a core outside that folder,
    // or one without its file, is taken for one whose states can't be
    // relied on, and no game is hosted or joined with it ("Core does not
    // support netplay"). So the core's own folder, and OSD/OS's file
    // (coreInfoText()), read afresh each time: no cache keeps an older one.
    set("libretro_directory", QFileInfo(launch.core).absolutePath());
    set("libretro_info_path", launch.infoDir);
    set("core_info_cache_enable", on(false));
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
    // A pad's left stick is its joystick too (kStickAsJoystick). A player on
    // an arcade panel has its two rows instead (kArcadePanel), and one with
    // Arcade Buttons those, where the panels play (onPanel()); a panel under
    // Classic types its buttons by number (kArcadePanelClassic).
    set("input_joypad_driver", QStringLiteral("sdl2"));
    set("input_autodetect_enable", on(false));
    for (int p = 1; p <= controllers::kPlayers; ++p) {
        QList<std::pair<QByteArray, QString>> binds;
        const auto add = [&binds](const auto &list) {
            for (const auto &[name, value] : list)
                binds.append({ QByteArray(name), QString::fromLatin1(value) });
        };
        const bool panel = launch.panels.value(p - 1);
        if (panel && panelsPlay(launch))
            add(kArcadePanel);
        else if (panel)
            add(kArcadePanelClassic);
        else {
            add(kStandardGamepad);
            add(kStickAsJoystick);
        }
        if (withArcade(launch, p - 1))
            binds = arcadeBinds(binds, launch.arcade.value(p - 1));
        const QByteArray player = QByteArray("input_player") + QByteArray::number(p) + '_';
        for (const auto &[name, value] : std::as_const(binds))
            set((player + name).constData(), value);
    }
    // A keyboard's coin and start as MAME has them, and as the Arcade screen
    // takes COIN: 5 and 1 for player 1, 6 and 2 for player 2, 7 and 3, 8 and
    // 4. FinalBurn Neo's coin is the RetroPad's Select, which RetroArch puts
    // on Right Shift alone, and its start Enter, for player 1 only.
    for (int p = 1; p <= 4; ++p) {
        const QByteArray player = QByteArray("input_player") + QByteArray::number(p) + '_';
        set((player + "select").constData(), QStringLiteral("num%1").arg(p + 4));
        set((player + "start").constData(), QStringLiteral("num%1").arg(p));
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
    // Each player's pad kept for them (2), or only preferred (1) where
    // RetroArch knows pads by name.
    for (int p = 0; p < launch.pads.size(); ++p) {
        if (launch.pads.at(p).isEmpty())
            continue;
        const QByteArray player = QByteArray("input_player") + QByteArray::number(p + 1);
        set((player + "_device_reservation_type").constData(),
            launch.padsByVendor ? QStringLiteral("2") : QStringLiteral("1"));
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
        // The core's options both machines play with alike, from OSD/OS's
        // own file alone (netplayOptionsText()): not this machine's, nor a
        // game's or a folder's own over them.
        set("global_core_options", on(true));
        set("core_options_path", netplayOptionsPath(launch.configDir));
        set("game_specific_options", on(false));
        set("netplay_nickname", launch.nickname);
        set("netplay_start_as_spectator", on(false));
        // The game's ports this machine's players take, asked for by number
        // (its player 1 the first of them); none asked for leaves RetroArch
        // to its own, one port, the first free.
        for (int p = 1; p <= kMostPorts; ++p) {
            const QByteArray key = QByteArray("netplay_request_device_p") + QByteArray::number(p);
            set(key.constData(), on(launch.ports > 0 && p >= launch.portsFrom && p < launch.portsFrom + launch.ports));
        }
    }
    if (launch.gameRate) {
        // The game at its own rate (its driver's frames a second: Batsugun's
        // 59.64) on every machine. RetroArch stretches a game's sound to the
        // rate it takes the screen for where the two are within 5%
        // (audio_max_timing_skew), and the game with it, and nudges the sound
        // by up to half a percent to keep its buffer half full
        // (audio_rate_control): a CRT of 60.05 Hz played Batsugun at 60.05
        // frames a second, and a monitor of 240 Hz at a rate of its own. Over
        // the network the machine behind then ran unthrottled now and then to
        // catch up, its sound breaking up, and the one ahead stalled. Neither
        // stretch nor nudge, and the sound card's clock paces the game at its
        // own rate on every machine whose screen is faster than it, the
        // screen showing a frame twice now and then (a CRT of 60.05 Hz, every
        // 2.4 s). RetroArch's Sync to Exact Content Framerate
        // (vrr_runloop_enable) would hold a machine fallen behind to the
        // game's rate too, and it would never catch up.
        set("audio_sync", on(true));
        set("audio_max_timing_skew", QStringLiteral("0.000000"));
        set("audio_rate_control", on(false));
    }
    if (launch.netplay == Launch::Netplay::Host) {
        // In the lobby, through a relay: a home connection takes no calls.
        set("netplay_public_announce", on(true));
        set("netplay_use_mitm_server", on(true));
        set("netplay_mitm_server", launch.relay.isEmpty() ? QStringLiteral("nyc") : launch.relay);
    } else if (launch.netplay == Launch::Netplay::LanHost) {
        // On the local network alone, straight: a host that isn't relayed
        // answers the queries made there (readLanAnswer()). Not in the
        // lobby, and no port opened on the router for the internet.
        set("netplay_public_announce", on(false));
        set("netplay_use_mitm_server", on(false));
        set("netplay_nat_traversal", on(false));
    }
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString coreRemapPath(const QString &remapDir, bool lanCore) {
    const QLatin1String name = lanCore ? kLanCoreName : kCoreName;
    return remapDir + QLatin1Char('/') + name + QLatin1Char('/') + name + QStringLiteral(".rmp");
}

QString remapText(const Launch &launch) {
    QStringList lines;
    const auto port = [&lines](int p) {
        lines << configLine(QByteArray("input_libretro_device_p" + QByteArray::number(p)).constData(),
                            QString::number(k6ButtonPanel));
    };
    const bool hosting = launch.netplay == Launch::Netplay::Host || launch.netplay == Launch::Netplay::LanHost;
    if (hosting) {
        // RetroArch gives the machines joining the host's types, and a
        // machine's players find their pads only among ports of one kind:
        // every port the panel's, or none.
        if (launch.layout == Launch::Layout::Panel) {
            for (int p = 1; p <= kMostPorts; ++p)
                port(p);
        }
    } else if (launch.netplay == Launch::Netplay::None) {
        for (int p = 0; p < launch.panels.size() && p < controllers::kPlayers; ++p) {
            if (onPanel(launch, p))
                port(p + 1);
        }
    }
    if (lines.isEmpty())
        return QString();
    lines.prepend(QString(kOwnRemap) + QLatin1String(" for each FinalBurn Neo game: what is changed here is lost at the next."));
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

bool ownsRemap(const QString &path) {
    QFile file(path);
    if (!file.exists())
        return true;
    return file.open(QIODevice::ReadOnly) && QString::fromUtf8(file.readLine()).startsWith(kOwnRemap);
}

QString coreOptionsPath(const QString &configDir) {
    return configDir + QLatin1Char('/') + kCoreName + QLatin1Char('/') + kCoreName + QStringLiteral(".opt");
}

QString withHardcoreOptions(const QString &text) {
    // Of the core options RetroArch's hardcore mode disallows for FinalBurn
    // Neo (cheevos.c), the one its defaults set: patched sets allowed. A
    // cheat turned on is disallowed too, but none is unless the user turns
    // it on, and RetroArch says so on its screen then.
    static const struct { const char *key; const char *value; } wanted[] = {
        { "fbneo-allow-patched-romsets", "disabled" },
    };
    QString out = text;
    if (!out.isEmpty() && !out.endsWith(QLatin1Char('\n')))
        out += QLatin1Char('\n');
    // The last line is the "" after the final newline: a line added goes
    // before it, so the file still ends in one.
    QStringList lines = out.split(QLatin1Char('\n'));
    for (const auto &option : wanted) {
        const QLatin1String key(option.key);
        const QString line = configLine(option.key, QLatin1String(option.value));
        bool found = false;
        for (QString &existing : lines) {
            const QString trimmed = existing.trimmed();
            if (!trimmed.startsWith(key) || !trimmed.mid(key.size()).trimmed().startsWith(QLatin1Char('=')))
                continue;
            existing = line;
            found = true;
        }
        if (!found)
            lines.insert(lines.size() - 1, line);
    }
    return lines.join(QLatin1Char('\n'));
}

QString netplayOptionsPath(const QString &configDir) {
    return configDir + QStringLiteral("/netplay-options.cfg");
}

QString netplayOptionsText(const QString &userOptions) {
    // FinalBurn Neo's defaults (its libretro retro_common.cpp) for each of
    // its options that changes how a game runs, high scores and patched sets
    // aside. A plain RetroArch's host plays with these unless its user
    // changed them; FinalBurn Neo turns its high scores off over the network
    // anyway.
    static const struct { const char *key; const char *value; } alike[] = {
        { "fbneo-force-60hz", "disabled" },
        { "fbneo-resolution", "640x480" },
        { "fbneo-fixed-frameskip", "0" },
        { "fbneo-frameskip-type", "disabled" },
        { "fbneo-frameskip-manual-threshold", "33" },
        { "fbneo-cpu-speed-adjust", "100%" },
        { "fbneo-diagnostic-input", "Hold Start" },
        { "fbneo-hiscores", "disabled" },
        { "fbneo-allow-patched-romsets", "disabled" },
        { "fbneo-samplerate", "48000" },
        { "fbneo-sample-interpolation", "4-point 3rd order" },
        { "fbneo-fm-interpolation", "4-point 3rd order" },
        { "fbneo-lowpass-filter", "disabled" },
        { "fbneo-analog-speed", "100%" },
        { "fbneo-socd", "3" },
        { "fbneo-cyclone", "disabled" },
        { "fbneo-neogeo-mode", "DIPSWITCH" },
        { "fbneo-memcard-mode", "disabled" },
    };
    static const char *const picture[] = {
        "fbneo-allow-depth-32",
        "fbneo-vertical-mode",
        "fbneo-lightgun-crosshair-emulation",
    };
    static const QRegularExpression option(QStringLiteral("^\\s*([^\\s=]+)\\s*=\\s*\"([^\"]*)\""));
    QHash<QString, QString> user;
    for (const QString &line : userOptions.split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch match = option.match(line);
        if (match.hasMatch())
            user.insert(match.captured(1), match.captured(2));
    }
    QStringList lines{ QStringLiteral("# Written by OSD/OS for each game played over the network: what is changed here is lost at the next.") };
    for (const char *key : picture) {
        const auto value = user.constFind(QLatin1String(key));
        if (value != user.cend())
            lines << configLine(key, *value);
    }
    for (const auto &[key, value] : alike)
        lines << configLine(key, QLatin1String(value));
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString coreInfoPath(const QString &infoDir, bool lanCore) {
    return infoDir + QLatin1Char('/') + (lanCore ? kLanCoreInfoFile : kCoreInfoFile);
}

QString coreInfoText(bool lanCore) {
    // Of libretro's own (libretro-core-info's fbneo_libretro.info), the
    // core's names and its states: deterministic, the same on every machine,
    // as netplay needs them (and run-ahead).
    return QStringLiteral("# Written by OSD/OS for each game it starts: what is changed here is lost at the next.\n"
                          "display_name = \"Arcade (FinalBurn Neo)\"\n"
                          "corename = \"%1\"\n"
                          "savestate = \"true\"\n"
                          "savestate_features = \"deterministic\"\n")
        .arg(lanCore ? kLanCoreName : kCoreName);
}

QStringList arguments(const Launch &launch) {
    // The log only with -v: --log-file alone writes nothing. The paths in
    // the system's own separators: FinalBurn Neo finds the set's folder by
    // cutting the content's path at the last of them (its extract_directory,
    // PATH_DEFAULT_SLASH_C), a backslash on Windows, so a path given with
    // slashes there had it look in RetroArch's own folder ("None of those
    // archives was found in your paths").
    const auto native = [](const QString &path) { return QDir::toNativeSeparators(path); };
    QStringList args{ QStringLiteral("-L"), native(launch.core), QStringLiteral("--config"), native(launch.config),
                      QStringLiteral("--verbose"), QStringLiteral("--log-file=") + native(launch.log) };
    if (hosts(launch.netplay))
        args << QStringLiteral("--host");
    else if (launch.netplay == Launch::Netplay::Join || launch.netplay == Launch::Netplay::LanJoin)
        args << QStringLiteral("--connect") << launch.joinAddress;
    args << native(launch.content);
    return args;
}

std::optional<QString> netplayError(const QString &line) {
    static const QRegularExpression error(QStringLiteral("\\[ERROR\\] \\[Netplay\\] (.*)$"));
    const QRegularExpressionMatch match = error.match(line);
    if (!match.hasMatch())
        return std::nullopt;
    QString why = match.captured(1).trimmed();
    if (why.endsWith(QLatin1Char('.')))
        why.chop(1);
    return why;
}

QString netplayFailure(const QStringList &errors, bool hosting) {
    // netplay_frontend.c: a start that fails ends in init_netplay()'s
    // "Failed to initialize netplay", after the lines that say what did. A
    // host tries its port over IPv6, then IPv4, each miss logged ("Failed
    // to bind port"), so a miss alone ends nothing. Its other errors, a
    // client's wrong messages or a stall, leave the game on.
    static const auto among = [](const QString &why, const auto &ends) {
        return std::any_of(std::begin(ends), std::end(ends),
                           [&why](const char *end) { return why.contains(QLatin1String(end)); });
    };
    // What went wrong as netplay was set up, hosting or joining: the port,
    // the address, the relay, a core without the states it needs; the
    // sockets line follows the one that says why.
    static const char *const setup[] = { "Failed to bind port", "Failed to set a hosting address", "Failed to resolve host",
                                         "Failed to connect to relay server", "Failed to create a tunnel session",
                                         "Core does not support netplay", "Failed to set up netplay sockets" };
    // Joining alone: the host, reached or not, turns this machine away. A
    // host logs the same of a client it turns away, and plays on.
    static const char *const refused[] = { "Failed to connect to host", "Cannot connect", "Netplay host full",
                                           "banned from this host", "not running RetroArch", "Incorrect password",
                                           "too high for this host", "Failed to receive", "does not support netplay between" };
    QString telling;
    for (const QString &why : errors) {
        if (!hosting && among(why, refused))
            return why;
        if (why.contains(QLatin1String("Failed to initialize netplay")))
            return telling.isEmpty() ? why : telling;
        if (among(why, setup) && (telling.isEmpty() || !why.contains(QLatin1String("Failed to set up netplay sockets"))))
            telling = why;
    }
    return QString();
}

void NetplayLog::read(const QString &line) {
    if (m_hostLeft || !m_failure.isEmpty())
        return;
    // netplay_frontend.c's: the handshake done (netplay_handshake_ready(),
    // MSG_CONNECTED_TO), and a connection that has ended (netplay_hangup(),
    // MSG_NETPLAY_CLIENT_HANGUP), the host's gone or it stalled out.
    if (!m_hosting && line.contains(QLatin1String("[Netplay] Connected to"))) {
        m_connected = true;
        return;
    }
    if (!m_hosting && line.contains(QLatin1String("[Netplay] Netplay disconnected"))) {
        if (m_connected)
            m_hostLeft = true;
        else
            m_failure = m_errors.isEmpty() ? QStringLiteral("Netplay disconnected") : m_errors.last();
        return;
    }
    const std::optional<QString> error = netplayError(line);
    if (!error || m_connected)
        return;
    m_errors << *error;
    m_failure = netplayFailure(m_errors, m_hosting);
}

Run readLog(const QString &path, bool hosting) {
    Run run;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return run;
    static const QRegularExpression rom(QStringLiteral("\\[FBNeo\\] ROM at index \\d+ with name (\\S+) and CRC"));
    NetplayLog netplay(hosting);
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
        netplay.read(line);
        if (line.contains(QLatin1String("[ERROR]")))
            run.error = line.section(QLatin1String("[ERROR]"), 1).trimmed();
    }
    run.netplayFailure = netplay.failure();
    run.hostLeft = netplay.hostLeft();
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
        room.game = setOf(text("game_name"));
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

QByteArray lanQuery() {
    // Its DISCOVERY_QUERY_MAGIC, sent big-endian.
    return QByteArrayLiteral("RANQ");
}

std::optional<Room> readLanAnswer(const QByteArray &packet, const QString &address) {
    // Its struct ad_packet: "RANS", the game's CRC, the port to join at and
    // the passwords it asks for (big-endian numbers), then each string in a
    // field of its own size, ended by a NUL within it: who hosts, the host's
    // system, the core and its version, RetroArch's version, the game's file
    // (without its extension) and the subsystem it plays in.
    if (packet.size() != kLanAnswerSize || !packet.startsWith("RANS"))
        return std::nullopt;
    const auto number = [&packet](int at) { return qFromBigEndian<quint32>(packet.constData() + at); };
    int at = 16;
    const auto text = [&packet, &at](int size) {
        QByteArray field = packet.mid(at, size);
        at += size;
        if (const qsizetype end = field.indexOf('\0'); end >= 0)
            field.truncate(end);
        return QString::fromUtf8(field);
    };
    const QString host = text(32);
    const QString frontend = text(32);
    const QString core = text(32);
    const QString coreVersion = text(32);
    const QString retroarch = text(32);
    const QString content = text(256);
    const QString subsystem = text(256);
    Room room;
    room.rom = cleaned(content, 64);
    room.game = setOf(content);
    if (const quint32 crc = number(4))
        room.crc = QStringLiteral("%1").arg(crc, 8, 16, QLatin1Char('0')).toUpper();
    room.core = cleaned(core, 32);
    room.coreVersion = cleaned(coreVersion, 32);
    room.subsystem = cleaned(subsystem, 32);
    if (room.subsystem == QLatin1String("N/A"))
        room.subsystem.clear();
    room.retroarch = cleaned(retroarch, 16);
    room.frontend = cleaned(frontend, 32);
    room.host = cleaned(host, 32);
    const quint32 passwords = number(12);
    room.password = passwords & 1;
    room.spectatePassword = passwords & 2;
    room.joinAddress = joinAddressOf(address, static_cast<int>(number(8)), QString());
    // FinalBurn Neo, or OSD/OS's own for a CV1000 game (lanCorePath()).
    room.joinable = (room.core == kCoreName || room.core == kLanCoreName) && !room.game.isEmpty()
                    && !room.joinAddress.isEmpty();
    return room;
}

bool isLanAddress(quint32 ipv4) {
    return (ipv4 & 0xFF000000) == 0x0A000000 || (ipv4 & 0xFFF00000) == 0xAC100000
           || (ipv4 & 0xFFFF0000) == 0xC0A80000;
}

bool isLanSender(const QHostAddress &address) {
    bool ipv4 = false;
    const quint32 number = address.toIPv4Address(&ipv4);
    return ipv4 && (isLanAddress(number) || address.isLoopback());
}

static_assert(kMostPorts == controllers::kPlayers, "a port for each player the menus have a controller for");

namespace {
// The claims' packets: a query for so many ports, an answer giving the
// first and how many, and a release, each a line of ASCII with its numbers
// after it.
const char kClaimQueryHead[] = "OSDOS PORTS? ";
const char kClaimAnswerHead[] = "OSDOS PORTS ";
const char kClaimReleaseText[] = "OSDOS PORTS FREE";
const char kClaimLayoutHead[] = "OSDOS LAYOUT ";
const char kLayoutPanel[] = "panel";
const char kLayoutClassic[] = "classic";
const char kClaimPaceText[] = "OSDOS PACE game";

// A number of at most two digits, least to most; nothing for anything else.
std::optional<int> number(const QByteArray &digits, int least, int most) {
    if (digits.isEmpty() || digits.size() > 2
        || !std::all_of(digits.cbegin(), digits.cend(), [](char c) { return c >= '0' && c <= '9'; }))
        return std::nullopt;
    const int n = digits.toInt();
    if (n < least || n > most)
        return std::nullopt;
    return n;
}

// What follows head in packet, split at its spaces; nothing for a packet
// that doesn't begin with head.
std::optional<QList<QByteArray>> wordsAfter(const QByteArray &packet, const char *head) {
    if (!packet.startsWith(head))
        return std::nullopt;
    return packet.mid(static_cast<qsizetype>(qstrlen(head))).split(' ');
}
} // namespace

int localPorts(const QStringList &guids) {
    int last = 0;
    for (int p = 0; p < guids.size() && p < kMostPorts; ++p) {
        if (!guids.at(p).isEmpty())
            last = p + 1;
    }
    return std::max(1, last);
}

QByteArray claimQuery(int players) {
    return QByteArray(kClaimQueryHead) + QByteArray::number(players);
}

QByteArray claimAnswer(const PortRange &ports) {
    return QByteArray(kClaimAnswerHead) + QByteArray::number(ports.first) + ' ' + QByteArray::number(ports.players);
}

QByteArray claimRelease() {
    return QByteArray(kClaimReleaseText);
}

std::optional<int> readClaimQuery(const QByteArray &packet) {
    const std::optional<QList<QByteArray>> words = wordsAfter(packet, kClaimQueryHead);
    if (!words || words->size() != 1)
        return std::nullopt;
    return number(words->first(), 1, kMostPorts);
}

std::optional<PortRange> readClaimAnswer(const QByteArray &packet) {
    const std::optional<QList<QByteArray>> words = wordsAfter(packet, kClaimAnswerHead);
    if (!words || words->size() != 2)
        return std::nullopt;
    const std::optional<int> first = number(words->at(0), 0, kMostPorts);
    const std::optional<int> players = number(words->at(1), 0, kMostPorts);
    // None as { 0, 0 }; else a run of ports within the six.
    if (!first || !players || (*first == 0) != (*players == 0) || *first + *players - 1 > kMostPorts)
        return std::nullopt;
    return PortRange{ *first, *players };
}

bool isClaimRelease(const QByteArray &packet) {
    return packet == kClaimReleaseText;
}

QByteArray claimLayout(Launch::Layout layout) {
    return QByteArray(kClaimLayoutHead) + (layout == Launch::Layout::Panel ? kLayoutPanel : kLayoutClassic);
}

std::optional<Launch::Layout> readClaimLayout(const QByteArray &packet) {
    const std::optional<QList<QByteArray>> words = wordsAfter(packet, kClaimLayoutHead);
    if (!words || words->size() != 1)
        return std::nullopt;
    if (words->first() == kLayoutPanel)
        return Launch::Layout::Panel;
    if (words->first() == kLayoutClassic)
        return Launch::Layout::Classic;
    return std::nullopt;
}

QByteArray claimPace() {
    return QByteArray(kClaimPaceText);
}

bool isClaimPace(const QByteArray &packet) {
    return packet == kClaimPaceText;
}

Launch::Layout hostLayout(const QList<bool> &panels, const QList<QStringList> &arcade, int players) {
    // One machine takes one port fewer than the game's players at most (a
    // host leaves the machines joining one, and they find its own taken): up
    // to four players, three at most, as the panel's layout takes; five or
    // six may be four of one machine. A game whose players aren't known
    // keeps the panel: the host takes port 1 alone, and a machine joining is
    // given three ports at most (PortClaims::each()).
    if (players > kPanelPorts + 1)
        return Launch::Layout::Classic;
    for (int p = 0; p < kMostPorts; ++p) {
        if (panels.value(p) || !arcade.value(p).isEmpty())
            return Launch::Layout::Panel;
    }
    return Launch::Layout::Classic;
}

void PortClaims::reset(int own, int most, int each) {
    m_most = std::clamp(most, 1, kMostPorts);
    m_own = std::clamp(own, 0, m_most);
    m_each = std::clamp(each, 1, kMostPorts);
    m_claims.clear();
}

bool PortClaims::taken(int port) const {
    return port <= m_own || std::any_of(m_claims.cbegin(), m_claims.cend(), [port](const Claim &c) {
        return c.first <= port && port < c.first + c.players;
    });
}

PortRange PortClaims::claim(const QString &address, int players) {
    release(address);
    if (players < 1)
        return {};
    players = std::min(players, m_each);
    // The first run that takes them all; else the first free port and the
    // run after it.
    PortRange some;
    for (int first = m_own + 1; first <= m_most; ++first) {
        if (taken(first))
            continue;
        int run = 1;
        while (run < players && first + run <= m_most && !taken(first + run))
            ++run;
        if (run == players) {
            m_claims.append({ address, first, players });
            return { first, players };
        }
        if (some.first == 0)
            some = { first, run };
    }
    if (some.first > 0)
        m_claims.append({ address, some.first, some.players });
    return some;
}

void PortClaims::release(const QString &address) {
    m_claims.removeIf([&address](const Claim &c) { return c.address == address; });
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

std::optional<QString> bundleVersion(const QString &program) {
    const QString folder = QFileInfo(QFileInfo(program).canonicalFilePath()).absolutePath();
    if (!folder.endsWith(QLatin1String(".app/Contents/MacOS")))
        return std::nullopt;
    QFile plist(folder + QStringLiteral("/../Info.plist"));
    if (!plist.open(QIODevice::ReadOnly))
        return QString();
    // An XML property list, as Xcode leaves an app's; a binary one says
    // nothing.
    static const QRegularExpression version(
        QStringLiteral("<key>CFBundleShortVersionString</key>\\s*<string>\\s*(\\d+\\.\\d+(?:\\.\\d+)?)"));
    const QRegularExpressionMatch match = version.match(QString::fromUtf8(plist.readAll()));
    return match.hasMatch() ? match.captured(1) : QString();
}

QString achievementsHash(const QString &fileName) {
    return QString::fromLatin1(QCryptographicHash::hash(QFileInfo(fileName).completeBaseName().toUtf8(),
                                                        QCryptographicHash::Md5).toHex());
}

} // namespace libretro
