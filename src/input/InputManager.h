#pragma once
#include <QObject>
#include <QEvent>
#include <QTimer>
#include <QHash>
#include <QPair>
#include <QSet>
#include <QDateTime>
#include <QVariantMap>
#include <QFileSystemWatcher>
#include <QElapsedTimer>
#include <SDL.h>

#include "ControllerProfile.h"

class QQuickWindow;
class QKeyEvent;
class QSocketNotifier;
class AppCore;
class JammaPins;

// Centralized gamepad input. SDL controller buttons/axes are mapped to a small
// set of named actions (up/down/left/right/select/back/play_pause), and each
// action is delivered to QML as an ordinary synthesized key event posted to the
// root window — so every existing Keys.onPressed handler (including the Player
// views that forward keys to mpv over IPC) works without gamepad-specific code.
// Defaults can be overridden per-input in $DATA_ROOT/input.cfg (live-reloaded).
//
// The same Action/synthesized-key machinery also backs keyboard/remote-button
// remapping (Settings > Remote Controls): an extra physical key can be bound to
// one of the six actions via config.json's "remote_keymap.<action>" settings,
// on top of (never replacing) that action's default key, see keyRemap handling
// in eventFilter().
//
// Settings → Controls also sets each controller apart (ControllerProfile):
// every pad has its own map, the shared one with its profile's bindings over
// it, and its own button labels; on Linux every keyboard and remote is read
// from its /dev/input nodes beside Qt, which tells whose a key is, for its
// own extra keys. config.json's app.controllers.<key> holds them.
//
// A pad's Back button is MAME's coin (COIN on the JAMMA board's sticks), or
// the input its Arcade Buttons make Coin. Where the item with the focus takes
// a coin (a takesCoin property true on it, or on the nearest item round it
// that has one: the Arcade module's Arcade screen), that input is MAME's coin
// key, 5, instead of its action, so the view handles the key alone, as it
// does from a keyboard.
//
// A pad's Arcade Buttons (Settings → Controls → a pad → Arcade Buttons) are
// which of its inputs is each of a cabinet's, Button 1 to 8, Coin and Start,
// in every game: kept in its profile, and given to the Arcade module for
// each player as MAME and RetroArch name them (controllers::playerArcade()).
class InputManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString lastInputDevice READ lastInputDevice NOTIFY lastInputDeviceChanged)
    Q_PROPERTY(QVariantMap hints READ hints NOTIFY hintsChanged)
    // [{ key, name, kind ("gamepad", "arcade", "keyboard", "remote"), count,
    // present, jamma }]: the controllers connected, pads first, then those
    // with settings of their own that aren't (present false, count 0); jamma
    // the JAMMA board's player (1, 2) for its sticks connected, else 0.
    Q_PROPERTY(QVariantList controllers READ controllers NOTIFY controllersChanged)
    // The key of the controller used last.
    Q_PROPERTY(QString activeController READ activeController NOTIFY activeControllerChanged)
    // Settings → Controls → Players: [{ player (1 to 6), key (the controller
    // chosen, "" for Auto), name (the one playing, or the one chosen while
    // it is away), playing (the key of the one playing, ""), connected }].
    Q_PROPERTY(QVariantList players READ players NOTIFY playersChanged)

public:
    explicit InputManager(const QString &dataRoot, AppCore *appCore, QObject *parent = nullptr);
    ~InputManager() override;

    void setTargetWindow(QQuickWindow *window);
#ifdef Q_OS_LINUX
    // A remote's node of the tests' own: a descriptor written as the kernel
    // writes /dev/input's nodes (input_event), read as they are.
    void adoptNodeForTest(int fd, const QString &name);
