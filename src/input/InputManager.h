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

#include <functional>
#include <optional>

#include "ControllerProfile.h"

class QQuickWindow;
class QKeyEvent;
class QWheelEvent;
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
// remapping (Settings → Controls → Keyboards & Remotes): extra physical keys
// can be bound to an action via config.json's "remote_keymap.<action>" settings,
// on top of (never replacing) that action's default key, see m_keyRemap's
// handling in eventFilter().
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
// A pad's Arcade Buttons (Settings → Controls → a pad → Game Buttons) are
// which of its inputs is each of a cabinet's, Button 1 to 8, Coin and Start,
// in every game: kept in its profile, and given to the Arcade module for
// each player as MAME and RetroArch name them (controllers::playerArcade()).
// A pad SDL knows by its kind starts on Auto, OSD/OS's own by the game's
// buttons (arcadeLayoutOf()); any other on Engine's, the emulators' own.
//
// Cabinet Lock's unlock buttons (Settings → Startup → Cabinet Lock) are one to four
// inputs, of any pads and keyboards, held together: with the lock on
// (config.json app.cabinet_lock), held for three seconds they are
// unlockRequested(), and Main.qml opens the main menu. A press of one of them
// that isn't a direction waits a moment for the rest before it acts, so that
// a START among them doesn't start a game first; once they are all held,
// none of them acts. One alone acts as it is let go, short of those three
// seconds.
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
    // Cabinet Lock's unlock buttons by name, joined ("P1 START + P2 START"),
    // "" for none.
    Q_PROPERTY(QString unlockButtons READ unlockButtons NOTIFY unlockButtonsChanged)
    // How long a wait for new unlock buttons (captureUnlockButtons()) lasts
    // with nothing held, for the page to count down.
    Q_PROPERTY(int unlockCaptureSeconds READ unlockCaptureSeconds CONSTANT)

public:
    explicit InputManager(const QString &dataRoot, AppCore *appCore, QObject *parent = nullptr);
    ~InputManager() override;

    void setTargetWindow(QQuickWindow *window);
#ifdef Q_OS_LINUX
    // A remote's node of the tests' own: a descriptor written as the kernel
    // writes /dev/input's nodes (input_event), read as they are.
    void adoptNodeForTest(int fd, const QString &name, bool mouse = false);
#endif
    // Every pad taken for one SDL hears only through its reports, as its
    // HIDAPI driver's (the tests' virtual pads say what is held as they open).
    void padsReportLateForTest() { m_padsReportLate = true; }
    QString lastInputDevice() const { return m_lastInputDevice; }
    QVariantMap hints() const { return m_hints; }
    QVariantList controllers() const;
    QString activeController() const { return m_activeController; }
    QVariantList players() const;
    QString unlockButtons() const { return m_unlockNames.join(QStringLiteral(" + ")); }
    static constexpr int kUnlockCaptureSeconds = 10;
    static int unlockCaptureSeconds() { return kUnlockCaptureSeconds; }

    // The key that stands for every keyboard and remote at once (All
    // Keyboards & Remotes): its page's extra keys are config.json's
    // app.remote_keymap, and a wait for a press of it takes every real key,
    // Consumer Control button and mouse button.
    Q_PROPERTY(QString allKeys READ allKeys CONSTANT)
