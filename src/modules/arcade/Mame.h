#pragma once
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QXmlStreamReader>

// What the Arcade module asks of MAME and makes of its answers, with no
// process of its own, so tests/arcade_test.cpp can drive it; and where MAME
// and its plugins are, for the High Scores module too.
namespace mame {

// MAME's program: <data>/bin/mame (one of the user's own, as yt-dlp's
// drop-in is), else mame on the PATH, else Debian's /usr/games/mame, which a
// systemd service's PATH leaves out, the BSDs' and Homebrew's; on Windows
// the one Settings → Arcade → MAME downloaded (downloadedMame()), a MAME
// folder beside OSD/OS, or where its builds are commonly unpacked. "" for
// none.
QString programPath(const QString &dataRoot);
// Where Settings → Arcade → MAME puts mamedev's build on Windows:
// <data>/arcade/mame/mame.exe, its own folders (plugins, hash, bgfx…)
// beside it as the build has them.
QString downloadedMame(const QString &dataRoot);
// The plugins folder of that MAME, with the hiscore plugin in it: beside the
// program as installed (Debian's /usr/games/../share/games/mame/plugins,
// Homebrew's bin/../share/mame/plugins, or a plugins folder of its own),
// else Debian's own. "" when there is none.
QString pluginsFolder(const QString &program);

// A machine as -listxml describes it: what the game list shows and sorts by.
struct Machine {
    QString name;          // its short name, its set's: "pacman"
    QString description;   // "Pac-Man (Midway)"
    QString year;          // "1980", or "198?" or "1980?" where MAME isn't sure
    QString manufacturer;  // "Namco (Midway license)"
    QString cloneOf;       // its parent's short name, for a clone
    QString sourceFile;    // its driver's source: "capcom/cps1.cpp" (MAME before 0.229: "cps1.cpp")
    QString status;        // its driver's: "good", "imperfect" or "preliminary"
    int     players = 0;   // as many as play at once
    int     rotate = 0;    // its (first) screen's rotation: 0, 90, 180 or 270
    // Its (first) screen's size in the game's own pixels, before the
    // rotation: 0 where MAME doesn't say (a vector screen).
    int     width = 0;
    int     height = 0;
    bool    bios = false;  // a BIOS set (neogeo), which other games start from
    bool    device = false;
    bool    runnable = true;

    // A game to list: not a BIOS or a device, and MAME can run it.
    bool isGame() const { return !bios && !device && runnable; }
    QVariantMap toVariant() const;
    static Machine fromVariant(const QString &name, const QVariantMap &map);
};

// Reads -listxml as it comes, a chunk at a time.
class ListXmlReader {
public:
    // Ready for another run's output.
    void reset();
    void addData(const QByteArray &chunk);
    // The machines read whole since the last call.
    QList<Machine> take();
    // MAME's build, as its root element has it: "0.276 (unknown)".
    QString build() const { return m_build; }
    // Not XML MAME wrote (a premature end is no error: more may come).
    bool failed() const;

private:
    void read();

