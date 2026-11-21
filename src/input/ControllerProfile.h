#pragma once
#include <QHash>
#include <QList>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class QProcessEnvironment;

// Settings → Controls, each controller apart: what InputManager knows of one
// controller and keeps for it, without SDL or evdev, so the tests can drive it.
//
// A controller is a gamepad (SDL's, the JAMMA board's two among them) or, on
// Linux, a keyboard or a remote (its /dev/input nodes, read beside Qt). Its
// key names it in config.json, app.controllers.<key>: the same model of pad
// shares one, as does every node of one keyboard or remote.
namespace controllers {

// What a pad's buttons are called, in the hints and on its page (BUTTON
// NAMES): letters with A at the bottom (A B X Y, the south button A) or with
// A on the right (B A Y X), shapes (X O SQ TR), or numbers, an arcade
// stick's. Auto takes what the pad says it is. config.json has them as
// "auto", "a_below", "a_right", "shapes" and "numbers"; the names an older
// version wrote for the same four are still read (labelStyleFromName()).
enum class LabelStyle { Auto, ABelow, ARight, Shapes, Numbers };
LabelStyle labelStyleFromName(const QString &name);
QString labelStyleName(LabelStyle style);
QString labelStyleTitle(LabelStyle style);
// In the order Settings offers them, as a pad's page has them (Button
// Names): [{ style (its name), title }].
QVariantList labelStyles();
// A pad's input, input.cfg's token for it ("a", "dpup", "leftx-",
// "lefttrigger+"), as that style prints it. Auto isn't one: InputManager
// makes it one of the four first.
QString inputLabel(LabelStyle style, const QString &token);

// A pad's key: "pad-" and SDL's GUID for it (lower case) as far as that GUID
// names the model and stays the same from one connection to the next
// (stablePadGuid()), so that the same pad finds its settings again whatever
// it was plugged into, and the same model shares them.
QString padKey(const QString &sdlGuid);
// SDL's GUID of a pad, lower case, without what changes between two
// connections of the same pad. A GUID with a vendor and a product keeps those
// alone: its bus (USB or Bluetooth), the checksum of the name SDL gives it,
// its version and its driver's bytes (XInput's, HIDAPI's, Raw Input's...)
// are zeroed. An XInput pad SDL knows no vendor and product of is named by
// its slot ("XInput Controller #2", as the slot it gets may change), the
// name's checksum in its GUID with it: that checksum is zeroed. Any other
// GUID (a pad SDL knows by its bus and its name alone, the name in the
// GUID) is kept as it is. "" stays "".
QString stablePadGuid(const QString &sdlGuid);
// The key a profile, or a player's choice, saved by an earlier version is
// read under now: a pad's whole SDL GUID made stable (padKey()); any other
// key as it is.
QString currentKey(const QString &key);
// A pad's name without the slot number SDL names an XInput pad by when it
// knows no vendor and product of it ("XInput Controller #2" is "XInput
// Controller"); any other name as it is.
QString padModelName(const QString &name);
// The key a pad that connects takes its settings by: its own (padKey())
// when it has settings or isn't known by its vendor and product; else the
// key of a pad of the same vendor and the same name (padModelName()) that
// has settings, the same pad over another connection (a pad has a product
// number of its own over Bluetooth); else its own. profiles: the settings
// kept, by key.
struct Profile;
QString sharedPadKey(const QString &key, const QString &name, const QHash<QString, Profile> &profiles);
// Whether a controller's key is a pad's (padKey()), not a keyboard's or a
// remote's.
bool isPadKey(const QString &key);
QString evdevKey(int bus, int vendor, int product);

// One of the JAMMA board's sticks, by the name osdos-jamma gives it ("OSD/OS
// JAMMA P1"): an arcade's controls, its buttons in the cabinet's two rows.
bool isJammaStick(const QString &name);
// The key a JAMMA stick had before osdos-jamma gave it a vendor and a
// product: SDL then knew it by its bus, its name's CRC (as its GUID now
// has them, sdlGuid) and the first eleven letters of its name. "" for a GUID
// that isn't SDL's.
QString legacyJammaKey(const QString &sdlGuid, const QString &name);

// What an evdev node is to Settings → Controls, by the keys it has: a
// keyboard, a remote, or nothing to list (a gamepad, which is SDL's; a mouse;
// a power button; a USB sound card's volume/media HID). Linux only; None
// elsewhere.
enum class NodeKind { None, Keyboard, Remote };
NodeKind classifyNode(const QSet<int> &keys);
// Whether a node has a mouse's buttons (left or right) and none of a pad's:
// a mouse, or the mouse an air-mouse remote also is.
bool hasMouseButtons(const QSet<int> &keys);
// A USB sound card's Consumer Control / volume HID by the name the kernel or
// SDL gives it (CM108 and the like). Not a pad or a remote: Settings →
// Sound → Audio Output is where that device belongs.
bool looksLikeUsbAudioHid(const QString &name);

// Settings → Controls → a pad → Game Buttons: which of its inputs is each
// of a cabinet's, Button 1 to 8, Coin and Start (kArcadeInputs, in that
// order), the same in MAME and FinalBurn Neo. An input is input.cfg's token:
// a button ("x", "rightshoulder"), a trigger pressed ("righttrigger+"), or
// the right stick pushed ("rightx-", an N64 pad's C buttons); "" for none.
// The D-pad and the left stick are the joystick, never one of them.
constexpr int kArcadeInputs = 10;
constexpr int kArcadeCoin = 8;
constexpr int kArcadeStart = 9;
// config.json's name of each ("1" to "8", "coin", "start"), and the page's
// ("Button 1", "Coin").
QString arcadeInputName(int index);
QString arcadeInputLabel(int index);
// Every input that can be one of them, in the order the page offers them:
// the buttons as MAME numbers them, Back and Start, then the rest.
QStringList arcadeTokens();
bool isArcadeToken(const QString &token);
// LAYOUT's choices, in its order: "auto", OSD/OS's own by the game's
// buttons (arcadeAutoInputs()), where a pad SDL knows by its kind starts;
// "default", none of OSD/OS's (MAME and FinalBurn Neo each lay a pad's
// buttons out their own way), where any other pad starts; "fighting6", a
// fighter's punches over its kicks where a pad has them (X Y RB over A B
// RT); "neogeo4", a Neo Geo's A B C D on A B X Y, as MAME numbers a pad's
// buttons; a console pad's own: "nes" (B A), "megadrive" (A B C, then X Y
// Z), "snes" (Street Fighter II's: Y X L over B A R), "saturn" (its
// fighters': X Y Z over A B C) and "n64" (Killer Instinct Gold's: B C◄ C▲
// over A C▼ C►); and "custom", set button by button. Their titles, and a
// layout's inputs (kArcadeInputs tokens, "" for none; none at all for
// auto, default and custom). numbered: a stick whose buttons are wired by number,
// a JAMMA board's, whose six-button panel is its pins 1 2 3 over 4 5 6:
// "fighting6" is its buttons in order (arcadeDefaultInputs()), where a
// pad's has its face buttons' rows.
QStringList arcadeLayouts();
QString arcadeLayoutTitle(const QString &layout);
QStringList arcadeLayoutInputs(const QString &layout, bool numbered = false);
// Where a layout set by hand starts from: A, B, X, Y, the shoulders and the
// triggers Button 1 to 8, Back and Start Coin and Start. A JAMMA board's
// stick has these under Default too: its pins' buttons 1 to 8, COIN and
// START, as osdos-jamma gives them.
QStringList arcadeDefaultInputs();
// Auto's inputs for a game: one of up to four buttons on Neo Geo 4's, one
// of five or more (manyButtons) on Fighting 6's. Coin and Start are Back and
// Start in both.
QStringList arcadeAutoInputs(bool manyButtons, bool numbered = false);

// What a pad has, as MAME's game controller support (its sdlgame joystick
// provider) sees it through SDL: its inputs SDL binds (tokens without a
// sign: "a", "lefttrigger", "rightx"), and the triggers it counts among the
// buttons (bound to a button, or a pad whose triggers are buttons: a Switch
// Pro's, Joy-Cons').
struct PadShape {
    QSet<QString> inputs;
    QSet<QString> buttonTriggers;
    // Whether it has a token's input ("lefttrigger+": its left trigger).
    bool has(const QString &token) const;
};
// MAME's name for a pad's input, for its controller file (JOYCODE_<n>_<name>):
// the buttons it numbers, BUTTON1 on, in its order (A, B, X, Y, the
// shoulders, the triggers that are buttons, the sticks' clicks, the paddles,
// Guide, Misc, the touchpad); Back and Start SELECT and START; a trigger that
// is an axis pressed (SLIDER1, SLIDER2, negative as MAME has triggers); the
// right stick's ZAXIS and RZAXIS pushed. "" for an input the pad hasn't.
QString mameInput(const PadShape &shape, const QString &token);
// RetroArch's bind for it, as SDL's game controller has the pad
// (input_playerN_*_btn, _axis): a button's number ("2"), an axis pushed
// ("+5", "-2"); "" for none.
QString retroarchBind(const QString &token);
// A pad's Arcade Buttons (kArcadeInputs tokens) as they act on a pad with
// this shape: a Coin or a Start it hasn't (set while it was away) is its
// own Back or Start, or the other's where the other of the two has its own,
// which a button that had it gives up, so that a game can always be paid
// for and left; the rest as they are.
QStringList arcadeActing(QStringList inputs, const PadShape &shape);

// A controller's own settings. bindings: an input to an action ("up", "down",
// "left", "right", "select", "back", "play_pause", or "none" to take its
// action away). A pad's inputs are input.cfg's tokens; a keyboard's or a
// remote's, InputManager's key ids, as numbers.
struct Profile {
    // As last seen: for config.json's reader, and for Settings → Controls to
    // list it by while it isn't connected. kind: "gamepad", "arcade",
    // "keyboard" or "remote".
    QString name;
    QString kind;
    // A pad's SDL_GameControllerType as last seen (0, unknown, for none):
    // its Auto labels and its Game Buttons' Auto while it is away.
    // config.json has it as "sdl_type".
    int sdlType = 0;
    LabelStyle labels = LabelStyle::Auto;
    QHash<QString, QString> bindings;
    // Its Arcade Buttons: a layout of arcadeLayouts() chosen, "" for none
    // (Auto for a pad SDL knows by its kind, Engine's for any other), and
    // with a preset or Custom its kArcadeInputs tokens, Coin's and Start's
    // never "" (none for "auto" and "default", which a game's own buttons
    // and the engines lay out).
    QString arcadeLayout;
    QStringList arcade;
    // The inputs last set by hand (Custom's), kept while another layout is
    // chosen, so that Custom brings them back: kArcadeInputs tokens, or
    // none. config.json has them as arcade.custom beside a layout, and
    // with none chosen as arcade: { custom }. "default" written is Engine's
    // chosen, which a pad SDL knows by its kind keeps over Auto.
    QStringList arcadeCustom;

