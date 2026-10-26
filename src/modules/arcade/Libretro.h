#pragma once
#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QSize>
#include <QString>
#include <QStringList>
#include <optional>

class QHostAddress;
namespace mame { struct Machine; }

// The Arcade module's second engine: RetroArch with the FinalBurn Neo core
// (FBNeo), for the games it has, where RetroAchievements' high scores and
// online play are. What is asked of it and made of what it says, with no
// process of its own, so tests/arcade_test.cpp can drive it.
namespace libretro {

// RetroArch's program: <data>/bin/retroarch (one of the user's own, as
// MAME's drop-in is), else retroarch on the PATH, else Debian's
// /usr/bin/retroarch, the BSDs' /usr/local/bin/retroarch or the Mac's
// RetroArch.app; on Windows the one Settings → Arcade → FinalBurn Neo
// downloaded (downloadedRetroArch()), a RetroArch folder beside OSD/OS, or
// where its builds and installers commonly put it. "" for none.
QString programPath(const QString &dataRoot);
// Where Settings → Arcade → FinalBurn Neo puts RetroArch on Windows:
// <data>/arcade/retroarch/RetroArch-Win64/retroarch.exe.
QString downloadedRetroArch(const QString &dataRoot);

// The FinalBurn Neo core: the one Settings → Arcade → FinalBurn Neo
// downloaded (<data>/arcade/retroarch/cores), else the one OSD/OS's package
// brought (packagedCore()), else the OSD/OS image's (/usr/local/lib/libretro),
// else one installed for the system's or the user's RetroArch. "" for none.
QString corePath(const QString &dataRoot);
// FinalBurn Neo's list of its arcade games (its DAT), downloaded, the
// package's or the image's, as corePath() looks; "" for none.
QString datPath(const QString &dataRoot);
// Where Settings → Arcade → FinalBurn Neo puts the core, its list and its
// licence: <data>/arcade/retroarch/cores/fbneo_libretro.so (.dylib on a
// Mac), <data>/arcade/retroarch/fbneo-arcade.dat, and fbneo-license.txt.
QString downloadedCore(const QString &dataRoot);
QString downloadedDat(const QString &dataRoot);
QString downloadedLicense(const QString &dataRoot);
// Where OSD/OS's FreeBSD and OpenBSD packages put the core they bring, built
// from FinalBurn Neo's source for want of a build of libretro's
// (scripts/build-fbneo.sh), with its list and licence: under the app's own
// prefix, <prefix>/lib/osdos/ and <prefix>/share/osdos/fbneo/, the prefix
// being the folder above the app's (/usr/local).
QString packagedCore();
QString packagedDat();
QString packagedLicense();

// FinalBurn Neo's arcade games, from its DAT (ClrMame Pro's XML): each
// game's set by its description. Not a BIOS set (neogeo), which games start
// from but which isn't one. Empty for a file that can't be read. With
// aspects, the shape of each game's picture on the glass, its width over its
// height (4:3, 3:4 for a vertical one), where the DAT says (<video>). With
// machines, each game as MAME would describe it, for a set MAME doesn't
// know: its description, year, maker, parent, driver (named as MAME's are:
// capcom/d_cps1.cpp is capcom/cps1.cpp), whether it works, and a vertical
// one's rotation (90); no players, which the DAT doesn't say.
QHash<QString, QString> readDat(const QString &path, QHash<QString, double> *aspects = nullptr,
                                QHash<QString, mame::Machine> *machines = nullptr);

// The shape of a game's picture in the pixels of a screen of that size and
// shape (screenAspect, 4:3 for a tube), for RetroArch (Launch::aspect):
// gameAspect, its shape on the glass, as much wider as the screen's pixels
// are narrower (720×240, JAMMA 240p, a 4:3 tube: a 4:3 game 3.0, a vertical
// one 1.6875). 0 for no size.
double viewportAspect(double gameAspect, const QSize &screen, double screenAspect);

// Whether RetroArch knows a pad by its vendor and product on this system.
// Its SDL2 joypad driver reads them out of SDL's GUID on Linux alone: on
// Windows it takes the GUID's bus and checksum for them, elsewhere nothing,
// so a pad kept for a player by them is never found, and RetroArch gives it
// the first port kept for no one (a lone pad plays as player 2).
#ifdef Q_OS_LINUX
inline constexpr bool kPadsByVendor = true;
#else
inline constexpr bool kPadsByVendor = false;
#endif

// The device RetroArch keeps for a player (input_playerN_reserved_device).
// byVendor (kPadsByVendor): "vvvv:pppp Name", the vendor and product its
// SDL GUID holds, as RetroArch's own menu writes it, or the name alone for
// a GUID without them. Otherwise the name RetroArch's SDL gives the pad:
// its mapping's among those OSD/OS hands it (controllers::padMappings()),
// whatever the GUID's checksum, else name.
QString reservedDevice(const QString &guid, const QString &name, bool byVendor);

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
    // Kept for its player where RetroArch knows pads by vendor and product
    // (padsByVendor, kPadsByVendor); where it knows them by name, only
    // preferred, so that one it names otherwise plays on the port it comes
    // on rather than being moved off it.
    QStringList pads;
    bool padsByVendor = true;
    // Which players' controllers are arcade panels, a JAMMA board's sticks:
    // the cabinet's rows of buttons as FinalBurn Neo's 6-Button Panel has
    // them (configText()'s binds, remapText()'s device), off line only.
    QList<bool> panels;
    // Each player's Arcade Buttons as RetroArch binds them
    // (controllers::ArcadeBinds::retroarch: Button 1 to 8, Coin and Start,
    // "" for none), empty for a player who has none: off line, on FinalBurn
    // Neo's 6-Button Panel where it has Button 1 to 6, in place of a panel's
    // or the Standard Gamepad's.
    QList<QStringList> arcade;
    // Where RetroArch looks for remap files (coreRemapPath()).
    QString remapDir;
    // Where it reads the core's info file (coreInfoPath()).
    QString infoDir;
    // RetroAchievements: the account, signed in with its token, or with its
    // password for RetroArch to swap for one (it saves the token on exit).
    QString achievementsUser;
    QString achievementsToken;
    QString achievementsPassword;
    // Online play: hosting through a relay (netplay_mitm_server), hosting
    // on the local network alone (LanHost, LAN Play), or joining a room as
    // the lobby gives it (Join) or a host on the local network (LanJoin).
    enum class Netplay { None, Host, LanHost, Join, LanJoin } netplay = Netplay::None;
    QString nickname;
    QString relay;            // "nyc", "madrid", "saopaulo", "singapore", "chuncheon"
    QString joinAddress;      // RetroArch's --connect: "address|port" or "address|port|session"
    // The game's ports this machine's players take, portsFrom and as many as
    // ports (netplay_request_device_pN), its player 1 the first of them; none
    // (ports 0) leaves RetroArch to its own: one port, the first free.
    int     portsFrom = 0;
    int     ports = 0;
    // Online, the controller types every port plays with. RetroArch gives
    // every machine the host's, port by port, and a machine's players find
    // their pads only among ports of one kind (its pad n drives the n-th of
    // its ports of that kind), so a host sets one kind for all six: Classic,
    // RetroArch's own, as a plain RetroArch host has them; or Panel, FinalBurn
    // Neo's 6-Button Panel, a host with an arcade panel's players
    // (hostLayout()), which its OSD/OS tells the machines joining before
    // their ports (claimLayout()). The binds follow it: a panel's player on
    // kArcadePanel under Panel, on kArcadePanelClassic under Classic; a pad's
    // Arcade Buttons under Panel alone. Off line each player has their own
    // (onPanel()), whatever this says.
    enum class Layout { Classic, Panel } layout = Layout::Classic;
};
// Whether a game is hosted (Host, LanHost), rather than joined or played alone.
constexpr bool hosts(Launch::Netplay netplay) {
    return netplay == Launch::Netplay::Host || netplay == Launch::Netplay::LanHost;
}
// The layout a host plays an online game with: Panel where one of its
// players is on an arcade panel (panels) or has Arcade Buttons (arcade),
// unless the game plays five or six players at once (players; 0 where not
// known), where one machine may bring more than the Panel layout's ports
// (kPanelPorts): Classic then, as without a panel.
Launch::Layout hostLayout(const QList<bool> &panels, const QList<QStringList> &arcade, int players);
// The retroarch.cfg for a game: everything RetroArch would otherwise take
// from a desktop's defaults, for a console with the menus' controllers.
QString configText(const Launch &launch);
// RetroArch's arguments: the core, the config, the log and the game.
QStringList arguments(const Launch &launch);
// The remap file RetroArch loads with FinalBurn Neo from remapDir: the
// core's own, by its name.
QString coreRemapPath(const QString &remapDir);
// Its text: off line, each player on an arcade panel, or with Arcade Buttons,
// on FinalBurn Neo's 6-Button Panel, its controller type for such a panel;
// hosting on the Panel layout, every port on it, the machines joining taking
// the host's types; "" for none, when no file is needed (joining: the host's
// types come with the connection).
QString remapText(const Launch &launch);
// Whether the remap file at path is OSD/OS's to write over or remove: there
// is none, or remapText() wrote it. One saved from RetroArch's menu (Save
// Core Remap File) or put there by hand is the user's, loaded as it is.
bool ownsRemap(const QString &path);
// FinalBurn Neo's core options file in configDir, where RetroArch keeps a
// core's options (<core name>/<core name>.opt, global_core_options off), and
// its text with the option RetroAchievements' hardcore mode wants: RetroArch
// pauses hardcore while a core option it disallows is set ("Hardcore paused.
// Setting not allowed: fbneo-allow-patched-romsets=enabled"), and FinalBurn
// Neo's default allows patched sets. withHardcoreOptions() sets that line, or
// adds it, and leaves the rest of the text as it is: the same text back when
// nothing is to change.
QString coreOptionsPath(const QString &configDir);
QString withHardcoreOptions(const QString &text);
// FinalBurn Neo's info file, which RetroArch looks for in infoDir by the
// core's name (fbneo_libretro.info), and the text OSD/OS writes in it before
// each game: what libretro's own information on the core says that RetroArch
// acts on, above all that its states come out the same on every machine,
// without which RetroArch neither hosts nor joins a game with it ("Core does
// not support netplay").
QString coreInfoPath(const QString &infoDir);
QString coreInfoText();