#endif
    QString lastInputDevice() const { return m_lastInputDevice; }
    QVariantMap hints() const { return m_hints; }
    QVariantList controllers() const;
    QString activeController() const { return m_activeController; }
    QVariantList players() const;

    // One controller's page: { key, name, kind, present, labels, labelsInUse,
    // jamma, arcade, rows }; labels the style chosen ("auto"...), labelsInUse
    // the one shown (a pad's, by its title); jamma the JAMMA board's player
    // (1, 2) for its sticks, else 0; arcade its Arcade Buttons' layout, by
    // its title ("Default"; "" for a controller that isn't a pad); rows
    // [{ action, label, value }], value what fires the action on it.
    Q_INVOKABLE QVariantMap controllerPage(const QString &key) const;
    // The next input of the controller with this key is reported through
    // controllerInputCaptured() instead of acting; "" ends it, and so do 12
    // seconds (the page's own wait is 10), should the page go without.
    Q_INVOKABLE void captureController(const QString &key);
    // The same for several at once: the JAMMA board's two sticks, tested
    // together.
    Q_INVOKABLE void captureControllers(const QStringList &keys);
    // An input of a controller (as controllerInputCaptured() named it) given
    // an action; "none" takes a pad's input's action away, and a keyboard's
    // or a remote's extra key. Returns { outcome, action, label }, action
    // (and label) what the input does now. outcome: "set"; "already" for an
    // input that does it already, or keeps what it does (keyCapture()); "only"
    // for a pad's only Select / OK or Back, which stays; "other" for a key to
    // take away that isn't one of its extra keys; "cancel" for a keyboard's or
    // a remote's Back, which ends the wait instead.
    Q_INVOKABLE QVariantMap bindControllerInput(const QString &key, const QString &input, const QString &action);
    // What a wait for a press makes of a keyboard's or a remote's key (a Qt
    // key, or a Consumer Control button's or a mouse button's extended id)
    // on the controller with this key, or "" for All Keyboards & Remotes:
    // { outcome, action, label } as bindControllerInput() returns them. A key
    // that does Back ends the wait; one that does Select / OK, or one the
    // menus use (the arrows, Enter, Space), keeps what it does, so that no
    // keyboard loses them. For an action, "none" too on a controller's page.
    Q_INVOKABLE QVariantMap keyCapture(const QString &key, int id, const QString &action) const;
    // A pad's button labels: "auto", "xbox", "playstation", "nintendo", "arcade".
    Q_INVOKABLE void setControllerLabels(const QString &key, const QString &style);
    Q_INVOKABLE void resetController(const QString &key);
    // A pad's Arcade Buttons page: { key, name, present, jamma, layout,
    // layoutTitle, layouts [{ layout, title }], rows [{ input, label, token,
    // value }], choices [{ token, label }] }. layout "default", "fighting6",
    // "neogeo4" or "custom"; a row's input "1" to "8", "coin" or "start", its
    // token the pad's input that is it, as it acts on a pad connected
    // (arcadeInputsOf()) ("" for none; under Default the one a layout of its
    // own starts from), and value that as the pad's labels call it, "None"
    // for none or one a pad connected hasn't. Under Default
    // the rows are the pad's own: a JAMMA stick's pins' buttons; another
    // pad's buttons "Auto", MAME and FinalBurn Neo laying them out their own
    // ways. choices: the inputs it has that can be one (every one for a pad
    // away), in controllers::arcadeTokens()' order.
    Q_INVOKABLE QVariantMap arcadePage(const QString &key) const;
    // One of arcadePage()'s layouts: a preset's inputs, Custom's from those
    // there are (the default ones from Default), Default none.
    Q_INVOKABLE void setArcadeLayout(const QString &key, const QString &layout);
    // One of its Arcade Buttons ("1" to "8", "coin", "start") made an input
    // of the pad (controllerInputCaptured()'s token), "" for none, which
    // Coin and Start never are; the layout is Custom then. Returns
    // { outcome, label, other }: "set"; "swapped" for an input another of
    // them had, which takes this one's in its place, none for one the pad
    // hasn't (other: that one's label, "Button 3"); "kept" for Coin's or
    // Start's when this one has none to give it (other: "Coin"); "already";
    // "joystick" for the D-pad's and the left stick's, the games' joystick;
    // "menu" for Guide, MAME's menu; "unsaved". label: what the input is
    // called.
    Q_INVOKABLE QVariantMap setArcadeInput(const QString &key, const QString &input, const QString &token);
    // All ten at once (Set All Buttons), in kArcadeInputs' order: "set",
    // "unsaved", or "" for a list that isn't ten different ones of the pad's
    // ("" for none), Coin and Start among them.
    Q_INVOKABLE QString setArcadeInputs(const QString &key, const QStringList &tokens);
    // What one of its Arcade Buttons is on a pad, as its labels call it and
    // as it acts on a pad connected (controllers::arcadeActing()): under
    // Default Coin and Start its own Back and Start, a JAMMA stick's buttons
    // its pins'. "" for none, for one a pad connected hasn't, for another
    // pad's buttons under Default (the emulators' to number), and for a
    // controller that isn't a pad.
    Q_INVOKABLE QString arcadeLabel(const QString &key, const QString &input) const;
    // A player's controller: a connected pad's key, or "" for Auto. A pad is
    // one player at a time, unless two of its model are connected.
    Q_INVOKABLE void setPlayer(int player, const QString &key);

    // Human-readable name for a Qt::Key value (e.g. "Return", "O", "F5"), for
    // the remap screen to show what's currently bound. Used from QML.
    Q_INVOKABLE QString keyDisplayName(int qtKey) const;
    // What a pad's own labels call one of its buttons, by input.cfg's token
    // ("back", "start"): "COIN" on a JAMMA stick, "BACK" on an Xbox pad. ""
    // for a controller that isn't a pad.
    Q_INVOKABLE QString buttonLabel(const QString &key, const QString &token) const;
    // While a JAMMA stick's page tests the board's controls: the board's
    // pins as they change (JammaPins), through jammaPinChanged(), the pins
    // the sticks never show among them.
    Q_INVOKABLE void watchJammaPins(bool on);
    // A stand-in for osdos-jamma, for the tests.
    void setJammaProgram(const QString &program);

    // Remap-capture mode, held on by the remap screen while its "press a
    // button" overlay is up. While active, remap delivery is suspended and
    // every real input — keyboard key, Consumer Control button, mouse button —
    // is reported through auxButtonPressed() instead of acting, so the overlay
    // sees the physical input itself even when it's already bound to an action
    // (the eventFilter would otherwise consume it and synthesize that action's
    // default key). Synthesized gamepad key events keep flowing to QML so a
    // pad's Back button can still cancel the overlay.
    Q_INVOKABLE void setRemapCapture(bool active);

