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

// What a pad's buttons are called, in the hints and on its page. Auto takes
// what the pad says it is; an arcade stick's buttons are numbered.
enum class LabelStyle { Auto, Xbox, PlayStation, Nintendo, Arcade };
LabelStyle labelStyleFromName(const QString &name);
QString labelStyleName(LabelStyle style);
QString labelStyleTitle(LabelStyle style);
// In the order Settings offers them.
QStringList labelStyleNames();
// A pad's input, input.cfg's token for it ("a", "dpup", "leftx-",
// "lefttrigger+"), as that style prints it. Auto isn't one: InputManager
// makes it Xbox, PlayStation, Nintendo or Arcade first.
QString inputLabel(LabelStyle style, const QString &token);

QString padKey(const QString &sdlGuid);
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
// a power button). Linux only; None elsewhere.
enum class NodeKind { None, Keyboard, Remote };
NodeKind classifyNode(const QSet<int> &keys);

// Settings → Controls → a pad → Arcade Buttons: which of its inputs is each
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
// LAYOUT's choices, in its order: "default", none of OSD/OS's (MAME and
// FinalBurn Neo each lay a pad's buttons out their own way); "fighting6", a
// fighter's punches over its kicks where a pad has them (X Y RB over A B
// RT); "neogeo4", a Neo Geo's A B C D on A B X Y, as MAME numbers a pad's
// buttons; and "custom", set button by button. Their titles, and a layout's
// inputs (kArcadeInputs tokens; none for default and custom).
QStringList arcadeLayouts();
QString arcadeLayoutTitle(const QString &layout);
QStringList arcadeLayoutInputs(const QString &layout);
// Where a layout set by hand starts from: A, B, X, Y, the shoulders and the
// triggers Button 1 to 8, Back and Start Coin and Start. A JAMMA board's
// stick has these under Default too: its pins' buttons 1 to 8, COIN and
// START, as osdos-jamma gives them.
QStringList arcadeDefaultInputs();

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
    LabelStyle labels = LabelStyle::Auto;
    QHash<QString, QString> bindings;
    // Its Arcade Buttons: a layout of arcadeLayouts() but "default" ("" for
    // that), and with one its kArcadeInputs tokens, Coin's and Start's never
    // "".
    QString arcadeLayout;
    QStringList arcade;

    bool isEmpty() const { return labels == LabelStyle::Auto && bindings.isEmpty() && arcadeLayout.isEmpty(); }
    static Profile fromVariant(const QVariant &value);
    QVariantMap toVariant() const;
};

// The actions a controller's page offers, in its order, and their names.
QStringList actionNames();
QString actionLabel(const QString &action);

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
// player whose pad has no layout of its own. InputManager keeps them as it
// keeps the players' environment; the Arcade module writes them for each
// game. kPlayers entries.
struct ArcadeBinds {
    QStringList mame;
    QStringList retroarch;
    bool isEmpty() const { return mame.isEmpty(); }
};
void setPlayerArcade(const QList<ArcadeBinds> &players);
QList<ArcadeBinds> playerArcade();

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