// What a run's log says (RetroArch's own, -v --log-file). Over the network,
// hosting or joining (hosting says which), netplayFailure is why RetroArch
// couldn't, after which it played the game alone (netplayFailure()).
struct Run {
    bool logged = false;          // there was a log to read
    bool started = false;         // FinalBurn Neo started the game
    QStringList missing;          // files it needed and found none of
    QString loginFailure;         // why RetroAchievements didn't take the account
    QString netplayFailure;       // why it couldn't host or join the game
    QString error;                // RetroArch's last error line
};
Run readLog(const QString &path, bool hosting = false);
// A line of the log that is one of RetroArch's netplay errors ("[ERROR]
// [Netplay] ..."): what it says, its full stop dropped; nothing for any other
// line.
std::optional<QString> netplayError(const QString &line);
// Of a run's netplay errors so far, in order (netplayError()), why RetroArch
// couldn't host or join the game, after which it says so once on its screen
// and plays on alone; "" while nothing has ended the game. Joining, the host
// not reached or the handshake refused (another core, an old RetroArch, the
// host full, the password, a ban, what the host sent not received) ends it by
// itself. Hosting, only "Failed to initialize netplay" does, which every start
// that fails ends in, and the failure is the most telling line before it (the
// port, the sockets, the relay, the core): a port missed over IPv6 is logged
// too, and then bound over IPv4. A warning (versions, CRCs), a client a host
// turns away, input out of order and a peer that leaves let the game go on.
QString netplayFailure(const QStringList &errors, bool hosting);
// Whether a sign-in failure (Run::loginFailure) is RetroAchievements turning
// the account down, a password or a token it doesn't take, rather than not
// being reached ("No response from server"), which leaves a token as it is.
bool loginRefused(const QString &failure);
// The token RetroArch saved in a config it wrote back on exit; "" for none.
QString savedToken(const QString &configPath);