public:
    static QString allKeys();

    // How often the pads are polled now (0 while they are let go of for a
    // game): at a pad's rate with one connected, else only often enough to
    // notice one plugged in (pollInterval()).
    int pollIntervalMs() const { return m_pollTimer.isActive() ? m_pollTimer.interval() : 0; }

    // One controller's page: { key, name, kind, present, count, labels,
    // labelsInUse, labelStyles, jamma, arcade, players, player, rows }; labels
    // the style chosen ("auto"...), labelsInUse the one shown (a pad's, by its
    // title), labelStyles a pad's choices (controllers::labelStyles());
    // jamma the JAMMA board's player (1, 2) for its sticks, else 0; arcade
    // its Game Buttons' layout, by its title ("" for a controller that isn't
    // a pad); players the numbers a pad plays as now, player the one chosen
    // for it by hand (0 for Auto); rows [{ action, label, value, first,
    // inputs [{ input, label }] }]: value what does the action on it, first
    // the first of those, inputs the ones that can be taken away (a pad's
    // buttons, those a pad connected has; a keyboard's or a remote's extra
    // keys). A keyboard's value names the action's own key, then every
    // keyboard's extra keys ("(all)"), then its own: "ENTER + MENU (ALL) + F5".
    // kind "keys" is allKeys' page.
    Q_INVOKABLE QVariantMap controllerPage(const QString &key) const;
    // A wait for a press of the controller with this key: its inputs act on
    // nothing meanwhile. A press comes as controllerInputDown() at once, as
    // controllerInputCaptured() when it is let go (a tap: the answer), and
    // held for a second and a half as captureHeld(), which is the way out
    // of every wait: the one who holds answers nothing. "" ends the wait.
    // seconds is how long the page itself waits (its PressPrompt's count:
    // ten, twenty for a step of Set One by One, thirty on the JAMMA board's
    // test); the wait ends by itself a moment after that, should the page go
    // without, and never before the page's own does.
    static constexpr int kCaptureSeconds = 10;
    Q_INVOKABLE void captureController(const QString &key, int seconds = kCaptureSeconds);
    // The same for several at once: the JAMMA board's two sticks, tested
    // together.
    Q_INVOKABLE void captureControllers(const QStringList &keys, int seconds = kCaptureSeconds);
    // An input of a controller (as controllerInputCaptured() named it) given
    // an action beside the ones that do it already (Add another); "none"
    // takes a pad's input's action away, and a keyboard's or a remote's
    // extra key. Returns { outcome, action, label }, action (and label) what
    // the input does now. outcome: "set"; "already" for an input that does
    // it already, or keeps what it does (keyCapture()); "only" for the only
    // Select / OK, Back, Up or Down a controller has, which stays (label:
    // that action's); "other" for a key to take away that isn't one of its
    // extra keys; "cancel" for a keyboard's or a remote's Back, which ends
    // the wait instead; "unsaved".
    Q_INVOKABLE QVariantMap bindControllerInput(const QString &key, const QString &input, const QString &action);
    // The same input made the only one that does the action (Replace): the
    // ones that did it do nothing from now on (cleared: their names). Where
    // that would leave the controller without the Select / OK, Back, Up or
    // Down the input was, the first of them takes it in its place
    // ("swapped"; other: its name, otherAction: what it does now), so no
    // order of presses is refused; "only" is left for an action that had no
    // input to give. A keyboard's or a remote's new extra key takes the
    // place of the action's other extra keys, at once.
    Q_INVOKABLE QVariantMap replaceControllerInput(const QString &key, const QString &input, const QString &action);
    // One of controllerPage()'s inputs taken away (Remove one), by its name
    // there: "set", "only" for the last Select / OK, Back, Up or Down of a
    // pad, or for a remote's extra key that is the one the menus were last
    // worked with, "other", "unsaved".
    Q_INVOKABLE QVariantMap removeControllerInput(const QString &key, const QString &input);
    // What a wait for a press makes of a keyboard's or a remote's key (a Qt
    // key, or a Consumer Control button's or a mouse button's extended id)
    // on the controller with this key (allKeys, or "", for All Keyboards &
    // Remotes): { outcome, action, label } as bindControllerInput() returns
    // them. A key that does Back ends the wait; one the menus use (the
    // arrows, Enter, Space) keeps what it does; an extra key that is the
    // Select / OK, Up or Down the menus were last worked with stays ("only"),
    // on a remote: a keyboard has the menus' own keys beside it. For an
    // action, "none" too on a controller's page.
    Q_INVOKABLE QVariantMap keyCapture(const QString &key, int id, const QString &action) const;
    // What an input of a controller is, for its test: { action, game }: the
    // menus' action it does ("Select / OK", "" for none), and for a pad what
    // it is in the games: one of its Game Buttons ("Button 4", "Coin"),
    // "Joystick" (the D-pad and the left stick), "MAME's menu" (Guide),
    // "Engine's" for a button the emulators number themselves (Default),
    // "" for none.
    Q_INVOKABLE QVariantMap inputRoles(const QString &key, const QString &input) const;
    // A pad's button names: "auto", "a_below", "a_right", "shapes", "numbers".
    // False, as from every setter here, when the settings could not be
    // written: nothing has changed then, and the page says so.
    Q_INVOKABLE bool setControllerLabels(const QString &key, const QString &style);
    // A controller's own settings forgotten; allKeys: every keyboard's and
    // remote's extra keys.
    Q_INVOKABLE bool resetController(const QString &key);
    // A pad's Game Buttons page: { key, name, present, jamma, layout,
    // layoutTitle, layouts [{ layout, title }], rows [{ input, label, token,
    // value }], manyRows, choices [{ token, label }] }. layout one of
    // controllers::arcadeLayouts(); a row's input "1" to "8", "coin" or
    // "start", its token the pad's input that is it, as it acts on a pad
    // connected (arcadeInputsOf()) ("" for none; under Default the one a
    // layout of its own starts from), and value that as the pad's labels
    // call it, "None" for none or one a pad connected hasn't. Under Default
    // the rows are the pad's own: a JAMMA stick's pins' buttons; another
    // pad's buttons "Engine's", MAME and FinalBurn Neo laying them out their
    // own ways. Under Auto (arcadeLayoutOf()) the rows are a game of up to
    // four buttons', and manyRows a game of five or more's (Fighting 6's),
    // which the page says in words; [] under any other layout. choices: the
    // inputs it has that can be one (every one for a pad away), in
    // controllers::arcadeTokens()' order.
    Q_INVOKABLE QVariantMap arcadePage(const QString &key) const;
    // One of arcadePage()'s layouts: a preset's inputs, Custom's from those
    // there are (the default ones from Default, the four-button row from
    // Auto), Default and Auto none: each is kept as chosen. False when it
    // could not be saved.
    Q_INVOKABLE bool setArcadeLayout(const QString &key, const QString &layout);
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
    // All ten at once (Set One by One), in kArcadeInputs' order: "set",
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
    // one player at a time, unless two of its model are connected. False
    // when nothing changed: no such player, a pad that isn't there, or the
    // settings could not be written.
    Q_INVOKABLE bool setPlayer(int player, const QString &key);
    // The same from the pad's side (its page's PLAYER): the number it plays
    // as, 0 for Auto. A number another pad was chosen for swaps the two
    // (that one takes this pad's, or goes back to Auto if this one had
    // none); one a pad played as by Auto leaves that pad to Auto. Returns
    // { outcome ("set", "already", "away" for a pad not connected, "unsaved"
    // when the settings could not be written, no pad having moved), other,
    // otherPlayer }: the pad that had the number, by name, and the number it
    // plays as now (0 for none); "" where no pad moved.
    Q_INVOKABLE QVariantMap setPadPlayer(const QString &key, int player);
    // Every player back to Auto; false when that could not be saved.
    Q_INVOKABLE bool resetPlayers();

    // Human-readable name for a Qt::Key value (e.g. "Return", "O", "F5"), for
    // the remap screen to show what's currently bound. Used from QML.
    Q_INVOKABLE QString keyDisplayName(int qtKey) const;
    // What a pad's own labels call one of its buttons, by input.cfg's token
    // ("back", "start"): "COIN" on a JAMMA stick, "BACK" on most pads. ""
    // for a controller that isn't a pad.
    Q_INVOKABLE QString buttonLabel(const QString &key, const QString &token) const;
    // While a JAMMA stick's page tests the board's controls: the board's
    // pins as they change (JammaPins), through jammaPinChanged(), the pins
    // the sticks never show among them.
    Q_INVOKABLE void watchJammaPins(bool on);
    // A stand-in for osdos-jamma, for the tests.
    void setJammaProgram(const QString &program);
    // Cabinet Lock's unlock buttons chosen anew (true), or the wait for them
    // ended (false). While it waits, every pad's input and every key is
    // reported (unlockButtonsHeld()) instead of acting; those held together
    // for two seconds, one to four, become the unlock buttons, kept in
    // config.json's app.unlock_buttons, and end it (unlockCaptureEnded()),
    // as ten seconds with nothing held do. Held so and not saved (the
    // settings could not be written), the old ones stay and the signal
    // says unsaved.
    Q_INVOKABLE void captureUnlockButtons(bool on);


    // Close every open SDL game controller so a child that takes the screen
    // (RetroArch, MAME) can open the same pads with its own SDL. Player env
    // and pad mappings are left as they were for ScriptLauncher / Arcade.
    // main.cpp calls this when an arcade game holds DisplayHandoff
    // (ArcadeBackend::kHandoffOwner); every other hand-off keeps the pads.
    void releaseJoysticksForHandoff();
    // Open the pads again once the display is back (deferred: see
    // finishReclaimJoysticks()).
    void reclaimJoysticksAfterHandoff();
    // Who says whether back is the player's now (MpvController::takesBack(),
    // main.cpp): asked at each back. While it is, back goes to the player at
    // once (mpvKeyRequested), from whatever it comes (a key, a pad's or a
    // remote's button, a click), and not by way of the view that has the
    // keys: a video has the screen then, and back is the one key that leaves
    // it.
    void setBackTaker(std::function<bool()> takes);