signals:
    void lastInputDeviceChanged();
    void hintsChanged();
    // Emitted instead of posting a key event when the Qt window is inactive
    // (fullscreen mpv holds OS focus on macOS, which clears QML active focus).
    // main.cpp connects this to MpvController::sendKey.
    void mpvKeyRequested(const QString &key);
    // Capture-mode reporting (see setRemapCapture): a raw press on any of the
    // three remappable input paths — a real keyboard key (its Qt::Key), a
    // Consumer Control button (kEvdevKeyBase + Linux KEY_* code; Linux only,
    // see scanNodes()), or a mouse button (kMouseButtonBase +
    // Qt::MouseButton). keyDisplayName() and the remote_keymap.* settings
    // accept all three id spaces alike. Only emitted while capture is active.
    void auxButtonPressed(int extendedKeyId);
    void controllersChanged();
    void activeControllerChanged();
    void playersChanged();
    // captureController()'s: the input, as bindControllerInput() takes it, and
    // what it is called.
    void controllerInputCaptured(const QString &key, const QString &input, const QString &label);
    // watchJammaPins()'s: a pin pressed or let go, its player (1, 2; 0 for an
    // expander that is neither's), expander ("0x20") and name ("B1"), with the
    // input osdos-jamma's mapping makes of it ("tl2"); or why none come,
    // which ends the watch.
    void jammaPinChanged(int player, const QString &address, const QString &pin, bool pressed,
                         const QString &input);
    void jammaPinsFailed(const QString &reason);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void pollSdl();
    void onRepeatDelayElapsed();
    void onRepeatTick();
    void onDataDirChanged(const QString &path);
    void onAppSettingChanged(const QString &key, const QString &value);
#ifdef Q_OS_LINUX
    // Defined in the Q_OS_LINUX section of InputManager.cpp; the ifdef keeps
    // moc from emitting a metacall reference to it on other platforms.
    void scanNodes();
#endif