    QXmlStreamReader m_xml;
    QList<Machine>   m_done;
    Machine          m_machine;
    bool             m_inMachine = false;
    int              m_depth = 0;      // within the machine element
    bool             m_hadDisplay = false;
    // A text element of the machine's being read (description, year,
    // manufacturer) and its text so far: it may come over several chunks.
    QString          m_field;
    QString          m_text;
    QString          m_build;
};

// What MAME is told of the screen (Settings → Arcade → Emulator Video): its
// shape ("4:3", "16:9"), where its pixels aren't square (720×240 for JAMMA
// 240p is a 4:3 tube, which MAME would take as 3:1 and squeeze the picture
// on; "" leaves them square); and with integer, that a game's lines are
// drawn whole, once or twice, the width to the picture's shape, where at
// least one of each fits in the screen's (screenLines): a game with more is
// spread over them, as MAME would otherwise draw it once and cut it.
QStringList screenArguments(const Machine &machine, int screenLines, const QString &shape, bool integer);

// A set's file or folder name MAME may know: letters, digits and underscores
// only. Anything else (a space, a wildcard MAME would match against every
// machine it has) can't be one, and MAME isn't asked about it.
bool isShortName(const QString &name);

// The maker a game is listed under: "Namco (Midway license)" → "Namco",
// "Capcom / Mitchell" → "Capcom"; a company under its own name whichever of
// its arms, mergers and legal forms made the game: "Nintendo of America" →
// "Nintendo", "SNK Playmore" → "SNK", "Taito America Corporation" → "Taito".
// "" for none.
QString makerOf(const QString &manufacturer);
// Its decade: "1981", "198?" and "1981?" → "1980s"; "" when the year doesn't
// say ("19??").
QString decadeOf(const QString &year);
// A game's title, its description without what tells its versions apart:
// "Galaga (Namco rev. B)" → "Galaga", "Truxton / Tatsujin" → "Truxton".
QString titleOf(const QString &description);
// Whether a machine is a bootleg or a hack of a game rather than its maker's
// own release, as MAME names them: its description says so in parentheses
// ("Galaga (bootleg)", "Pac-Man (Midway, speedup hack)", "Puckman (Japan
// set 1, Probably Bootleg)"), or its maker does ("bootleg", "bootleg
// (Playmark)", "hack").
bool isBootlegOrHack(const Machine &machine);
// The board or system a driver is for, as the Arcade screen names it, from
// its source file: "capcom/cps1.cpp" → "CPS-1", "neogeo/neogeo.cpp" →
// "Neo-Geo"; any other, the driver's own name: "nintendo/dkong.cpp" →
// "DKONG". "" for none.
QString systemOf(const QString &sourceFile);

// The genre arcade players know a game by, from its catver.ini category
// (progettosnaps' list), without the "* Mature *" mark and a TTL game's
// "TTL *": "Fighter / Versus" is Fighting, "Fighter / 2.5D" Beat 'em Up
// (Final Fight), "Shooter / Flying Vertical" Shmup, "Platform / Shooter
// Scrolling" Run and Gun (Metal Slug), "Shooter / Gun" Light Gun, "Driving /
// …" Racing; any other is its main category: Platform, Sports, Puzzle, Maze,
// Climbing, Shooter…
QString genreOf(const QString &category);

// The genres of catver.ini: each set's, genreOf() its category:
// "sf2=Fighter / Versus" → "sf2" → "Fighting". Empty for a file that can't
// be read.
QHash<QString, QString> readGenres(const QString &path);

// The Lua MAME runs as the game starts (-autoboot_script) to put the credits
// bought on the Arcade screen in: player 1's coin pressed that many times,
// from a few seconds in, once the game is past its start-up checks.
QString coinScript(int coins);

// The controller file MAME is started with (-ctrlr): each player's controller
// (Settings → Controls) as MAME's joystick of that number, by its SDL GUID
// (mapdevice; a GUID a second player shares, two pads of one model, is left
// to MAME's order), and Coin and Start together on any player's, or Esc, to
// leave the game, MAME's own way out, so it saves what it keeps on the way.
// arcade: each player's Arcade Buttons as MAME names its joystick's inputs
// (controllers::ArcadeBinds::mame: Button 1 to 8, Coin and Start; "" for
// none), empty for a player who has none: that player's buttons, coin and
// start are those, beside MAME's keys for them, and Coin and Start the way
// out.
QString controllerConfig(const QStringList &playerGuids, const QList<QStringList> &arcade = {});

// What a game that couldn't start lacked, from MAME's output: the files it
// couldn't find, and the sets it looked for them in (its own, its parent's,
// a BIOS's).
struct Missing {
    QStringList files;
    QStringList sets;
};
Missing missingFiles(const QString &output);

// The line MAME printed that says what went wrong ("Fatal error: …"): the
// last of its last few lines that says error, else its last line, never one
// of ALSA's (the library's complaints about a missing sound card).
QString lastError(const QString &output);

} // namespace mame
