#pragma once
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QSize>
#include <QString>
#include <QStringList>

// The Arcade module's second engine: RetroArch with the FinalBurn Neo core
// (FBNeo), for the games it has, where RetroAchievements' high scores and
// online play are. What is asked of it and made of what it says, with no
// process of its own, so tests/arcade_test.cpp can drive it.
namespace libretro {

// RetroArch's program: <data>/bin/retroarch (one of the user's own, as
// MAME's drop-in is), else retroarch on the PATH, else Debian's
// /usr/bin/retroarch or the Mac's RetroArch.app. "" for none.
QString programPath(const QString &dataRoot);

// The FinalBurn Neo core: the one Settings → Arcade → FinalBurn Neo
// downloaded (<data>/arcade/retroarch/cores), else the OSD/OS image's
// (/usr/local/lib/libretro), else one installed for the system's or the
// user's RetroArch. "" for none.
QString corePath(const QString &dataRoot);
// FinalBurn Neo's list of its arcade games (its DAT), downloaded or the
// image's, as corePath() looks; "" for none.
QString datPath(const QString &dataRoot);
// Where Settings → Arcade → FinalBurn Neo puts the core, its list and its
// licence: <data>/arcade/retroarch/cores/fbneo_libretro.so (.dylib on a
// Mac), <data>/arcade/retroarch/fbneo-arcade.dat, and fbneo-license.txt.
QString downloadedCore(const QString &dataRoot);
QString downloadedDat(const QString &dataRoot);
QString downloadedLicense(const QString &dataRoot);

// FinalBurn Neo's arcade games, from its DAT (ClrMame Pro's XML): each
// game's set by its description. Not a BIOS set (neogeo), which games start
// from but which isn't one. Empty for a file that can't be read. With
// aspects, the shape of each game's picture on the glass, its width over its
// height (4:3, 3:4 for a vertical one), where the DAT says (<video>).
QHash<QString, QString> readDat(const QString &path, QHash<QString, double> *aspects = nullptr);

// The shape of a game's picture in the pixels of a screen of that size and
// shape (screenAspect, 4:3 for a tube), for RetroArch (Launch::aspect):
// gameAspect, its shape on the glass, as much wider as the screen's pixels
// are narrower (720×240, JAMMA 240p, a 4:3 tube: a 4:3 game 3.0, a vertical
// one 1.6875). 0 for no size.
double viewportAspect(double gameAspect, const QSize &screen, double screenAspect);

// The device RetroArch keeps for a player (input_playerN_reserved_device):
// "vvvv:pppp Name", the vendor and product its SDL GUID holds, as
// RetroArch's own menu writes it; "" for a GUID without them.
QString reservedDevice(const QString &guid, const QString &name);

// What a game is started with.
struct Launch {
    QString core;
    QString content;          // the set's file
    QString config;           // the retroarch.cfg written for it (configText())
    QString log;              // where RetroArch logs this run
    QString systemDir;        // FinalBurn Neo's BIOS sets in its fbneo
    QString saveDir;          // NVRAM and high scores, in its fbneo
    QString stateDir;
    QString configDir;        // the core's options
    bool    console = true;   // a Linux console, not a desktop: KMS, udev, ALSA
    bool    sound = true;
    // Settings → Arcade → Emulator Video: the picture's shape in the
    // screen's pixels where they aren't square (viewportAspect()), 0 for the
    // core's on square ones; and its lines drawn whole where they fit.
    double  aspect = 0;
    bool    integerScale = false;
    // Each player's controller, as reservedDevice() makes it: "" for none.
    QStringList pads;
    // RetroAchievements: the account, signed in with its token, or with its
    // password for RetroArch to swap for one (it saves the token on exit).
    QString achievementsUser;
    QString achievementsToken;
    QString achievementsPassword;
    // Online play: hosting through a relay (netplay_mitm_server), or
    // joining a room as the lobby lists it.
    enum class Netplay { None, Host, Join } netplay = Netplay::None;
    QString nickname;
    QString relay;            // "nyc", "madrid", "saopaulo", "singapore", "chuncheon"
    QString joinAddress;      // RetroArch's --connect: "address|port" or "address|port|session"
};
// The retroarch.cfg for a game: everything RetroArch would otherwise take
// from a desktop's defaults, for a console with the menus' controllers.
QString configText(const Launch &launch);
// RetroArch's arguments: the core, the config, the log and the game.
QStringList arguments(const Launch &launch);

// What a run's log says (RetroArch's own, -v --log-file).
struct Run {
    bool logged = false;          // there was a log to read
    bool started = false;         // FinalBurn Neo started the game
    QStringList missing;          // files it needed and found none of
    QString loginFailure;         // why RetroAchievements didn't take the account
    QString error;                // RetroArch's last error line
};
Run readLog(const QString &path);
// Whether a sign-in failure (Run::loginFailure) is RetroAchievements turning
// the account down, a password or a token it doesn't take, rather than not
// being reached ("No response from server"), which leaves a token as it is.
bool loginRefused(const QString &failure);
// The token RetroArch saved in a config it wrote back on exit; "" for none.
QString savedToken(const QString &configPath);

// A room the netplay lobby lists (lobby.libretro.com/list): a game
// FinalBurn Neo hosts, joined at joinAddress. What anyone hosting wrote, so
// taken only as it can be used: a set's name, an address, a port and a
// session, and the host's name without markup, 32 characters at most.
struct Room {
    int     id = 0;
    QString game;             // its set: game_name, the file's name in lower case
    QString host;             // who hosts it
    QString country;          // its two letters, "" for none
    QString joinAddress;      // RetroArch's --connect: through the relay when it is relayed
    bool    password = false;
};
// The lobby's rooms FinalBurn Neo hosts that can be joined (connectable),
// newest first. Empty for an answer that isn't the lobby's.
QList<Room> readLobby(const QByteArray &json);

// RetroAchievements' hash of an arcade game (rcheevos' RC_CONSOLE_ARCADE):
// the MD5 of its file's name without the extension, as it is written.
QString achievementsHash(const QString &fileName);

} // namespace libretro