signals:
    void lastInputDeviceChanged();
    void hintsChanged();
    // Emitted instead of posting a key event when the Qt window is inactive
    // (fullscreen mpv holds OS focus on macOS, which clears QML active focus),
    // and for the media keys, which always drive mpv: a press, or a repeat of
    // a key held (autoRepeat). main.cpp connects this to
    // MpvController::sendKey, and mpvKeysReleased(), which says such a key
    // was let go, to MpvController::releaseKeys.
    void mpvKeyRequested(const QString &key, bool autoRepeat);
    void mpvKeysReleased();
    void controllersChanged();
    void activeControllerChanged();
    void playersChanged();
    // captureController()'s: the input, as bindControllerInput() takes it, and
    // what it is called: pressed (Down), let go before the hold time (a tap,
    // Captured), or held for it (captureHeld(), which says only whose wait
    // it stops). A pad's input is input.cfg's token; a keyboard's or a
    // remote's key, and with allKeys every real key (its Qt::Key), Consumer
    // Control button (kEvdevKeyBase + its Linux code) and mouse button
    // (kMouseButtonBase + its Qt::MouseButton), is its id as a number, which
    // keyDisplayName() and the extra keys' settings take alike. With allKeys,
    // synthesized gamepad keys keep flowing to QML, so a pad's Back still
    // cancels the wait.
    void controllerInputDown(const QString &key, const QString &input, const QString &label);
    void controllerInputCaptured(const QString &key, const QString &input, const QString &label);
    void captureHeld(const QString &key);
    // watchJammaPins()'s: a pin pressed or let go, its player (1, 2; 0 for an
    // expander that is neither's), expander ("0x20") and name ("B1"), with the
    // input osdos-jamma's mapping makes of it ("tl2"); or why none come,
    // which ends the watch.
    void jammaPinChanged(int player, const QString &address, const QString &pin, bool pressed,
                         const QString &input);
    void jammaPinsFailed(const QString &reason);
    // Cabinet Lock on: its unlock buttons held together for three seconds.
    void unlockRequested();
    void unlockButtonsChanged();
    // captureUnlockButtons()'s: what is held now, by name ("" for nothing),
    // and whether that is more than four. And its end: saved, the buttons
    // held are the unlock buttons now; unsaved, they were held long enough
    // but the settings could not be written; neither, the wait was left.
    void unlockButtonsHeld(const QString &names, bool tooMany);
    void unlockCaptureEnded(bool saved, bool unsaved);

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
    using Action = controllers::Action;

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
    void logPadAdded(SDL_JoystickID id, const QString &guid, const QString &ownKey) const;
    struct Pad;
    Pad padFor(const QString &key, const QString &name, int sdlType) const;
    Pad padOf(const QString &key, const QString &name) const;
    // What a pad's input is called on it (buttonLabel()).
    QString padInputLabel(const Pad &pad, const QString &token) const;
    static int jammaPlayer(const Pad &pad);
    static bool parsePadInput(const QString &token, int *button, int *axis, int *sign);
    static Action padInputAction(const Pad &pad, const QString &input);
    int padInputsFor(SDL_JoystickID id, Action a) const;
    // A pad's inputs that do an action, input.cfg's tokens, buttons first:
    // with its controller (one connected), only those it has.
    static QStringList padTokensFor(const Pad &pad, SDL_GameController *gc, Action a);
    // The pad connected with this key, m_pads.cend() for one away. A pad
    // Steam's virtual pad stands in for (m_suppressedDevices) is away: its
    // inputs are no longer listened to, and it isn't listed, played or
    // shown on its page as connected.
    using LivePad = QHash<SDL_JoystickID, Pad>::const_iterator;
    LivePad livePad(const QString &key) const;
    // The key of a pad connected by its key, -1 for one away (livePad()).
    SDL_JoystickID padIdOf(const QString &key) const;
    // An extra key (keyboard, remote, or allKeys' shared ones) given an
    // action, or with None taken away; false if it couldn't be saved.
    bool setExtraKey(const QString &key, int id, Action a);
    bool saveKeyRemap();
    // The extra keys that do an action on a keyboard's or a remote's page
    // (allKeys: the shared ones), by name.
    QList<int> extraKeysFor(const QString &key, Action a) const;
    // Whether an extra key that does an action can stop doing it: the action
    // isn't one the menus need, or the controller is a keyboard (it has the
    // menus' own keys), or the menus were last worked with something else.
    bool canLoseKey(const QString &key, int id, Action a) const;
    Action keyActionOn(const QString &key, int id) const;
    Action defaultKeyAction(int id) const;
    static QVariantMap captureOutcome(const QString &outcome, Action does);
    bool saveProfiles();
    void loadPlayers();
    bool savePlayers();
    bool choosePlayer(int player, const QString &key);
    void updatePlayers();
    controllers::Profile &editProfile(const QString &key);
    // A pad's Arcade Buttons as they are now, kArcadeInputs tokens: its
    // layout's, under Auto a game of up to four buttons' or of five or more's
    // (manyButtons), or under Default the pad's own (arcadeDefaultInputs());
    // on a pad connected, as they act on it (controllers::arcadeActing()).
    QStringList arcadeInputsOf(const QString &key, bool manyButtons = false) const;
    // The layout a pad's Game Buttons are on: the one chosen for it, or with
    // none chosen "auto" for a pad SDL knows by its kind (knownPadType()) that
    // isn't a JAMMA board's stick, "default" (Engine's) for any other. By
    // what the pad was last seen as (m_seenAs), so also while it is away.
    QString arcadeLayoutOf(const QString &key) const;
    // arcadeInputsOf()'s inputs before they act on the pad.
    QStringList arcadeRowOf(const QString &key, bool manyButtons = false) const;
    // Whether SDL knows a pad by its kind (an SDL_GameControllerType that is
    // neither unknown nor virtual).
    static bool knownPadType(int sdlType);
    // A pad's controller while it is connected (livePad()), else null.
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
    // A wait's presses (captureController()): one pressed, reported as down
    // and kept until it is let go (a tap, reported) or the oldest of them
    // has been held long enough (the wait's way out).
    void capturePressed(const QPair<QString, int> &held, const QString &key, const QString &input,
                        const QString &label);
    bool captureReleased(const QPair<QString, int> &held);
    void armCaptureStop();
    void captureHeldOut();
    static QString buttonToken(int button);
    static QString axisToken(int axis, int sign);