// The RetroAchievements account checked as it is signed in (Settings →
// Arcade → RetroAchievements Account), not at the next game: RetroArch, with
// no picture, sound or controllers, runs OSD/OS's sign-in core
// (osdos_signin_libretro, src/modules/arcade/signin) on a file no game is
// of. It signs in with the password as it would at a game, then finds no
// game, so no session of one is started; and on exit it saves the token in
// the password's place, as after a game.
struct SignIn {
    QString core;             // osdos_signin_libretro.so
    QString content;          // the file it opens, <dir>/sign-in.osdos
    QString config;           // the retroarch.cfg written for it
    QString dir;              // where it keeps what it writes
    QString user;
    QString password;
};
QString signInConfigText(const SignIn &signIn);
// The core, the config and the file; its log to its output (-v), read as it
// comes.
QStringList signInArguments(const SignIn &signIn);
// What a line RetroArch printed says of the sign-in: nothing (None), that it
// signed in, or that it was refused, why in failure ("Invalid User/Password
// combination. Please try again"), or that RetroAchievements didn't answer.
enum class SignInLine { None, SignedIn, Failed, Unreachable };
SignInLine readSignInLine(const QString &line, QString *failure);

// A room the netplay lobby lists (lobby.libretro.com/list), or a host on the
// local network answers with (readLanAnswer()), of any core; one FinalBurn
// Neo hosts is joined at joinAddress. What anyone hosting wrote, so taken
// only as it can be shown or used: names without markup and cut short, a
// set's name, a CRC of eight hex digits, an address, a port and a session.
struct Room {
    int     id = 0;
    QString game;             // its set: game_name, the file's name in lower case; "" for a name that can't be one
    QString rom;              // game_name as its host has it, its file's name, 64 characters at most
    QString crc;              // game_crc, in capitals; "" for none
    QString core;             // core_name: "FinalBurn Neo", "Snes9x"
    QString coreVersion;
    QString subsystem;        // the core's subsystem it plays in, "" for none
    QString retroarch;        // retroarch_version, the host's
    QString frontend;         // the host's system: "win32 x64", "unix ARMv8"
    QString host;             // who hosts it, 32 characters at most
    QString country;          // its two letters, "" for none
    QString joinAddress;      // RetroArch's --connect: through the relay when it is relayed
    QString relay;            // the relay it goes through ("us-east1"), "" for a room reached straight
    QDateTime created;        // when it was hosted; invalid where the lobby doesn't say
    int     players = -1;     // playing in it, -1 where the lobby doesn't say
    int     spectators = -1;
    bool    password = false;
    bool    spectatePassword = false;
    bool    connectable = true;   // as RetroArch's menu reads it: unless the lobby says not
    // One RetroArch joins with FinalBurn Neo: its core, connectable, hosted
    // in a RetroArch the menu lists rooms of (1.9.1 on), for a set, at an
    // address.
    bool    joinable = false;
};
// Every room the lobby lists, newest first: the newest thousand of an answer
// with more. Empty for an answer that isn't the lobby's.
QList<Room> readWholeLobby(const QByteArray &json);
// Of them, those FinalBurn Neo hosts that can be joined (joinable).
QList<Room> readLobby(const QByteArray &json);