private:
    enum class Action { None, Up, Down, Left, Right, Select, Back, PlayPause };

    void initSdl();
    void openController(int deviceIndex);
    void closeController(SDL_JoystickID instanceId);
    void rebuildMapping();
    void loadDefaultMapping();
    void loadUserMapping();
    void loadKeyRemap();
    void loadProfiles();
    void rebuildPads();
    void rebuildPad(SDL_JoystickID id);
    void adoptLegacyJammaKey(SDL_JoystickID id);
    struct Pad;
    Pad padFor(const QString &key, const QString &name, int sdlType) const;
    Pad padOf(const QString &key, const QString &name) const;
    // What a pad's input is called on it (buttonLabel()).
    QString padInputLabel(const Pad &pad, const QString &token) const;
    static int jammaPlayer(const Pad &pad);
    static bool parsePadInput(const QString &token, int *button, int *axis, int *sign);
    static Action padInputAction(const Pad &pad, const QString &input);
    int padInputsFor(SDL_JoystickID id, Action a) const;
    Action keyActionOn(const QString &key, int id) const;
    Action defaultKeyAction(int id) const;
    static QString actionName(Action a);
    static QVariantMap captureOutcome(const QString &outcome, Action does);
    bool saveProfiles();
    void loadPlayers();
    bool savePlayers();
    void updatePlayers();
    controllers::Profile &editProfile(const QString &key);
    // A pad's Arcade Buttons as they are now, kArcadeInputs tokens: its
    // layout's, or with none the pad's own (arcadeDefaultInputs()); on a pad
    // connected, as they act on it (controllers::arcadeActing()).
    QStringList arcadeInputsOf(const QString &key) const;
    // A pad's controller while it is connected, else null.
    SDL_GameController *padController(const QString &key) const;
    bool saveArcade(const QString &key, const QString &layout, const QStringList &inputs);
    static bool needsInput(int index);
    // What a connected pad has, as MAME sees it (controllers::PadShape).
    static controllers::PadShape padShape(SDL_GameController *gc);
    // Each player's Arcade Buttons, for the games (controllers::setPlayerArcade()).
    void publishArcade();
    void noteController(const QString &key);
    Action profileAction(const QString &key, const QString &input) const;
    bool capturing(const QString &key) const;
    static QString buttonToken(int button);
    static QString axisToken(int axis, int sign);
#ifdef Q_OS_LINUX
    struct Node;
    void watchNodes();
    void addNode(int fd, const QString &path, const QString &name, const QString &key,
                 controllers::NodeKind kind);
    void readNode(Node *node);
    void drainNodes();
    void closeNode(Node *node);
    static int qtKeyForEvdev(int linuxCode);
