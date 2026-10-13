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

// What an evdev node is to Settings → Controls, by the keys it has: a
// keyboard, a remote, or nothing to list (a gamepad, which is SDL's; a mouse;
// a power button). Linux only; None elsewhere.
enum class NodeKind { None, Keyboard, Remote };
NodeKind classifyNode(const QSet<int> &keys);

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

    bool isEmpty() const { return labels == LabelStyle::Auto && bindings.isEmpty(); }
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