// LAN Play, RetroArch's netplay on the local network: a host that isn't
// relayed answers a query broadcast to its port (lanQuery()) with what it
// plays, a packet of kLanAnswerSize bytes (RetroArch's struct ad_packet).
constexpr quint16 kLanPort = 55435;
constexpr int kLanAnswerSize = 688;
QByteArray lanQuery();
// A host's answer, from address, as a room: what it plays, who hosts it, and
// where it is joined, address and the port it gives; its id 0, joinable as a
// lobby's room of FinalBurn Neo's is. Nothing for a packet that isn't one.
std::optional<Room> readLanAnswer(const QByteArray &packet, const QString &address);
// Whether an IPv4 address (a number) is one RetroArch answers queries from:
// 10/8, 172.16/12 or 192.168/16.
bool isLanAddress(quint32 ipv4);
// Whether an answer from address is taken: from a host on the local network,
// as RetroArch takes them (isLanAddress()), or from this machine.
bool isLanSender(const QHostAddress &address);

// Six players over the network, a host's and each joiner's together.
// RetroArch gives the machine hosting port 1 and each machine joining one
// port, the first free, unless a machine asks for its ports by number
// (Launch::portsFrom): the ports its players drive in order, its player 1
// the first asked for. So a host asks for ports 1 to its players' count
// (localPorts(), one fewer than the game plays at once at most, so that a
// machine joining has a port) and its OSD/OS keeps count of the ports the
// machines joining take (PortClaims): each OSD/OS joining asks it, at
// kLanClaimPort, for ports for its players (claimQuery()) and is answered
// with the first of theirs and how many (claimAnswer()), none when the game
// has none left; one leaving gives its ports back (claimRelease()). A host
// that doesn't answer, RetroArch alone or an older OSD/OS, holds port 1: a
// joiner then asks for ports from 2 with more than one player, else for
// none. Before the ports, a host says the layout it plays with
// (claimLayout(), Launch::layout), which an older OSD/OS doesn't: Classic
// then.
constexpr quint16 kLanClaimPort = 55436;
// The ports a game is played on at most: FinalBurn Neo's six-player games,
// as many as the players the menus have controllers for.
constexpr int kMostPorts = 6;
// The ports one machine's players take at most on the Panel layout.
// RetroArch 1.20 sends a machine's input for a frame in one packet of 16
// words, 4 of them its header, and a 6-Button Panel's port takes 3 (its
// buttons and two sticks, an analog device's), a Classic port 1: a fourth
// panel port's input is left out, the packet comes short, and the host
// drops the machine ("NETPLAY_CMD_INPUT received an unexpected payload
// size"), after which each plays on alone, deaf to the other. A game of at
// most four players never needs more of one machine (hostPorts() leaves the
// others one port at least); one of five or six is played on Classic
// (hostLayout()).
constexpr int kPanelPorts = 3;
// The ports one machine's players take at most on a layout.
constexpr int mostPortsOn(Launch::Layout layout) {
    return layout == Launch::Layout::Panel ? kPanelPorts : kMostPorts;
}
// How many ports a machine's players take: up to the last player with a
// controller (the players' GUIDs, controllers::playerGuids()), one at least.
int localPorts(const QStringList &guids);
// The ports a machine joining is given: the first and how many; none as
// { 0, 0 }.
struct PortRange {
    int first = 0;
    int players = 0;
};
QByteArray claimQuery(int players);
QByteArray claimAnswer(const PortRange &ports);
QByteArray claimRelease();
QByteArray claimLayout(Launch::Layout layout);
// What a packet says: a query for so many ports (1 to kMostPorts), an answer
// giving the first and how many (within kMostPorts, or none), a release, or
// the host's layout; nothing for a packet that isn't one.
std::optional<int> readClaimQuery(const QByteArray &packet);
std::optional<PortRange> readClaimAnswer(const QByteArray &packet);
bool isClaimRelease(const QByteArray &packet);
std::optional<Launch::Layout> readClaimLayout(const QByteArray &packet);