#endif
    static QString evdevKeyName(int linuxCode);
    static QString mouseButtonName(int qtButton);
    void noteActiveController(SDL_JoystickID which);
    void recomputeSuppressedDevices();
    void handleButton(SDL_JoystickID which, Uint8 button, bool pressed);
    void handleAxis(SDL_JoystickID which, Uint8 axis, Sint16 value);
    void pressAction(Action a);
    void pressFrom(const QString &device, Action a);
    void beginPress(Action a);
    void releaseAction(Action a);
    // An input that can be held down: its controller (a pad's instance,
    // padSource(); a node's path; "mouse") and which input (a button, a pad's
    // axis numbered after its buttons, a key code).
    using HeldInput = QPair<QString, int>;
    static QString padSource(SDL_JoystickID id);
    void releaseHeld(const QString &source);
    void letGoOfPad(SDL_JoystickID id);
    static bool essentialAction(Action a);
    void deliverPress(Action a, bool autoRepeat);
    void deliverRelease(Action a, bool autoRepeat);
    void postKey(int qtKey, QEvent::Type type, bool autoRepeat);
    bool windowActive() const;
    // The item with the focus takes a coin (see the class's comment).
    bool takesCoin() const;
    void setLastInputDevice(const QString &device);
    void updateHints();
    static int qtKeyForAction(Action a);
    static QString mpvKeyForAction(Action a);
    // Maps a HID media-key event to the canonical mpv key name mpv-media-keys.lua
    // binds, or an empty string for non-media keys.
    static QString mpvKeyForMediaEvent(const QKeyEvent *ke);
    static Action actionFromString(const QString &name, bool *ok);
    static int buttonFromToken(const QString &token);
    static bool isDirectional(Action a);

    QQuickWindow *m_window = nullptr;
    AppCore *m_appCore = nullptr;
    QString m_dataRoot;
    bool m_sdlReady = false;

    QTimer m_pollTimer;
    QTimer m_repeatDelayTimer;
    QTimer m_repeatTimer;
    QElapsedTimer m_volumeRepeat;   // since the last volume key sent to mpv
    QFileSystemWatcher m_watcher;
    QDateTime m_cfgLastModified;

    QHash<SDL_JoystickID, SDL_GameController*> m_controllers;
    // The map every pad starts from, the defaults and input.cfg's lines.
    QHash<int, Action> m_buttonMap;                  // SDL_GameControllerButton → Action
    QHash<int, QPair<Action, Action>> m_axisMap;     // SDL_GameControllerAxis → (negative, positive)
    // Each pad's own: that map with its profile's bindings over it.
    struct Pad {
        QString key;
        QString name;
        bool arcade = false;
        QHash<int, Action> buttons;
        QHash<int, QPair<Action, Action>> axes;
        controllers::LabelStyle labels = controllers::LabelStyle::Xbox;   // never Auto
        // Its input that is MAME's coin (input.cfg's token), its Arcade
        // Buttons' Coin as it acts on the pad (controllers::arcadeActing()):
        // Back unless they say.
        QString coin = QStringLiteral("back");
    };
    QHash<SDL_JoystickID, Pad> m_pads;
    QHash<qint64, int> m_axisState;                  // (pad, axis) → engaged direction: -1 / 0 / +1
    QHash<QString, controllers::Profile> m_profiles; // config.json app.controllers.<key>
    // What each controller was last seen as, kept once it goes, for its page
    // while it is away: its name, its kind and a pad's SDL type (its Auto
    // labels).
    struct Seen {
        QString name;
        QString kind;
        int sdlType = 0;   // SDL_CONTROLLER_TYPE_UNKNOWN
    };
    QHash<QString, Seen> m_seenAs;
    QString m_activeController;
    QStringList m_captureControllers;                // captureController()'s keys
    JammaPins *m_jammaPins = nullptr;
    qint64 m_captureEnds = 0;                        // m_clock's time it ends at
    QStringList m_playerKeys;                        // config.json app.players: chosen, "" for Auto
    QStringList m_playerNames;                       // and the names they had
    QList<SDL_JoystickID> m_playerPads;              // who plays, -1 for nobody
    QHash<int, QString> m_labelOverrides;            // SDL button → user display label (input.cfg)
    QHash<int, Action> m_keyRemap;                   // Qt::Key or extended evdev id → Action
    SDL_JoystickID m_lastActiveController = -1;      // labels follow the pad last touched
    Action m_heldDirection = Action::None;

    // Steam Input mirror de-dup: it presents a managed "Steam Virtual Gamepad"
    // plus the raw controllers it mirrors (the Deck's built-in pad, etc.); the
    // raw device echoes each press ~0.5s later → double/late input. The virtual
    // pad is the low-latency one (and what Gaming Mode uses), so when it's
    // present we suppress the rest. Recomputed on connect/disconnect.
    QSet<SDL_JoystickID> m_suppressedDevices;

    // Actions currently held down — idempotent press/release hygiene (a device
    // can't re-fire a press it's already holding).
    QSet<Action> m_heldActions;
    // What each input held down began. Letting go ends that, whatever the
    // input does by then (a page has bound it anew, or waits for a press of
    // its controller's), and a controller that goes lets go of what it held.
    QHash<HeldInput, Action> m_heldInputs;
    // Coin inputs (Back, or the Arcade Buttons' Coin) held down as the coin
    // key: their release is its release.
    QSet<HeldInput> m_coinInputs;
    // A remote's buttons held down that went to the views as keys of their
    // own (readNode()): their release is the key's.
    QHash<HeldInput, int> m_postedKeys;
    // Keys whose press a wait for a press took (Qt::Key): swallowed until let
    // go, as their repeats would act as what they were just given.
    QSet<int> m_capturedKeys;
    bool m_remapCapture = false;                     // see setRemapCapture()
    int  m_wheelTurn = 0;                            // the mouse wheel's turn short of a notch (eventFilter())

#ifdef Q_OS_LINUX
    // A keyboard's or a remote's /dev/input node. A Consumer Control node's
    // buttons are acted on from here when they do something (keyActionOn()),
    // reported to a wait, or posted to the views as the keys they are where
    // the views act on them (qtKeyForEvdev()), and Qt's copy of such a press,
    // where Qt has one, is dropped (m_consumerTaken); any other node's keys
    // reach Qt, and their nodes only say whose they are (m_keySources).
    struct Node {
        int fd = -1;
        QSocketNotifier *notifier = nullptr;
        QString path;
        QString key;
        QString name;
        controllers::NodeKind kind = controllers::NodeKind::None;
        bool consumer = false;
    };
    QList<Node *> m_nodes;
    QFileSystemWatcher m_nodeWatcher;
    QTimer m_nodeScanTimer;
    int m_nodeRetries = 0;
    controllers::KeySources m_keySources;
    controllers::KeySources m_consumerTaken;         // Consumer Control presses readNode() took
#endif
    QElapsedTimer m_clock;

    QString m_lastInputDevice = "keyboard";
    QVariantMap m_hints;
};
