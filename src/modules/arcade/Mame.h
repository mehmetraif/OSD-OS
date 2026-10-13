#pragma once
#include <QByteArray>
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
// systemd service's PATH leaves out. "" for none.
QString programPath(const QString &dataRoot);
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
    QString status;        // its driver's: "good", "imperfect" or "preliminary"
    int     players = 0;   // as many as play at once
    int     rotate = 0;    // its (first) screen's rotation: 0, 90, 180 or 270
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

// A set's file or folder name MAME may know: letters, digits and underscores
// only. Anything else (a space, a wildcard MAME would match against every
// machine it has) can't be one, and MAME isn't asked about it.
bool isShortName(const QString &name);

// The maker a game is listed under: "Namco (Midway license)" → "Namco",
// "Capcom / Mitchell" → "Capcom". "" for none.
QString makerOf(const QString &manufacturer);
// Its decade: "1981", "198?" and "1981?" → "1980s"; "" when the year doesn't
// say ("19??").
QString decadeOf(const QString &year);

// The controller file MAME is started with (-ctrlr): each player's controller
// (Settings → Controls) as MAME's joystick of that number, by its SDL GUID
// (mapdevice; a GUID a second player shares, two pads of one model, is left
// to MAME's order), and Coin and Start together on player 1's, or Esc, to
// leave the game, MAME's own way out, so it saves what it keeps on the way.
QString controllerConfig(const QStringList &playerGuids);

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