    bool isEmpty() const {
        return labels == LabelStyle::Auto && bindings.isEmpty() && arcadeLayout.isEmpty() && arcadeCustom.isEmpty();
    }
    static Profile fromVariant(const QVariant &value);
    QVariantMap toVariant() const;
};

// The menus' actions, which a controller's inputs do: a pad's buttons, a
// key, a click. Each is a key the views take (Up, Down, Left, Right,
// Return, Escape, Space), which InputManager posts for a pad's press, or
// sends to mpv by its name while a video has the screen. One table in
// ControllerProfile.cpp holds what each is called and its keys.
enum class Action { None, Up, Down, Left, Right, Select, Back, PlayPause };

// The actions a controller's page offers, in its order, and their names.
QStringList actionNames();
// The action a name stands for in config.json's bindings or in input.cfg,
// as actionNames() names it ("play_pause" for "playpause" too), "none" for
// none, or "" for a name that is no action. Both are read through this, so
// that what one takes the other does.
QString actionOf(const QString &name);
// Its row's label on a controller's page ("Select / OK"); a name that is
// no action as it is.
QString actionLabel(const QString &action);
// The action a name stands for (actionOf()): None for "none", and for a
// name that is no action, which `ok` tells apart.
Action actionFromName(const QString &name, bool *ok = nullptr);
// Its name ("none" for None), the key the views take for it (Qt::Key, 0 for
// None), mpv's name for that key ("ENTER"), and that key as a keyboard's
// row names it ("Arrow key", "Enter"); "" for None.
QString actionName(Action a);
int actionKey(Action a);
QString actionMpvKey(Action a);
QString actionKeyName(Action a);

// The players (Settings → Controls → Players) for a program OSD/OS starts, a
// script or a game: OSDOS_PLAYER<n>_NAME, _GUID (SDL's) and _DEVICE (its
// device node), for each player with a controller. InputManager keeps them;
// the launchers add them to the program's environment. Six: the JAMMA
// board's two and four more, as many as MAME's six-player games have.
constexpr int kPlayers = 6;
void setPlayerEnvironment(const QList<QPair<QString, QString>> &variables);
void applyPlayersTo(QProcessEnvironment &env);
// Each player's controller's SDL GUID, players 1 to kPlayers in order, "" for
// one without: what the Arcade module tells MAME (its mapdevice lines).
QStringList playerGuids();
// And its name, as SDL has it: with the GUID, the controller RetroArch keeps
// for the player.
QStringList playerNames();
// SDL's mapping of each controller connected, as OSD/OS's own SDL has it (its
// GUID, its name, its buttons and axes), the GUID's CRC zeroed so that an SDL
// of another version takes it too: what the Arcade module gives MAME and
// RetroArch in their environment (SDL_GAMECONTROLLERCONFIG), so that their
// SDL, of another version or on another system, knows every pad as the menus
// do and numbers its buttons as the binds written for it do. InputManager
// keeps them as the pads come and go.
void setPadMappings(const QStringList &mappings);
QStringList padMappings();
// A mapping with its GUID's CRC zeroed (SDL's, of the pad's name, the GUID's
// third and fourth bytes): SDL matches a mapping without one whether or not
// its own GUIDs carry it. Anything that isn't a mapping is left as it is.
QString mappingWithoutCrc(const QString &mapping);
// Each player's Arcade Buttons as the engines take them, kArcadeInputs
// entries each, mameInput()'s and retroarchBind()'s names: empty for a
// player whose pad has no layout of its own (Engine's). On Auto, mame and
// retroarch are a game of up to four buttons' (Neo Geo 4's) and mameMany
// and retroarchMany one of five or more's (Fighting 6's): the Arcade module
// takes the one its game needs (mameFor(), retroarchFor()). Online, and for
// a host's layout, Auto is no layout of the player's own (retroarchOnline()):
// it plays there as Engine's does. InputManager keeps them as it keeps the
// players' environment; the Arcade module writes them for each game.
// kPlayers entries.
// key, name and layout: the player's pad's key and name, and the layout in
// force for it (arcadeLayouts()' name, "default" for Engine's), for the log
// line a game starts with (playersLogLine()).
struct ArcadeBinds {
    QStringList mame;
    QStringList retroarch;
    QStringList mameMany;
    QStringList retroarchMany;
    bool automatic = false;
    QString key;
    QString name;
    QString layout;
    bool isEmpty() const { return mame.isEmpty(); }
    QStringList mameFor(bool manyButtons) const { return automatic && manyButtons ? mameMany : mame; }
    QStringList retroarchFor(bool manyButtons) const { return automatic && manyButtons ? retroarchMany : retroarch; }
    QStringList retroarchOnline() const { return automatic ? QStringList() : retroarch; }
};
void setPlayerArcade(const QList<ArcadeBinds> &players);
QList<ArcadeBinds> playerArcade();
// The line the Arcade module logs as a game starts, so that a saved log says
// what each player played with: for each player with a pad, its name and key,
// the layout in force for it (Auto with the row the game takes, manyButtons),
// the binds written for it (applied, the engine's names for Button 1 to 8,
// Coin and Start; none for Engine's, or Auto online) and the device the
// engine keeps for that player (devices, "" for none said); engine and
// driver name the emulator and the joypad driver it is told to use.
QString playersLogLine(const QString &engine, const QString &driver, bool manyButtons,
                       const QList<QStringList> &applied, const QStringList &devices = {});

// Which keyboard or remote a key came from. Qt doesn't say; the nodes, read
// beside it, do: each press noted as it comes, then taken by the key event
// Qt makes of it (nativeScanCode, the evdev code, with or without X11's 8).
class KeySources {
public:
    void pressed(const QString &device, int code, qint64 ms);
    // The device of a press Qt reports, "" when no node had it; a repeat or
    // a release goes to the device that pressed it.
    QString takePress(quint32 nativeScanCode, qint64 ms);
    QString heldBy(quint32 nativeScanCode) const;
    void released(quint32 nativeScanCode);
    void forget(const QString &device);

private:
    struct Press { QString device; int code; qint64 ms; };
    QList<Press> m_pending;
    QHash<int, QString> m_held;   // evdev code → device
};

} // namespace controllers