#ifdef Q_OS_LINUX
    struct Node;
    void watchNodes();
    void addNode(int fd, const QString &path, const QString &name, const QString &key,
                 controllers::NodeKind kind, bool mouse = false);
    void readNode(Node *node);
    void drainNodes();
    void closeNode(Node *node);
    static int qtKeyForEvdev(int linuxCode);
    static QString volumeKeyForEvdev(int linuxCode);
    void nodeClick(Qt::MouseButton button, bool press);
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
    // A pad's input (input.cfg's token) pressed or let go, a button's or an
    // axis's alike (handleButton(), handleAxis()).
    void pressPadInput(SDL_JoystickID which, const HeldInput &input, const QString &token, Action a);
    void releasePadInput(SDL_JoystickID which, const HeldInput &input, const QString &token);
    // What a pad's press does (pressPadInput()): MAME's coin key where the
    // focus takes one, else its action. Nothing for a pad gone or no longer
    // listened to since, as for a press held back.
    void actOnPadPress(SDL_JoystickID which, const HeldInput &input, const QString &token, Action a);
    // The coin key let go, for an input that pressed it: false for any other.
    bool releaseCoin(const HeldInput &input);

    // Cabinet Lock (captureUnlockButtons()). An input as the unlock buttons
    // name it: a pad's by the player it plays ("p1/start"), else by its key
    // ("pad-…/start"); a key by its id ("key/16777220"). Its name as the
    // page shows it: "P1 START", a pad of no player's by its name.
    void loadUnlock();
    bool watchingUnlock() const { return m_lockOn && !m_unlockInputs.isEmpty(); }
    QString padInputId(SDL_JoystickID which, const QString &token) const;
    QString padInputName(SDL_JoystickID which, const QString &token) const;
    // A press, while the unlock buttons are waited for, reported (true); else,
    // with the lock on, one of them held back until the rest come or a moment
    // has gone by (true), act then, but a direction, which acts at once
    // unless one is held back already; all of them held, kept from the views
    // (true). The only one there is has no rest to wait for: it is held
    // back until it is let go, or held long enough. False for a press to
    // act on now.
    bool unlockPress(const QString &id, const QString &name, const HeldInput &input, bool direction,
                     const std::function<void()> &act);
    // A release: reported while they are waited for (true); else, all of
    // them held long enough though the timer couldn't run out, the unlock
    // first (unlockHeldOut()); what was held back acted on, if this input's
    // press was; and false.
    bool unlockRelease(const QString &id);
    bool heldBack(const QString &id) const;
    void flushHeldBack();
    void unlockAllHeld();
    void quietUnlockHeld();
    void unlockHeldOut();
    void unlockCaptureChanged();
    void endUnlockCapture(bool saved, bool unsaved = false);
    // A key's part in it (eventFilter()): true to keep the key from the
    // views, nullopt to let it go on. A key that must come after one held
    // back goes on as a replay of it, posted after that one's.
    std::optional<bool> unlockKey(const QKeyEvent *ke, int id, const QString &device);
    void replayKey(const QKeyEvent &ke, const QString &device);
    void deliverPress(Action a, bool autoRepeat);
    bool clickActs(Qt::MouseButton pressed, bool isPress);
    void deliverRelease(Action a, bool autoRepeat);
    // Whether this back is the player's (setBackTaker()), said in the log
    // with what it came from.
    bool backIsThePlayers(const QString &from);
    // A key that went on to the views while a video has the screen, and who
    // has the keys there: said in the log once for each.
    void noteKeyToViews(const QString &name);
    // eventFilter() in parts: the wheel's notches, and a key's stages, in
    // the order eventFilter() lists them. A key as each stage takes it: the
    // event, whether it came in at its window (rather than at an item it
    // goes on to), whether the app made it (a pad's press, postKey()) or
    // replayed it (replayKey()), its id (keyId()), the keyboard or remote
    // it came from, "" where none is known, and whether that is a Consumer
    // Control node whose press readNode() took (keyIn() works these out).
    // Each stage's answer: true to keep the key from the views, false to
    // let it go on, nullopt to leave it to the next.
    bool wheelTurned(QObject *obj, const QWheelEvent *event);
    struct KeyIn {
        const QKeyEvent *ke = nullptr;
        QEvent::Type type = QEvent::None;
        bool atWindow = false;
        bool synthetic = false;
        bool replay = false;
        int id = 0;
        QString device;
        bool takenByNode = false;
    };
    KeyIn keyIn(QObject *obj, const QKeyEvent *ke);
    std::optional<bool> keyTakenByNode(const KeyIn &key);
    std::optional<bool> keyFromKeyboard(const KeyIn &key);
    std::optional<bool> oneShotKeyHeld(const KeyIn &key);
    std::optional<bool> rightShiftKey(const KeyIn &key);
    std::optional<bool> unlockButtonKey(const KeyIn &key);
    std::optional<bool> keyTakenByWait(const KeyIn &key);
    std::optional<bool> keyForWait(const KeyIn &key);
    std::optional<bool> extraKey(const KeyIn &key);
    std::optional<bool> backKeyForPlayer(const KeyIn &key);
    std::optional<bool> unnamedKey(const KeyIn &key);
    std::optional<bool> mediaKey(const KeyIn &key);
    std::optional<bool> keyToViews(const KeyIn &key);
    void postKey(int qtKey, QEvent::Type type, bool autoRepeat);
    bool windowActive() const;
    // The item with the focus takes a coin (see the class's comment).
    bool takesCoin() const;
    void setLastInputDevice(const QString &device);
    void updateHints();
    // Maps a HID media-key event to the canonical mpv key name mpv-media-keys.lua
    // binds, or an empty string for non-media keys.
    static QString mpvKeyForMediaEvent(const QKeyEvent *ke);
    static int buttonFromToken(const QString &token);
    static bool isDirectional(Action a);

    QQuickWindow *m_window = nullptr;
    AppCore *m_appCore = nullptr;
    QString m_dataRoot;
    bool m_sdlReady = false;
    // True between releaseJoysticksForHandoff() and finishReclaimJoysticks().
    bool m_joysticksReleasedForHandoff = false;
    // Bumped on each release so a deferred reclaim from an earlier hand-off
    // is dropped if another game has already taken the pads again.
    quint64 m_joystickHandoffEpoch = 0;
    void finishReclaimJoysticks();
    bool heldThroughGame(SDL_JoystickID which, Action a, const QString &input);
    void settleAfterHandoff(SDL_JoystickID which);

    QTimer m_pollTimer;
    // The poll's interval for the pads connected now, and the poll set to it
    // while it runs.
    int pollInterval() const;
    void followPadsInPoll();
    QTimer m_repeatDelayTimer;
    QTimer m_repeatTimer;
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
        controllers::LabelStyle labels = controllers::LabelStyle::ABelow; // never Auto
        // Its input that is MAME's coin (input.cfg's token), its Arcade
        // Buttons' Coin as it acts on the pad (controllers::arcadeActing()):
        // Back unless they say.
        QString coin = QStringLiteral("back");
    };
    QHash<SDL_JoystickID, Pad> m_pads;
    QHash<qint64, int> m_axisState;                  // (pad, axis) → engaged direction: -1 / 0 / +1
    QHash<QString, controllers::Profile> m_profiles; // config.json app.controllers.<key>
    // The key each profile read was saved under where it isn't its key now
    // (controllers::currentKey(): an earlier version's whole SDL GUID), for
    // the line a pad that connects is logged with. The next save writes it
    // under its key now.
    QHash<QString, QString> m_profileFrom;
    // Each connected pad's key, as it was found when it connected
    // (controllers::sharedPadKey()): kept while it stays, so that its page
    // goes on being its own whatever settings are saved or reset meanwhile.
    QHash<SDL_JoystickID, QString> m_padKeys;
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
    struct CapturePress {
        QPair<QString, int> held;                    // the input held down (HeldInput)
        QString key;
        QString input;
        QString label;
        qint64 at = 0;                               // m_clock's time it was pressed
    };
    QList<CapturePress> m_capturePresses;            // held down, the oldest first
    QTimer m_captureStopTimer;                       // the oldest held long enough
    // What each action was last done with: a key's id, -1 for a pad.
    QHash<int, int> m_lastActionBy;
    // The pad buttons input.cfg has a line for (loadUserMapping()).
    QSet<int> m_cfgButtons;
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
    // Pad inputs held down as the pads opened again after a game (the Coin
    // and Start that quit it), pressed before m_handoffSettleEnds (m_clock's
    // time), or on a pad of m_lateAfterHandoff (heldThroughGame()): neither
    // their press nor their release goes to the menus.
    QSet<HeldInput> m_heldThroughHandoff;
    qint64 m_handoffSettleEnds = 0;
    // The pads opened again after a game that SDL hears only through their
    // reports, with m_clock's time until which their presses but their
    // directions' are taken for held through it, unless they are heard
    // holding nothing before (settleAfterHandoff()).
    QHash<SDL_JoystickID, qint64> m_lateAfterHandoff;
    bool m_padsReportLate = false;                   // padsReportLateForTest()
    // A remote's buttons held down that went to the views as keys of their
    // own (readNode()): their release is the key's.
    QHash<HeldInput, int> m_postedKeys;
    // Keys whose press a wait for a press took (Qt::Key): swallowed until let
    // go, as their repeats would act as what they were just given.
    QSet<int> m_capturedKeys;
    // setBackTaker()'s, the back keys held that went to the player (Qt::Key:
    // their repeats and their release go nowhere else), and what
    // noteKeyToViews() has said.
    std::function<bool()> m_playerTakesBack;
    QSet<int> m_backToPlayer;
    QSet<QString> m_keysToViewsSaid;
    // Cabinet Lock: whether it is on (config.json app.cabinet_lock), its
    // unlock buttons (padInputId()) and their names (app.unlock_buttons).
    bool m_lockOn = false;
    QStringList m_unlockInputs;
    QStringList m_unlockNames;
    // Those of them held down now, each as the input it is (a key's source
    // "keys", its Qt key); all of them start m_unlockTimer.
    QHash<QString, HeldInput> m_unlockHeld;
    QTimer m_unlockTimer;
    QElapsedTimer m_unlockSince;    // since all of them were held
    // Presses of theirs held back, in the order they came, and what they do.
    struct HeldBack {
        QString id;
        QString source;   // a pad's padSource(), "keys"
        std::function<void()> act;
    };
    QList<HeldBack> m_heldBack;
    QTimer m_heldBackTimer;
    // The devices of the keys replayed (replayKey()), as they were posted.
    QStringList m_replayDevices;
    // Waiting for new unlock buttons: what is held, as it was pressed, and its
    // name; the same held for two seconds is kept (m_captureHoldTimer), ten
    // seconds with nothing held end the wait (m_captureIdleTimer).
    bool m_unlockCapture = false;
    QList<QPair<QString, QString>> m_captureHeld;
    QTimer m_captureHoldTimer;
    QTimer m_captureIdleTimer;

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
        // A mouse's buttons are on it: read while the window can't take a
        // click (nodeClick()). A node that is only a mouse has no kind,
        // and is not listed.
        bool mouse = false;
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