// A host's count of the ports taken: its own, 1 to own, and each joiner's
// by its address, within the game's ports (most), each joiner's players no
// more than each (mostPortsOn() the layout). A joiner is given the first run
// of free ports that takes all its players, else the first free port and
// what follows it; one asking again is given its ports anew, and one leaving
// gives them up.
class PortClaims {
public:
    void reset(int own, int most = kMostPorts, int each = kMostPorts);
    // The joiner's ports, none ({ 0, 0 }) when none is left (or no player).
    PortRange claim(const QString &address, int players);
    void release(const QString &address);
    int own() const { return m_own; }
    int most() const { return m_most; }
    int each() const { return m_each; }

private:
    struct Claim {
        QString address;
        int first = 0;
        int players = 0;
    };
    bool taken(int port) const;
    int m_own = 0;
    int m_most = kMostPorts;
    int m_each = kMostPorts;
    QList<Claim> m_claims;
};

// A libretro core as it describes itself (retro_get_system_info): its name
// and version ("v1.0.0.03 6e7c2a1"), the version the lobby says a room's
// host plays in.
struct CoreInfo {
    QString name;
    QString version;
};
// The core at path, loaded in this process and asked to describe itself, no
// other function of its called (what it does as it loads, and as the
// process ends, still runs). Empty, with why in error, for a file that isn't
// one. For a copy of the app run with --core-info <path> (main.cpp), which
// prints it, so that a core that can't be loaded takes nothing else down.
CoreInfo loadCoreInfo(const QString &path, QString *error);
// What that copy printed ("name=…", "version=…" lines), taken as the
// lobby's names are: no markup and cut short, so the two can be compared.
CoreInfo readCoreInfo(const QByteArray &output);
// RetroArch's version as `retroarch --version` prints it ("Version: 1.19.1
// (Git 5a4f9b2)", an older one's "-- v1.9.0 --"); "" for none.
QString readVersion(const QByteArray &output);
// The version of the Mac app program is the executable of, or a link to
// (<name>.app/Contents/MacOS/<program>), as its Info.plist gives it
// (CFBundleShortVersionString, "1.22.2"): RetroArch.app opens as an app
// before it reads its arguments, so isn't asked. Nothing for a program
// outside an app; "" for an app whose Info.plist doesn't say.
std::optional<QString> bundleVersion(const QString &program);

// RetroAchievements' hash of an arcade game (rcheevos' RC_CONSOLE_ARCADE):
// the MD5 of its file's name without the extension, as it is written.
QString achievementsHash(const QString &fileName);

} // namespace libretro
