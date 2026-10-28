#include "InputManager.h"
#include "JammaPins.h"
#include "../AppCore.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QInputMethodQueryEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <algorithm>
#ifdef Q_OS_LINUX
#include <QSocketNotifier>
#include <QDir>
#include <linux/input.h>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#endif

namespace {
// Planted in nativeScanCode of synthesized events so the keyboard detector in
// eventFilter() can tell our gamepad-originated key events from real key presses.
constexpr quint32 kSyntheticScanCode = 0x240F00D;

// Extended key ids for the Consumer Control evdev path (see
// scanNodes()) are stored in m_keyRemap as kEvdevKeyBase plus
// the raw Linux KEY_* code. Qt::Key's own values (ASCII range, or the
// "special key" range starting at 0x01000000) never reach this high, so the
// two id spaces can share one QHash with no risk of collision.
constexpr int kEvdevKeyBase = 0x02000000;

// Same idea, for a mouse button (Qt::MouseButton), some "air mouse" remotes
// wire their center/OK button as a literal left click rather than any kind of
// key, so it never generates a QKeyEvent at all. Distinct base, same QHash.
constexpr int kMouseButtonBase = 0x03000000;

// Analog stick thresholds (of ±32768). Engage above one, release below the
// other — the gap prevents flutter when the stick rests near the threshold.
constexpr Sint16 kAxisEngage  = 16384;
constexpr Sint16 kAxisRelease = 12000;

// Held-direction auto-repeat, tuned to feel like keyboard repeat in lists.
constexpr int kRepeatDelayMs    = 400;
constexpr int kRepeatIntervalMs = 100;

// Qt reports both shift keys as Qt::Key_Shift; telling them apart takes the
// platform code. Linux keymaps (eglfs/evdev, X11, Wayland) report evdev's
// KEY_RIGHTSHIFT, with or without the X11-style +8 offset; macOS reports
// kVK_RightShift in the virtual key.
bool isRightShift(const QKeyEvent *ke) {
#ifdef Q_OS_MACOS
    return ke->nativeVirtualKey() == 0x3C;   // kVK_RightShift
#else
    const quint32 sc = ke->nativeScanCode();
    return sc == 54 || sc == 62;             // KEY_RIGHTSHIFT, +8 offset
#endif
}

// A key event's id, as the remaps and the waits for a press take it: Qt's
// key, but for a remote's OK and Select keys, which libinput's keymap has no
// symbol for (a key of 0, its scan code the evdev code plus 8), the id of a
// remote's button of that code, as a Consumer Control node's is.
int keyId(const QKeyEvent *ke) {
#ifdef Q_OS_LINUX
    if (ke->key() == 0) {
        const int code = int(ke->nativeScanCode()) - 8;
        if (code == KEY_OK || code == KEY_SELECT)
            return kEvdevKeyBase + code;
    }
#endif
    return ke->key();
}

// A USB sound card's volume HID mis-named as a gamepad (CM108 and the like),
// by its SDL name. Not opened as a controller — Settings → Audio Output is
// where that device belongs. Not by an Audio class interface on its USB
// device: a DualShock 4 or a DualSense on USB has one beside its pad.
bool looksLikeUsbAudioHid(const QString &name) {
    const QString n = name.toLower();
    return n.contains(QLatin1String("usb audio"))
        || n.contains(QLatin1String("usb pnp sound"))
        || n.contains(QLatin1String("usb headphone"))
        || n.contains(QLatin1String("usb sound"))
        || (n.contains(QLatin1String("c-media"))
            && (n.contains(QLatin1String("sound")) || n.contains(QLatin1String("audio"))
                || n.contains(QLatin1String("headphone"))));
}

// A remote's OK that the views don't take for Return (by keyId()): keypad
// Enter, which most of them don't handle, the Select key, and the OK and
// Select keys libinput has no symbol for.
bool isRemoteOk(int id) {
    switch (id) {
    case Qt::Key_Enter:
    case Qt::Key_Select:
#ifdef Q_OS_LINUX
    case kEvdevKeyBase + KEY_OK:
    case kEvdevKeyBase + KEY_SELECT:
#endif
        return true;
    default:
        return false;
    }
}

// Keys that act once per press however long they are held (by keyId()):
// Back, Select and Play/Pause. Held, Qt repeats them; a repeated Back opens a
// screen and leaves it at once (the main menu's Settings), or opens a video's
// menu and closes it. A Bluetooth keyboard can make it happen without
// holding: a key whose release comes late, as its first press after it wakes
// may, looks held. Backspace still repeats, to delete in the on-screen
// keyboard.
bool isOneShotKey(int id) {
    if (isRemoteOk(id))
        return true;
    switch (id) {
    case Qt::Key_Escape:
    case Qt::Key_Back:
    case Qt::Key_Return:
    case Qt::Key_Space:
        return true;
    default:
        return false;
    }
}

#ifdef Q_OS_LINUX
// Whose a key event is, by the presses a KeySources was told of as they were
// read from the nodes: a press takes the latest of its key, a repeat or a
// release goes to the one holding it, and a release lets go of it.
QString whoseKey(controllers::KeySources &sources, const QKeyEvent *ke, qint64 now) {
    const quint32 sc = ke->nativeScanCode();
    if (ke->type() == QEvent::KeyRelease) {
        const QString device = sources.heldBy(sc);
        if (!ke->isAutoRepeat())
            sources.released(sc);
        return device;
    }
    return ke->isAutoRepeat() ? sources.heldBy(sc) : sources.takePress(sc, now);
}
#endif

// What a pad's input is called: input.cfg's label for the button, else its
// name in the pad's label style.
QString padLabel(const QHash<int, QString> &overrides, controllers::LabelStyle style,
                 const QString &token, int button) {
    const QString label = button >= 0 ? overrides.value(button) : QString();
    return label.isEmpty() ? controllers::inputLabel(style, token) : label;
}

// (pad, axis) as one key.
qint64 axisKey(SDL_JoystickID pad, int axis) {
    return (qint64(pad) << 8) | axis;
}

// True when an editable text field (e.g. a QML TextInput/TextField) currently
// holds focus. Detected via the Qt::ImEnabled input-method query — the same
// signal virtual keyboards use to decide when to appear — so it doesn't depend
// on QML class names. Used to let Right Shift type shifted characters instead
// of acting as Back while the user is editing text.
bool textInputHasFocus() {
    QObject *fo = QGuiApplication::focusObject();
    if (!fo) return false;
    QInputMethodQueryEvent query(Qt::ImEnabled);
    QCoreApplication::sendEvent(fo, &query);
    return query.value(Qt::ImEnabled).toBool();
}
}

InputManager::InputManager(const QString &dataRoot, AppCore *appCore, QObject *parent)
    : QObject(parent)
    , m_appCore(appCore)
    , m_dataRoot(dataRoot)
{
    m_repeatDelayTimer.setSingleShot(true);
    m_repeatDelayTimer.setInterval(kRepeatDelayMs);
    m_repeatTimer.setInterval(kRepeatIntervalMs);
    connect(&m_pollTimer,        &QTimer::timeout, this, &InputManager::pollSdl);
    connect(&m_repeatDelayTimer, &QTimer::timeout, this, &InputManager::onRepeatDelayElapsed);
    connect(&m_repeatTimer,      &QTimer::timeout, this, &InputManager::onRepeatTick);
    m_clock.start();

    loadProfiles();
    loadPlayers();
    rebuildMapping();
    initSdl();

    loadKeyRemap();
    if (m_appCore)
        connect(m_appCore, &AppCore::appSettingChanged, this, &InputManager::onAppSettingChanged);

    // The JAMMA sticks' button test (watchJammaPins()).
    m_jammaPins = new JammaPins(this);
    connect(m_jammaPins, &JammaPins::pinChanged, this, &InputManager::jammaPinChanged);
    connect(m_jammaPins, &JammaPins::failed, this, &InputManager::jammaPinsFailed);

#ifdef Q_OS_LINUX
    // Keyboards and remotes, as they come and go: a Bluetooth keyboard makes
    // its node when it connects, and udev gives it to the input group a moment
    // later.
    m_nodeScanTimer.setSingleShot(true);
    connect(&m_nodeScanTimer, &QTimer::timeout, this, &InputManager::scanNodes);
    connect(&m_nodeWatcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        watchNodes();
        m_nodeRetries = 0;
        m_nodeScanTimer.start(300);
    });
    watchNodes();
    scanNodes();
#endif

    // Watch the data dir (not the file) so input.cfg can appear later and so
    // replace-on-save editors are caught; mtime check filters unrelated writes
    // (e.g. config.json saves land in the same dir).
    m_cfgLastModified = QFileInfo(m_dataRoot + "/input.cfg").lastModified();
    m_watcher.addPath(m_dataRoot);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &InputManager::onDataDirChanged);

    // App-wide filter: any real key press marks the keyboard as the active device.
    QCoreApplication::instance()->installEventFilter(this);
}

InputManager::~InputManager() {
    m_heldInputs.clear();   // nothing to let go of on the way out (closeNode())
    for (SDL_GameController *gc : std::as_const(m_controllers))
        SDL_GameControllerClose(gc);
    m_controllers.clear();
    if (m_sdlReady)
        SDL_Quit();
#ifdef Q_OS_LINUX
    const QList<Node *> nodes = m_nodes;
    for (Node *node : nodes)
        closeNode(node);
#endif
}

void InputManager::setTargetWindow(QQuickWindow *window) {
    m_window = window;
}

// ── SDL lifecycle ─────────────────────────────────────────────────────────────

void InputManager::initSdl() {
    // Keep receiving controller events while another window (mpv fullscreen)
    // has OS focus, and don't let SDL steal SIGINT/SIGTERM from Qt.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    // Force positional button semantics on Nintendo-type pads (default is
    // label-based there): "a" always means the SOUTH position, on every pad.
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");

#ifdef Q_OS_WIN
    // The app's main() is its own, not SDL2main's (SDL_MAIN_HANDLED).
    SDL_SetMainReady();
#endif
    // Game-controller subsystem only: no video, so this works headless (EGLFS).
    if (SDL_Init(SDL_INIT_GAMECONTROLLER) != 0) {
        qWarning("[input] SDL init failed: %s — gamepad support disabled", SDL_GetError());
        return;
    }
    m_sdlReady = true;

    const QString dbPath = m_dataRoot + "/gamecontrollerdb.txt";
    if (QFile::exists(dbPath)) {
        int added = SDL_GameControllerAddMappingsFromFile(dbPath.toUtf8().constData());
        if (added >= 0)
            qInfo("[input] loaded %d controller mappings from gamecontrollerdb.txt", added);
        else
            qWarning("[input] could not parse gamecontrollerdb.txt: %s", SDL_GetError());
    }

    // SDL emits CONTROLLERDEVICEADDED for already-connected pads on init,
    // so the poll loop handles initial enumeration and hotplug identically.
    m_pollTimer.start(16);
    qInfo("[input] SDL game-controller subsystem ready");
}

void InputManager::pollSdl() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_CONTROLLERDEVICEADDED:   openController(e.cdevice.which);  break;
        case SDL_CONTROLLERDEVICEREMOVED: closeController(e.cdevice.which); break;
        case SDL_CONTROLLERBUTTONDOWN:
            handleButton(e.cbutton.which, e.cbutton.button, true);
            break;
        case SDL_CONTROLLERBUTTONUP:
            handleButton(e.cbutton.which, e.cbutton.button, false);
            break;
        case SDL_CONTROLLERAXISMOTION:
            handleAxis(e.caxis.which, e.caxis.axis, e.caxis.value);
            break;
        default: break;
        }
    }
}

void InputManager::openController(int deviceIndex) {
    SDL_GameController *gc = SDL_GameControllerOpen(deviceIndex);
    if (!gc) {
        qWarning("[input] could not open controller %d: %s", deviceIndex, SDL_GetError());
        return;
    }
    SDL_Joystick *joystick = SDL_GameControllerGetJoystick(gc);
    const QString name = QString::fromUtf8(SDL_JoystickName(joystick));
    if (looksLikeUsbAudioHid(name)) {
        qInfo("[input] ignoring USB sound card HID as a controller: %s", qPrintable(name));
        SDL_GameControllerClose(gc);
        return;
    }
    SDL_JoystickID id = SDL_JoystickInstanceID(joystick);
    m_controllers.insert(id, gc);
    adoptLegacyJammaKey(id);
    rebuildPad(id);
    qInfo("[input] controller added: %s (%s)", qPrintable(m_pads.value(id).name), qPrintable(m_pads.value(id).key));
    recomputeSuppressedDevices();
    updatePlayers();
    emit controllersChanged();
}

void InputManager::closeController(SDL_JoystickID instanceId) {
    SDL_GameController *gc = m_controllers.take(instanceId);
    if (!gc)
        return;
    // By its own name, as it was added (rebuildPad()).
    qInfo("[input] controller removed: %s", qPrintable(m_pads.value(instanceId).name));
    SDL_GameControllerClose(gc);
    m_pads.remove(instanceId);

    letGoOfPad(instanceId);
    recomputeSuppressedDevices();   // built-in may have unplugged → unsuppress
    if (m_lastActiveController == instanceId) {
        m_lastActiveController = -1;
        updateHints();
    }
    updatePlayers();
    emit controllersChanged();
}

// ── Mapping ───────────────────────────────────────────────────────────────────

void InputManager::rebuildMapping() {
    loadDefaultMapping();
    loadUserMapping();
    rebuildPads();
    updateHints();
}

// Each pad's map: the shared one, its profile's bindings over it, and the
// labels it shows. Auto labels follow what the pad says it is (sdlType, its
// SDL_GameControllerType); the JAMMA board's sticks (osdos-jamma) are an
// arcade's. Also for a pad that isn't connected, to show its page.
InputManager::Pad InputManager::padFor(const QString &key, const QString &name, int sdlType) const {
    Pad pad;
    pad.key = key;
    pad.name = name;
    pad.arcade = controllers::isJammaStick(name);
    pad.buttons = m_buttonMap;
    pad.axes = m_axisMap;

    const controllers::Profile profile = m_profiles.value(key);
    for (auto it = profile.bindings.cbegin(); it != profile.bindings.cend(); ++it) {
        bool ok = false;
        const Action action = actionFromString(it.value(), &ok);
        int button = -1, axis = -1, sign = 0;
        if (!ok || !parsePadInput(it.key(), &button, &axis, &sign))
            continue;
        if (sign != 0) {
            auto pair = pad.axes.value(axis, { Action::None, Action::None });
            (sign < 0 ? pair.first : pair.second) = action;
            pad.axes[axis] = pair;
        } else {
            pad.buttons[button] = action;
        }
    }

    pad.labels = profile.labels;
    if (pad.labels == controllers::LabelStyle::Auto) {
        const auto type = SDL_GameControllerType(sdlType);
        bool nintendo = type == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO;
#if SDL_VERSION_ATLEAST(2, 24, 0)
        nintendo = nintendo
                || type == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT
                || type == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT
                || type == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR;
#endif
        const bool playstation = type == SDL_CONTROLLER_TYPE_PS3 || type == SDL_CONTROLLER_TYPE_PS4
                              || type == SDL_CONTROLLER_TYPE_PS5;
        pad.labels = pad.arcade ? controllers::LabelStyle::Arcade
                   : nintendo ? controllers::LabelStyle::Nintendo
                   : playstation ? controllers::LabelStyle::PlayStation
                   : controllers::LabelStyle::Xbox;
    }
    return pad;
}

// A pad's map by its key: a connected one's own, else what it would be.
InputManager::Pad InputManager::padOf(const QString &key, const QString &name) const {
    const auto live = std::find_if(m_pads.cbegin(), m_pads.cend(), [&key](const Pad &p) { return p.key == key; });
    return live != m_pads.cend() ? *live : padFor(key, name, m_seenAs.value(key).sdlType);
}

// The JAMMA board's player a stick of its is (osdos-jamma names them "OSD/OS
// JAMMA P1" and "OSD/OS JAMMA P2"), or 0.
int InputManager::jammaPlayer(const Pad &pad) {
    if (!pad.arcade)
        return 0;
    return pad.name.endsWith(QLatin1String("P1")) ? 1 : pad.name.endsWith(QLatin1String("P2")) ? 2 : 0;
}

// A pad's input as its profile and controllerInputCaptured() name it: a
// button ("a", "dpup"), or an axis with its direction ("leftx-",
// "righttrigger+"). False for neither.
bool InputManager::parsePadInput(const QString &token, int *button, int *axis, int *sign) {
    QString name = token;
    *sign = name.endsWith('+') ? +1 : name.endsWith('-') ? -1 : 0;
    if (*sign == 0) {
        *button = buttonFromToken(name);
        return *button >= 0;
    }
    name.chop(1);
    *axis = SDL_GameControllerGetAxisFromString(name.toUtf8().constData());
    return *axis != SDL_CONTROLLER_AXIS_INVALID;
}

InputManager::Action InputManager::padInputAction(const Pad &pad, const QString &input) {
    int button = -1, axis = -1, sign = 0;
    if (!parsePadInput(input, &button, &axis, &sign))
        return Action::None;
    if (sign == 0)
        return pad.buttons.value(button, Action::None);
    const auto pair = pad.axes.value(axis, { Action::None, Action::None });
    return sign < 0 ? pair.first : pair.second;
}

// How many of the inputs a connected pad has do this.
int InputManager::padInputsFor(SDL_JoystickID id, Action a) const {
    SDL_GameController *gc = m_controllers.value(id, nullptr);
    const Pad pad = m_pads.value(id);
    int count = 0;
    for (auto it = pad.buttons.cbegin(); it != pad.buttons.cend(); ++it) {
        if (it.value() == a && (!gc || SDL_GameControllerHasButton(gc, SDL_GameControllerButton(it.key()))))
            ++count;
    }
    for (auto it = pad.axes.cbegin(); it != pad.axes.cend(); ++it) {
        if (!gc || SDL_GameControllerHasAxis(gc, SDL_GameControllerAxis(it.key())))
            count += int(it->first == a) + int(it->second == a);
    }
    return count;
}

void InputManager::rebuildPad(SDL_JoystickID id) {
    SDL_GameController *gc = m_controllers.value(id, nullptr);
    if (!gc)
        return;
    SDL_Joystick *joystick = SDL_GameControllerGetJoystick(gc);
    char guid[64] = {};
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joystick), guid, sizeof guid);
    // The device's own name, not its mapping's: SDL gives a pad without a
    // vendor and product the mapping of one like it, name and all, so the
    // JAMMA board's P2 would be called P1.
    const char *name = SDL_JoystickName(joystick);
    if (!name || !*name)
        name = SDL_GameControllerName(gc);
    const int type = SDL_GameControllerGetType(gc);
    Pad pad = padFor(controllers::padKey(QString::fromLatin1(guid)),
                     name && *name ? QString::fromUtf8(name) : QStringLiteral("Gamepad"), type);
    // Its Arcade Buttons' Coin, by what it has.
    const controllers::Profile profile = m_profiles.value(pad.key);
    if (!profile.arcadeLayout.isEmpty())
        pad.coin = controllers::arcadeActing(profile.arcade, padShape(gc)).value(controllers::kArcadeCoin);
    m_pads.insert(id, pad);
    m_seenAs.insert(pad.key, { pad.name, pad.arcade ? QStringLiteral("arcade") : QStringLiteral("gamepad"), type });
}

// A JAMMA stick of an osdos-jamma that gives it a vendor and a product has
// another key than it had (controllers::legacyJammaKey()): its own settings,
// and a player's choice of it, go to its key now, so that nothing of the
// stick's is lost and no player is left waiting for a stick that won't come.
void InputManager::adoptLegacyJammaKey(SDL_JoystickID id) {
    SDL_GameController *gc = m_controllers.value(id, nullptr);
    if (!gc)
        return;
    SDL_Joystick *joystick = SDL_GameControllerGetJoystick(gc);
    const QString name = QString::fromUtf8(SDL_JoystickName(joystick));
    if (!controllers::isJammaStick(name))
        return;
    char guid[64] = {};
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joystick), guid, sizeof guid);
    const QString key = controllers::padKey(QString::fromLatin1(guid));
    const QString legacy = controllers::legacyJammaKey(QString::fromLatin1(guid), name);
    if (legacy.isEmpty() || legacy == key)
        return;
    // Its settings under its key already (made since) are the ones it keeps.
    const bool profile = m_profiles.contains(legacy);
    if (profile) {
        const controllers::Profile old = m_profiles.take(legacy);
        if (!m_profiles.contains(key))
            m_profiles.insert(key, old);
        m_seenAs.remove(legacy);
    }
    bool player = false;
    for (QString &chosen : m_playerKeys) {
        if (chosen == legacy) {
            chosen = key;
            player = true;
        }
    }
    if (profile || player)
        qInfo("[input] %s's settings follow it to its new key", qPrintable(name));
    if (profile)
        saveProfiles();
    if (player)
        savePlayers();
}

void InputManager::rebuildPads() {
    for (auto it = m_controllers.cbegin(); it != m_controllers.cend(); ++it)
        rebuildPad(it.key());
}

QString InputManager::buttonToken(int button) {
    const char *name = SDL_GameControllerGetStringForButton(SDL_GameControllerButton(button));
    return name ? QString::fromLatin1(name) : QString();
}

QString InputManager::axisToken(int axis, int sign) {
    const char *name = SDL_GameControllerGetStringForAxis(SDL_GameControllerAxis(axis));
    return name ? QString::fromLatin1(name) + (sign < 0 ? QLatin1Char('-') : QLatin1Char('+')) : QString();
}

void InputManager::loadDefaultMapping() {
    m_buttonMap.clear();
    m_axisMap.clear();
    m_labelOverrides.clear();
    m_buttonMap[SDL_CONTROLLER_BUTTON_DPAD_UP]       = Action::Up;
    m_buttonMap[SDL_CONTROLLER_BUTTON_DPAD_DOWN]     = Action::Down;
    m_buttonMap[SDL_CONTROLLER_BUTTON_DPAD_LEFT]     = Action::Left;
    m_buttonMap[SDL_CONTROLLER_BUTTON_DPAD_RIGHT]    = Action::Right;
    m_buttonMap[SDL_CONTROLLER_BUTTON_A]             = Action::Select;
    m_buttonMap[SDL_CONTROLLER_BUTTON_B]             = Action::Back;
    m_buttonMap[SDL_CONTROLLER_BUTTON_BACK]          = Action::Back;
    m_buttonMap[SDL_CONTROLLER_BUTTON_START]         = Action::PlayPause;
    m_buttonMap[SDL_CONTROLLER_BUTTON_LEFTSHOULDER]  = Action::Left;
    m_buttonMap[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = Action::Right;
    m_axisMap[SDL_CONTROLLER_AXIS_LEFTX] = { Action::Left, Action::Right };
    m_axisMap[SDL_CONTROLLER_AXIS_LEFTY] = { Action::Up,   Action::Down  };
}

InputManager::Action InputManager::actionFromString(const QString &name, bool *ok) {
    *ok = true;
    if (name == "up")         return Action::Up;
    if (name == "down")       return Action::Down;
    if (name == "left")       return Action::Left;
    if (name == "right")      return Action::Right;
    if (name == "select")     return Action::Select;
    if (name == "back")       return Action::Back;
    if (name == "play_pause" || name == "playpause") return Action::PlayPause;
    if (name == "none")       return Action::None;
    *ok = false;
    return Action::None;
}

QString InputManager::actionName(Action a) {
    switch (a) {
    case Action::Up:        return QStringLiteral("up");
    case Action::Down:      return QStringLiteral("down");
    case Action::Left:      return QStringLiteral("left");
    case Action::Right:     return QStringLiteral("right");
    case Action::Select:    return QStringLiteral("select");
    case Action::Back:      return QStringLiteral("back");
    case Action::PlayPause: return QStringLiteral("play_pause");
    case Action::None:      break;
    }
    return QStringLiteral("none");
}

// What a key does when nothing is bound to it (by keyId()): the views' own
// keys, a remote's OK and Select keys (isRemoteOk()), and a mouse's left and
// right buttons, Select / OK and Back: an air-mouse remote's OK is a left
// click while its pointer is on, and on a desktop a mouse works the menus
// the same way, the selection following its pointer (Main.qml).
InputManager::Action InputManager::defaultKeyAction(int id) const {
    switch (id) {
    case Qt::Key_Up:        return Action::Up;
    case Qt::Key_Down:      return Action::Down;
    case Qt::Key_Left:      return Action::Left;
    case Qt::Key_Right:     return Action::Right;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Select:    return Action::Select;
    case Qt::Key_Escape:
    case Qt::Key_Backspace:
    case Qt::Key_Back:      return Action::Back;
    case Qt::Key_Space:     return Action::PlayPause;
#ifdef Q_OS_LINUX
    case kEvdevKeyBase + KEY_OK:
    case kEvdevKeyBase + KEY_SELECT: return Action::Select;
#endif
    case kMouseButtonBase + int(Qt::LeftButton):  return Action::Select;
    case kMouseButtonBase + int(Qt::RightButton): return Action::Back;
    default:                return Action::None;
    }
}

// What a keyboard's or a remote's key does on it, as eventFilter() and
// readNode() go: its own extra key's action, then the shared remap's, then
// the views' own.
InputManager::Action InputManager::keyActionOn(const QString &key, int id) const {
    Action a = key.isEmpty() ? Action::None : profileAction(key, QString::number(id));
    if (a == Action::None)
        a = m_keyRemap.value(id, Action::None);
    return a != Action::None ? a : defaultKeyAction(id);
}

QVariantMap InputManager::captureOutcome(const QString &outcome, Action does) {
    const QString name = actionName(does);
    return QVariantMap{ { QStringLiteral("outcome"), outcome }, { QStringLiteral("action"), name },
                        { QStringLiteral("label"), controllers::actionLabel(name) } };
}

QVariantMap InputManager::keyCapture(const QString &key, int id, const QString &action) const {
    // All Keyboards & Remotes' wait: what the key does on the controller that
    // pressed it, the one used last (readNode() and eventFilter() note it).
    const Action does = keyActionOn(key.isEmpty() ? m_activeController : key, id);
    if (does == Action::Back)
        return captureOutcome(QStringLiteral("cancel"), does);
    if (essentialAction(does) || defaultKeyAction(id) != Action::None
        || (does != Action::None && actionName(does) == action))
        return captureOutcome(QStringLiteral("already"), does);
    if (action == QLatin1String("none") && !m_profiles.value(key).bindings.contains(QString::number(id)))
        return captureOutcome(QStringLiteral("other"), does);
    return captureOutcome(QStringLiteral("set"), does);
}

// Button names are POSITIONAL (Xbox reference layout): "a" is always the
// south face button regardless of what's printed on the pad. The positional
// aliases south/east/west/north and the long SDL_CONTROLLER_BUTTON_* forms
// (including SDL3-style SOUTH/EAST/…) resolve to the same buttons.
// Returns the SDL button, or -1 if the token isn't a button.
int InputManager::buttonFromToken(const QString &token) {
    QString name = token.toLower();
    name.remove(QStringLiteral("sdl_controller_button_"));
    if (name == "south")      name = QStringLiteral("a");
    else if (name == "east")  name = QStringLiteral("b");
    else if (name == "west")  name = QStringLiteral("x");
    else if (name == "north") name = QStringLiteral("y");
    const SDL_GameControllerButton button =
        SDL_GameControllerGetButtonFromString(name.toUtf8().constData());
    return button == SDL_CONTROLLER_BUTTON_INVALID ? -1 : int(button);
}

// $DATA_ROOT/input.cfg — case-insensitive, # comments, merged over defaults,
// bad lines skipped with a warning. Two line forms:
//   <input> <action>   bind a button/axis ("a", "south", "dpup", "lefty-"…)
//   label <button> <text>   override the footer label for a button
void InputManager::loadUserMapping() {
    QFile f(m_dataRoot + "/input.cfg");
    if (!f.exists())
        return;
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning("[input] could not read input.cfg: %s", qPrintable(f.errorString()));
        return;
    }

    QTextStream in(&f);
    int lineNo = 0, applied = 0;
    while (!in.atEnd()) {
        QString line = in.readLine();
        ++lineNo;
        int hash = line.indexOf('#');
        if (hash >= 0)
            line.truncate(hash);
        line = line.simplified();   // not lowercased: label text keeps its case
        if (line.isEmpty())
            continue;

        const QStringList parts = line.split(' ');

        if (parts[0].compare(QStringLiteral("label"), Qt::CaseInsensitive) == 0) {
            if (parts.size() != 3) {
                qWarning("[input] input.cfg line %d ignored (expected \"label <button> <text>\"): %s",
                         lineNo, qPrintable(line));
                continue;
            }
            const int button = buttonFromToken(parts[1]);
            if (button < 0) {
                qWarning("[input] input.cfg line %d ignored (unknown button \"%s\")",
                         lineNo, qPrintable(parts[1]));
                continue;
            }
            m_labelOverrides[button] = parts[2];
            ++applied;
            continue;
        }

        if (parts.size() != 2) {
            qWarning("[input] input.cfg line %d ignored (expected \"<input> <action>\"): %s",
                     lineNo, qPrintable(line));
            continue;
        }

        bool actionOk = false;
        const Action action = actionFromString(parts[1].toLower(), &actionOk);
        if (!actionOk) {
            qWarning("[input] input.cfg line %d ignored (unknown action \"%s\")",
                     lineNo, qPrintable(parts[1]));
            continue;
        }

        QString input = parts[0].toLower();
        input.remove(QStringLiteral("sdl_controller_axis_"));

        // Axis bindings carry a direction suffix (lefty-, triggerright+).
        int axisSign = 0;
        if (input.endsWith('+')) { axisSign = +1; input.chop(1); }
        else if (input.endsWith('-')) { axisSign = -1; input.chop(1); }

        // Accept the enum-style trigger names alongside SDL's string names.
        if (input == "triggerleft")  input = "lefttrigger";
        if (input == "triggerright") input = "righttrigger";

        if (axisSign != 0) {
            SDL_GameControllerAxis axis =
                SDL_GameControllerGetAxisFromString(input.toUtf8().constData());
            if (axis == SDL_CONTROLLER_AXIS_INVALID) {
                qWarning("[input] input.cfg line %d ignored (unknown axis \"%s\")",
                         lineNo, qPrintable(parts[0]));
                continue;
            }
            auto pair = m_axisMap.value(axis, { Action::None, Action::None });
            (axisSign < 0 ? pair.first : pair.second) = action;
            m_axisMap[axis] = pair;
        } else {
            const int button = buttonFromToken(input);
            if (button < 0) {
                qWarning("[input] input.cfg line %d ignored (unknown input \"%s\")",
                         lineNo, qPrintable(parts[0]));
                continue;
            }
            m_buttonMap[button] = action;
        }
        ++applied;
    }
    qInfo("[input] input.cfg: applied %d binding(s)", applied);
}

// Keyboard/remote-button remap, config.json's app.remote_keymap.<action>,
// each an int Qt::Key value (or unset/0, meaning "no extra key for this
// action"). Set from Settings > Remote Controls (views/RemapControls.qml).
// Unlike input.cfg's gamepad rebinds, this is additive: it never removes an
// action's default key, it only adds one more physical key that also fires
// it, so a bad remap can't lock the menus out from a plain keyboard.
void InputManager::loadKeyRemap() {
    m_keyRemap.clear();
    if (!m_appCore)
        return;
    static const struct { const char *name; Action action; } kRemapActions[] = {
        { "up",     Action::Up },
        { "down",   Action::Down },
        { "left",   Action::Left },
        { "right",  Action::Right },
        { "select", Action::Select },
        { "back",   Action::Back },
    };
    for (const auto &entry : kRemapActions) {
        bool ok = false;
        const int qtKey = m_appCore->get_setting(QString(), QStringLiteral("remote_keymap.") + entry.name).toInt(&ok);
        if (ok && qtKey != 0)
            m_keyRemap[qtKey] = entry.action;
    }
}

void InputManager::onAppSettingChanged(const QString &key, const QString &value) {
    Q_UNUSED(value)
    if (key.startsWith(QStringLiteral("remote_keymap.")))
        loadKeyRemap();
    if (key == QLatin1String("controllers") || key.startsWith(QStringLiteral("controllers."))) {
        loadProfiles();
        rebuildPads();
        publishArcade();
        updateHints();
        emit controllersChanged();
    }
    if (key == QLatin1String("players") || key.startsWith(QStringLiteral("players."))) {
        loadPlayers();
        updatePlayers();
    }
}

// config.json's app.players: { "1": { key, name }, "2": … }, the controller
// each player has, "" for Auto, and its name, for while it is away.
void InputManager::loadPlayers() {
    m_playerKeys = QStringList(controllers::kPlayers, QString());
    m_playerNames = m_playerKeys;
    if (!m_appCore)
        return;
    const QVariantMap all = m_appCore->get_setting(QString(), QStringLiteral("players")).toMap();
    for (int p = 0; p < controllers::kPlayers; ++p) {
        const QVariantMap one = all.value(QString::number(p + 1)).toMap();
        m_playerKeys[p] = one.value(QStringLiteral("key")).toString();
        m_playerNames[p] = one.value(QStringLiteral("name")).toString();
    }
}

// Saved, and taken up again through appSettingChanged; one that couldn't be
// (a full card) is read back as it was, so what the pads do is what the
// settings say: false then.
bool InputManager::savePlayers() {
    QVariantMap all;
    for (int p = 0; p < controllers::kPlayers; ++p)
        all.insert(QString::number(p + 1), QVariantMap{ { QStringLiteral("key"), m_playerKeys.value(p) },
                                                        { QStringLiteral("name"), m_playerNames.value(p) } });
    if (m_appCore) {
        if (m_appCore->save_setting(QString(), QStringLiteral("players"), all))
            return true;
        loadPlayers();
        return false;
    }
    updatePlayers();
    return true;
}

void InputManager::setPlayer(int player, const QString &key) {
    if (player < 1 || player > controllers::kPlayers)
        return;
    QString name;
    int connected = 0;
    for (auto it = m_pads.cbegin(); it != m_pads.cend(); ++it) {
        if (it->key != key || m_suppressedDevices.contains(it.key()))
            continue;
        ++connected;
        if (name.isEmpty())
            name = it->name;
    }
    if (!key.isEmpty() && connected == 0)
        return;   // only a pad that is there can be chosen
    const int p = player - 1;
    m_playerKeys[p] = key;
    m_playerNames[p] = name;
    // One pad, one player: a model is as many players as there are of it
    // connected. The others it was go back to Auto, the higher-numbered first.
    int players = 1;
    for (int other = 0; other < controllers::kPlayers && !key.isEmpty(); ++other) {
        if (other == p || m_playerKeys[other] != key || ++players <= connected)
            continue;
        m_playerKeys[other].clear();
        m_playerNames[other].clear();
    }
    savePlayers();
}

// Who plays: each player's chosen pad if it is there (a player whose pad is
// away has none until it is back), then Auto's players from the pads left:
// the JAMMA board's stick of the player's number, else the first left, the
// JAMMA board's sticks first, then the others in the order they came. SDL is
// told each pad's player index, which pads with player lights show (those
// that play none go on from 7); programs OSD/OS starts get the players in
// their environment (controllers::applyPlayersTo()).
void InputManager::updatePlayers() {
    const auto rank = [this](SDL_JoystickID id) {
        const Pad pad = m_pads.value(id);
        if (pad.arcade) {
            const int player = jammaPlayer(pad);
            return player > 0 ? player - 1 : 2;
        }
        return 3;
    };
    QList<SDL_JoystickID> order;
    for (auto it = m_pads.cbegin(); it != m_pads.cend(); ++it) {
        if (!m_suppressedDevices.contains(it.key()))
            order << it.key();
    }
    std::sort(order.begin(), order.end(), [&rank](SDL_JoystickID a, SDL_JoystickID b) {
        const int ra = rank(a), rb = rank(b);
        return ra != rb ? ra < rb : a < b;
    });

    QList<SDL_JoystickID> playing(controllers::kPlayers, -1);
    QSet<SDL_JoystickID> taken;
    for (int p = 0; p < controllers::kPlayers; ++p) {
        if (m_playerKeys.value(p).isEmpty())
            continue;
        for (const SDL_JoystickID id : std::as_const(order)) {
            if (!taken.contains(id) && m_pads.value(id).key == m_playerKeys.value(p)) {
                playing[p] = id;
                taken.insert(id);
                break;
            }
        }
    }
    for (int p = 0; p < controllers::kPlayers; ++p) {
        if (!m_playerKeys.value(p).isEmpty())
            continue;
        const auto free = std::find_if(order.cbegin(), order.cend(), [&](SDL_JoystickID id) {
            return !taken.contains(id) && rank(id) == p;
        });
        const auto first = std::find_if(order.cbegin(), order.cend(), [&](SDL_JoystickID id) {
            return !taken.contains(id);
        });
        const auto chosen = free != order.cend() ? free : first;
        if (chosen != order.cend()) {
            playing[p] = *chosen;
            taken.insert(*chosen);
        }
    }

    int next = controllers::kPlayers;
    for (const SDL_JoystickID id : std::as_const(order)) {
        SDL_GameController *gc = m_controllers.value(id, nullptr);
        if (!gc)
            continue;
        const int p = playing.indexOf(id);
        SDL_GameControllerSetPlayerIndex(gc, p >= 0 ? p : next++);
    }

    QList<QPair<QString, QString>> variables;
    for (int p = 0; p < controllers::kPlayers; ++p) {
        SDL_GameController *gc = m_controllers.value(playing[p], nullptr);
        if (!gc)
            continue;
        SDL_Joystick *joystick = SDL_GameControllerGetJoystick(gc);
        const QString prefix = QStringLiteral("OSDOS_PLAYER%1_").arg(p + 1);
        char guid[64] = {};
        SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joystick), guid, sizeof guid);
        variables << qMakePair(prefix + QLatin1String("NAME"), m_pads.value(playing[p]).name)
                  << qMakePair(prefix + QLatin1String("GUID"), QString::fromLatin1(guid));
#if SDL_VERSION_ATLEAST(2, 24, 0)
        const char *path = SDL_JoystickPath(joystick);
        if (path && *path)
            variables << qMakePair(prefix + QLatin1String("DEVICE"), QString::fromUtf8(path));
#endif
    }
    controllers::setPlayerEnvironment(variables);
    // Every pad's mapping as this SDL has it, for the emulators' SDL.
    QStringList mappings;
    for (SDL_GameController *gc : std::as_const(m_controllers)) {
        if (char *mapping = SDL_GameControllerMapping(gc)) {
            mappings << controllers::mappingWithoutCrc(QString::fromUtf8(mapping));
            SDL_free(mapping);
        }
    }
    mappings.sort();
    controllers::setPadMappings(mappings);

    m_playerPads = playing;
    publishArcade();
    emit playersChanged();
}

QVariantList InputManager::players() const {
    QVariantList list;
    for (int p = 0; p < controllers::kPlayers; ++p) {
        const SDL_JoystickID id = m_playerPads.value(p, -1);
        const auto pad = m_pads.constFind(id);
        const bool on = id >= 0 && pad != m_pads.constEnd();
        list << QVariantMap{ { QStringLiteral("player"), p + 1 },
                             { QStringLiteral("key"), m_playerKeys.value(p) },
                             { QStringLiteral("name"), on ? pad->name : m_playerNames.value(p) },
                             { QStringLiteral("playing"), on ? pad->key : QString() },
                             { QStringLiteral("connected"), on } };
    }
    return list;
}

// config.json's app.controllers: each controller's profile, by its key.
void InputManager::loadProfiles() {
    m_profiles.clear();
    if (!m_appCore)
        return;
    const QVariantMap all = m_appCore->get_setting(QString(), QStringLiteral("controllers")).toMap();
    for (auto it = all.cbegin(); it != all.cend(); ++it) {
        const controllers::Profile profile = controllers::Profile::fromVariant(it.value());
        if (profile.isEmpty())
            continue;
        m_profiles.insert(it.key(), profile);
        if (!profile.name.isEmpty() && !m_seenAs.contains(it.key()))
            m_seenAs.insert(it.key(), { profile.name, profile.kind });
    }
}

// Saved whole, so that a profile with nothing left in it goes from
// config.json too, and taken up again through appSettingChanged. One that
// couldn't be saved (a full card) is read back as it was: a binding the page
// reported set would otherwise act until the next start and be gone then.
// False for that.
bool InputManager::saveProfiles() {
    QVariantMap all;
    for (auto it = m_profiles.begin(); it != m_profiles.end();) {
        if (it->isEmpty()) {
            it = m_profiles.erase(it);
            continue;
        }
        all.insert(it.key(), it->toVariant());
        ++it;
    }
    if (m_appCore) {
        if (m_appCore->save_setting(QString(), QStringLiteral("controllers"), all))
            return true;
        loadProfiles();
        return false;
    }
    rebuildPads();
    publishArcade();
    updateHints();
    emit controllersChanged();
    return true;
}

InputManager::Action InputManager::profileAction(const QString &key, const QString &input) const {
    const auto it = m_profiles.constFind(key);
    if (it == m_profiles.constEnd())
        return Action::None;
    bool ok = false;
    const Action action = actionFromString(it->bindings.value(input), &ok);
    return ok ? action : Action::None;
}

void InputManager::noteController(const QString &key) {
    if (key.isEmpty() || key == m_activeController)
        return;
    m_activeController = key;
    emit activeControllerChanged();
}

QVariantList InputManager::controllers() const {
    struct Entry { QString key, name, kind; int count; int order; int jamma; };   // order 3: not connected
    QList<Entry> entries;
    const auto add = [&entries](const QString &key, const QString &name, const QString &kind, int order,
                                int jamma = 0) {
        for (Entry &e : entries) {
            if (e.key != key)
                continue;
            // The same model plugged in twice, or one device's nodes: one
            // line, its shortest name (a remote's Consumer Control node has
            // the name of its keyboard node and more).
            if (order == 0)
                ++e.count;
            if (name.size() < e.name.size())
                e.name = name;
            if (order < e.order) {
                e.order = order;
                e.kind = kind;
            }
            return;
        }
        entries.append({ key, name, kind, 1, order, jamma });
    };
    for (auto it = m_pads.cbegin(); it != m_pads.cend(); ++it) {
        if (!m_suppressedDevices.contains(it.key()))
            add(it->key, it->name, it->arcade ? QStringLiteral("arcade") : QStringLiteral("gamepad"), 0,
                jammaPlayer(*it));
    }
#ifdef Q_OS_LINUX
    for (const Node *node : m_nodes) {
        const bool keyboard = node->kind == controllers::NodeKind::Keyboard;
        add(node->key, node->name, keyboard ? QStringLiteral("keyboard") : QStringLiteral("remote"), keyboard ? 1 : 2);
    }
#endif
    // Away, with settings of their own: to look at, or to take back to the
    // defaults.
    const int connected = entries.size();
    for (auto it = m_profiles.cbegin(); it != m_profiles.cend(); ++it) {
        if (std::any_of(entries.cbegin(), entries.cbegin() + connected, [&it](const Entry &e) { return e.key == it.key(); }))
            continue;
        const bool pad = it.key().startsWith(QLatin1String("pad-"));
        QString kind = it->kind;
        if (kind.isEmpty())
            kind = pad ? QStringLiteral("gamepad") : QStringLiteral("keyboard");
        entries.append({ it.key(), it->name.isEmpty() ? it.key() : it->name, kind, 0, 3, 0 });
    }
    std::stable_sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        return a.order != b.order ? a.order < b.order : a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    QVariantList list;
    for (const Entry &e : entries)
        list << QVariantMap{ { QStringLiteral("key"), e.key }, { QStringLiteral("name"), e.name },
                             { QStringLiteral("kind"), e.kind }, { QStringLiteral("count"), e.count },
                             { QStringLiteral("present"), e.order < 3 }, { QStringLiteral("jamma"), e.jamma } };
    return list;
}

QVariantMap InputManager::controllerPage(const QString &key) const {
    const controllers::Profile profile = m_profiles.value(key);
    // Its settings' name, or what it was seen as, for one away.
    const Seen seen = m_seenAs.value(key);
    QString name = profile.name.isEmpty() ? seen.name : profile.name;
    QString kind = profile.name.isEmpty() ? seen.kind : profile.kind;
    bool present = false;
    const QVariantList listed = controllers();
    for (const QVariant &v : listed) {
        const QVariantMap c = v.toMap();
        if (c.value(QStringLiteral("key")).toString() == key) {
            name = c.value(QStringLiteral("name")).toString();
            kind = c.value(QStringLiteral("kind")).toString();
            present = c.value(QStringLiteral("present")).toBool();
            break;
        }
    }

    const bool isPad = key.startsWith(QLatin1String("pad-"));
    const Pad pad = isPad ? padOf(key, name) : Pad();

    QVariantList rows;
    for (const QString &action : controllers::actionNames()) {
        bool ok = false;
        const Action a = actionFromString(action, &ok);
        QStringList inputs;
        QString value;
        if (isPad) {
            for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b) {
                if (pad.buttons.value(b, Action::None) == a)
                    inputs << padLabel(m_labelOverrides, pad.labels, buttonToken(b), b);
            }
            for (int axis = 0; axis < SDL_CONTROLLER_AXIS_MAX; ++axis) {
                const auto pair = pad.axes.value(axis, { Action::None, Action::None });
                if (pair.first == a)
                    inputs << controllers::inputLabel(pad.labels, axisToken(axis, -1));
                if (pair.second == a)
                    inputs << controllers::inputLabel(pad.labels, axisToken(axis, +1));
            }
            value = inputs.isEmpty() ? QStringLiteral("None") : inputs.join(QStringLiteral(", "));
        } else {
            for (auto it = profile.bindings.cbegin(); it != profile.bindings.cend(); ++it) {
                if (it.value() == action)
                    inputs << keyDisplayName(it.key().toInt());
            }
            inputs.sort(Qt::CaseInsensitive);
            value = inputs.isEmpty() ? QStringLiteral("Default")
                                     : QStringLiteral("Default + ") + inputs.join(QStringLiteral(", "));
        }
        rows << QVariantMap{ { QStringLiteral("action"), action },
                             { QStringLiteral("label"), controllers::actionLabel(action) },
                             { QStringLiteral("value"), value } };
    }
    return QVariantMap{ { QStringLiteral("key"), key }, { QStringLiteral("name"), name },
                        { QStringLiteral("kind"), kind.isEmpty() ? (isPad ? QStringLiteral("gamepad") : QStringLiteral("keyboard")) : kind },
                        { QStringLiteral("present"), present },
                        { QStringLiteral("labels"), controllers::labelStyleName(profile.labels) },
                        { QStringLiteral("labelsInUse"), isPad ? controllers::labelStyleTitle(pad.labels) : QString() },
                        { QStringLiteral("jamma"), isPad ? jammaPlayer(pad) : 0 },
                        { QStringLiteral("arcade"), isPad ? controllers::arcadeLayoutTitle(profile.arcadeLayout) : QString() },
                        { QStringLiteral("rows"), rows } };
}

void InputManager::captureController(const QString &key) {
    captureControllers(key.isEmpty() ? QStringList() : QStringList{ key });
}

void InputManager::captureControllers(const QStringList &keys) {
    m_captureControllers = keys;
    m_captureControllers.removeAll(QString());
    m_captureEnds = m_captureControllers.isEmpty() ? 0 : m_clock.elapsed() + 12000;
}

void InputManager::watchJammaPins(bool on) {
    m_jammaPins->watch(on);
}

void InputManager::setJammaProgram(const QString &program) {
    m_jammaPins->setProgram(program);
}

bool InputManager::capturing(const QString &key) const {
    return !key.isEmpty() && m_captureControllers.contains(key) && m_clock.elapsed() < m_captureEnds;
}

// Its profile, to change, with what it is as it's seen now.
controllers::Profile &InputManager::editProfile(const QString &key) {
    const QVariantMap page = controllerPage(key);
    controllers::Profile &profile = m_profiles[key];
    profile.name = page.value(QStringLiteral("name")).toString();
    profile.kind = page.value(QStringLiteral("kind")).toString();
    return profile;
}

QVariantMap InputManager::bindControllerInput(const QString &key, const QString &input, const QString &action) {
    bool ok = false;
    const Action wanted = actionFromString(action, &ok);
    if (key.isEmpty() || input.isEmpty() || !ok)
        return {};
    const QString token = input.toLower();
    const bool isPad = key.startsWith(QLatin1String("pad-"));
    if (isPad) {
        // A pad keeps a Select / OK, a Back, an Up and a Down, or the menus
        // would be out of its reach: a button pressed again to start the
        // wait, say.
        const auto live = std::find_if(m_pads.cbegin(), m_pads.cend(), [&key](const Pad &p) { return p.key == key; });
        if (live != m_pads.cend()) {
            const Action does = padInputAction(*live, token);
            if (does == wanted)
                return captureOutcome(QStringLiteral("already"), does);
            if (essentialAction(does) && padInputsFor(live.key(), does) <= 1)
                return captureOutcome(QStringLiteral("only"), does);
        }
    } else {
        const QVariantMap capture = keyCapture(key, token.toInt(), action);
        if (capture.value(QStringLiteral("outcome")).toString() != QLatin1String("set"))
            return capture;
    }
    controllers::Profile &profile = editProfile(key);
    // A keyboard's or a remote's keys keep their own actions: only extra keys
    // are its profile's, and "none" takes one away.
    if (!isPad && wanted == Action::None)
        profile.bindings.remove(token);
    else
        profile.bindings.insert(token, action);
    if (!saveProfiles())
        return captureOutcome(QStringLiteral("unsaved"), wanted);
    return captureOutcome(QStringLiteral("set"), wanted);
}

void InputManager::setControllerLabels(const QString &key, const QString &style) {
    if (key.isEmpty())
        return;
    editProfile(key).labels = controllers::labelStyleFromName(style);
    saveProfiles();
}

void InputManager::resetController(const QString &key) {
    if (key.isEmpty())
        return;
    m_profiles.remove(key);
    saveProfiles();
}

// ── Arcade Buttons ────────────────────────────────────────────────────────────

QStringList InputManager::arcadeInputsOf(const QString &key) const {
    const controllers::Profile profile = m_profiles.value(key);
    const QStringList inputs = profile.arcadeLayout.isEmpty() ? controllers::arcadeDefaultInputs() : profile.arcade;
    SDL_GameController *gc = padController(key);
    return gc ? controllers::arcadeActing(inputs, padShape(gc)) : inputs;
}

SDL_GameController *InputManager::padController(const QString &key) const {
    const auto live = std::find_if(m_pads.cbegin(), m_pads.cend(), [&key](const Pad &p) { return p.key == key; });
    return live != m_pads.cend() ? m_controllers.value(live.key(), nullptr) : nullptr;
}

bool InputManager::saveArcade(const QString &key, const QString &layout, const QStringList &inputs) {
    controllers::Profile &profile = editProfile(key);
    profile.arcadeLayout = layout;
    profile.arcade = layout.isEmpty() ? QStringList() : inputs;
    return saveProfiles();
}

QVariantMap InputManager::arcadePage(const QString &key) const {
    if (!key.startsWith(QLatin1String("pad-")))
        return {};
    const QVariantMap controller = controllerPage(key);
    const QString name = controller.value(QStringLiteral("name")).toString();
    const Pad pad = padOf(key, name);
    const int jamma = jammaPlayer(pad);
    const QString layout = m_profiles.value(key).arcadeLayout;

    QVariantList layouts;
    for (const QString &l : controllers::arcadeLayouts())
        layouts << QVariantMap{ { QStringLiteral("layout"), l }, { QStringLiteral("title"), controllers::arcadeLayoutTitle(l) } };

    // What a pad connected has; one away may have any of them.
    SDL_GameController *gc = padController(key);
    const controllers::PadShape shape = padShape(gc);

    // Under Default MAME and FinalBurn Neo each number another pad's buttons
    // their own way: only its Back and Start are Coin and Start in both.
    const QStringList inputs = arcadeInputsOf(key);
    QVariantList rows;
    for (int i = 0; i < controllers::kArcadeInputs; ++i) {
        const bool theirs = layout.isEmpty() && !jamma && i < controllers::kArcadeCoin;
        const QString token = inputs.value(i);
        rows << QVariantMap{ { QStringLiteral("input"), controllers::arcadeInputName(i) },
                             { QStringLiteral("label"), controllers::arcadeInputLabel(i) },
                             { QStringLiteral("token"), token },
                             { QStringLiteral("value"), theirs ? QStringLiteral("Auto")
                                                        : token.isEmpty() || (gc && !shape.has(token)) ? QStringLiteral("None")
                                                        : padInputLabel(pad, token) } };
    }

    QVariantList choices;
    for (const QString &token : controllers::arcadeTokens()) {
        if (!gc || shape.has(token))
            choices << QVariantMap{ { QStringLiteral("token"), token }, { QStringLiteral("label"), padInputLabel(pad, token) } };
    }

    return QVariantMap{ { QStringLiteral("key"), key }, { QStringLiteral("name"), name },
                        { QStringLiteral("present"), controller.value(QStringLiteral("present")) },
                        { QStringLiteral("jamma"), jamma },
                        { QStringLiteral("layout"), layout.isEmpty() ? QStringLiteral("default") : layout },
                        { QStringLiteral("layoutTitle"), controllers::arcadeLayoutTitle(layout) },
                        { QStringLiteral("layouts"), layouts },
                        { QStringLiteral("rows"), rows },
                        { QStringLiteral("choices"), choices } };
}

void InputManager::setArcadeLayout(const QString &key, const QString &layout) {
    if (!key.startsWith(QLatin1String("pad-")) || !controllers::arcadeLayouts().contains(layout))
        return;
    if (layout == QLatin1String("default"))
        saveArcade(key, QString(), {});
    else if (layout == QLatin1String("custom"))
        saveArcade(key, layout, arcadeInputsOf(key));
    else
        saveArcade(key, layout, controllers::arcadeLayoutInputs(layout));
}

QVariantMap InputManager::setArcadeInput(const QString &key, const QString &input, const QString &token) {
    int index = -1;
    for (int i = 0; i < controllers::kArcadeInputs && index < 0; ++i) {
        if (input == controllers::arcadeInputName(i))
            index = i;
    }
    const QString wanted = token.toLower();
    if (!key.startsWith(QLatin1String("pad-")) || index < 0 || (wanted.isEmpty() && needsInput(index)))
        return {};
    const QString label = wanted.isEmpty() ? QStringLiteral("None") : padInputLabel(padOf(key, m_seenAs.value(key).name), wanted);
    const auto outcome = [&label](const QString &outcome, const QString &other = QString()) {
        return QVariantMap{ { QStringLiteral("outcome"), outcome }, { QStringLiteral("label"), label },
                            { QStringLiteral("other"), other } };
    };
    // Guide shows MAME's menu.
    if (wanted == QLatin1String("guide"))
        return outcome(QStringLiteral("menu"));
    if (!wanted.isEmpty() && !controllers::isArcadeToken(wanted))
        return outcome(QStringLiteral("joystick"));
    QStringList inputs = arcadeInputsOf(key);
    // Set under Default, it is a layout of its own, the pad's own inputs as
    // it starts from.
    if (!m_profiles.value(key).arcadeLayout.isEmpty() && inputs.value(index) == wanted)
        return outcome(QStringLiteral("already"));
    // One input, one of them: the one that had it takes this one's, none for
    // one the pad hasn't, unless that would leave Coin or Start none.
    const int other = wanted.isEmpty() ? -1 : inputs.indexOf(wanted);
    SDL_GameController *gc = padController(key);
    const QString own = gc && !padShape(gc).has(inputs.at(index)) ? QString() : inputs.at(index);
    if (other >= 0 && other != index && needsInput(other) && own.isEmpty())
        return outcome(QStringLiteral("kept"), controllers::arcadeInputLabel(other));
    if (other >= 0 && other != index)
        inputs[other] = own;
    inputs[index] = wanted;
    if (!saveArcade(key, QStringLiteral("custom"), inputs))
        return outcome(QStringLiteral("unsaved"));
    return other >= 0 && other != index ? outcome(QStringLiteral("swapped"), controllers::arcadeInputLabel(other))
                                        : outcome(QStringLiteral("set"));
}

QString InputManager::setArcadeInputs(const QString &key, const QStringList &tokens) {
    if (!key.startsWith(QLatin1String("pad-")) || tokens.size() != controllers::kArcadeInputs)
        return QString();
    QStringList inputs;
    for (const QString &t : tokens) {
        const QString token = t.toLower();
        if (token.isEmpty() ? needsInput(int(inputs.size()))
                            : !controllers::isArcadeToken(token) || inputs.contains(token))
            return QString();
        inputs << token;
    }
    return saveArcade(key, QStringLiteral("custom"), inputs) ? QStringLiteral("set") : QStringLiteral("unsaved");
}

// Coin and Start: a game can't be played without them.
bool InputManager::needsInput(int index) {
    return index == controllers::kArcadeCoin || index == controllers::kArcadeStart;
}

QString InputManager::arcadeLabel(const QString &key, const QString &input) const {
    if (!key.startsWith(QLatin1String("pad-")))
        return QString();
    const Pad pad = padOf(key, m_seenAs.value(key).name);
    for (int i = 0; i < controllers::kArcadeInputs; ++i) {
        if (input != controllers::arcadeInputName(i))
            continue;
        // Under Default another pad's buttons are the emulators' to number.
        if (m_profiles.value(key).arcadeLayout.isEmpty() && !jammaPlayer(pad) && i < controllers::kArcadeCoin)
            return QString();
        const QString token = arcadeInputsOf(key).value(i);
        SDL_GameController *gc = padController(key);
        return token.isEmpty() || (gc && !padShape(gc).has(token)) ? QString() : padInputLabel(pad, token);
    }
    return QString();
}

controllers::PadShape InputManager::padShape(SDL_GameController *gc) {
    controllers::PadShape shape;
    if (!gc)
        return shape;
    for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b) {
        if (SDL_GameControllerHasButton(gc, SDL_GameControllerButton(b)))
            shape.inputs.insert(buttonToken(b));
    }
    // MAME takes a Switch Pro Controller's triggers, and a pair of Joy-Cons',
    // for buttons, as they are.
    bool digitalTriggers = SDL_GameControllerGetType(gc) == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO;
#if SDL_VERSION_ATLEAST(2, 24, 0)
    digitalTriggers = digitalTriggers || SDL_GameControllerGetType(gc) == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR;
#endif
    for (int a = 0; a < SDL_CONTROLLER_AXIS_MAX; ++a) {
        const auto axis = SDL_GameControllerAxis(a);
        if (!SDL_GameControllerHasAxis(gc, axis))
            continue;
        const QString name = QString::fromLatin1(SDL_GameControllerGetStringForAxis(axis));
        shape.inputs.insert(name);
        const bool trigger = axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
        if (trigger && (digitalTriggers || SDL_GameControllerGetBindForAxis(gc, axis).bindType == SDL_CONTROLLER_BINDTYPE_BUTTON))
            shape.buttonTriggers.insert(name);
    }
    return shape;
}

// Each player's pad's Arcade Buttons, by what that pad has: none for a
// player without a pad, or whose pad has no layout of its own.
void InputManager::publishArcade() {
    QList<controllers::ArcadeBinds> players;
    for (int p = 0; p < controllers::kPlayers; ++p) {
        controllers::ArcadeBinds binds;
        const SDL_JoystickID id = m_playerPads.value(p, -1);
        SDL_GameController *gc = m_controllers.value(id, nullptr);
        const controllers::Profile profile = m_profiles.value(m_pads.value(id).key);
        if (gc && !profile.arcadeLayout.isEmpty()) {
            const controllers::PadShape shape = padShape(gc);
            for (const QString &token : controllers::arcadeActing(profile.arcade, shape)) {
                binds.mame << controllers::mameInput(shape, token);
                binds.retroarch << (shape.has(token) ? controllers::retroarchBind(token) : QString());
            }
        }
        players << binds;
    }
    controllers::setPlayerArcade(players);
}

void InputManager::setRemapCapture(bool active) {
    m_remapCapture = active;
}

#ifdef Q_OS_LINUX
// The keyboards and remotes, from their /dev/input nodes (read-only, never
// grabbed, so Qt goes on reading them too): each node of one device, by its
// bus, vendor and product, under that device's key.
//
// Most keep sending their keys to Qt, and are read here only to tell whose a
// key is (m_keySources, eventFilter()). Not a "Consumer Control" node: some
// remote/keyboard USB combos expose their Consumer Page HID buttons (Home,
// Back, Menu, the coloured buttons, zoom, media transport...) as a node of
// their own beside the keyboard's. Qt's evdev keyboard handler doesn't read
// such a node; libinput takes it for a keyboard, but its keymap has no
// symbol for some of those buttons (OK, Select). They are acted on from
// here, as kEvdevKeyBase + their KEY_* code, through the device's own
// bindings, the shared remap (m_keyRemap) and a remote's OK
// (defaultKeyAction()); Qt's copy of a press taken here is dropped
// (m_consumerTaken), and one not taken goes on as Qt's key.
//
// Looked at again whenever /dev/input changes (a Bluetooth keyboard makes its
// node as it connects) and, while a node can't be opened yet (udev gives it
// to the input group a moment after it appears), every second for a while.
// /dev/input, or /dev while there is none: the kernel makes the folder with
// the first input device, and takes it away with the last.
void InputManager::watchNodes() {
    const QString input = QStringLiteral("/dev/input");
    const QString dev = QStringLiteral("/dev");
    const QStringList watched = m_nodeWatcher.directories();
    if (QFileInfo::exists(input)) {
        if (!watched.contains(input)) {
            m_nodeWatcher.removePath(dev);
            m_nodeWatcher.addPath(input);
        }
    } else if (!watched.contains(dev)) {
        m_nodeWatcher.removePath(input);
        m_nodeWatcher.addPath(dev);
    }
}

void InputManager::scanNodes() {
    // Gone: unplugged (the device answers ENODEV), or its node removed. A
    // node of the tests' own (adoptNodeForTest) goes as its pipe closes.
    const QList<Node *> open = m_nodes;
    bool changed = false;
    for (Node *node : open) {
        if (!node->path.startsWith(QLatin1String("/dev/")))
            continue;
        struct input_id id;
        if (::ioctl(node->fd, EVIOCGID, &id) < 0 || !QFileInfo::exists(node->path)) {
            closeNode(node);
            changed = true;
        }
    }

    const QDir dir(QStringLiteral("/dev/input"));
    const QStringList entries = dir.entryList(QStringList() << QStringLiteral("event*"), QDir::System);
    bool denied = false;
    for (const QString &entry : entries) {
        const QString path = dir.filePath(entry);
        if (std::any_of(m_nodes.cbegin(), m_nodes.cend(), [&path](const Node *n) { return n->path == path; }))
            continue;
        const int fd = ::open(path.toUtf8().constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            denied = denied || errno == EACCES;
            continue;
        }
        constexpr int kBitsPerLong = 8 * sizeof(unsigned long);
        unsigned long bits[(KEY_MAX + kBitsPerLong) / kBitsPerLong] = {};
        char name[256] = {};
        struct input_id id = {};
        if (::ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) < 0 || ::ioctl(fd, EVIOCGID, &id) < 0
            || ::ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) < 0) {
            ::close(fd);
            continue;
        }
        // A TV's remote through HDMI-CEC: its node is there whenever the Pi's
        // HDMI is, and silent, as nothing here sets CEC up.
        if (id.bustype == BUS_CEC) {
            ::close(fd);
            continue;
        }
        QSet<int> keys;
        for (int k = 0; k <= KEY_MAX; ++k) {
            if ((bits[k / kBitsPerLong] >> (k % kBitsPerLong)) & 1UL)
                keys.insert(k);
        }
        const controllers::NodeKind kind = controllers::classifyNode(keys);
        if (kind == controllers::NodeKind::None) {
            ::close(fd);
            continue;
        }
        addNode(fd, path, QString::fromUtf8(name).trimmed(),
                controllers::evdevKey(id.bustype, id.vendor, id.product), kind);
        changed = true;
    }

    if (!denied)
        m_nodeRetries = 0;
    else if (m_nodeRetries < 10) {
        ++m_nodeRetries;
        m_nodeScanTimer.start(1000);
    }
    if (changed)
        emit controllersChanged();
}

// A node opened: read as its events come (readNode()), and listed under its
// device.
void InputManager::addNode(int fd, const QString &path, const QString &name, const QString &key,
                           controllers::NodeKind kind) {
    Node *node = new Node;
    node->fd = fd;
    node->path = path;
    node->key = key;
    node->name = name;
    node->kind = kind;
    node->consumer = node->name.contains(QStringLiteral("Consumer Control"), Qt::CaseInsensitive);
    node->notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(node->notifier, &QSocketNotifier::activated, this, [this, node] { readNode(node); });
    m_nodes.append(node);
    // As the list names a device of several nodes: by its shortest name.
    const auto seen = m_seenAs.constFind(node->key);
    if (seen == m_seenAs.cend() || node->name.size() < seen->name.size())
        m_seenAs.insert(node->key, { node->name, kind == controllers::NodeKind::Keyboard ? QStringLiteral("keyboard")
                                                                                          : QStringLiteral("remote") });
    qInfo("[input] %s: %s (%s, %s)", kind == controllers::NodeKind::Keyboard ? "keyboard" : "remote",
          qPrintable(name), qPrintable(path), qPrintable(node->key));
}

void InputManager::adoptNodeForTest(int fd, const QString &name) {
    addNode(fd, QStringLiteral("test:") + name, name, QStringLiteral("test-") + name,
            controllers::NodeKind::Remote);
    emit controllersChanged();
}

void InputManager::readNode(Node *node) {
    struct input_event ev;
    for (;;) {
        const ssize_t n = ::read(node->fd, &ev, sizeof(ev));
        if (n != ssize_t(sizeof(ev))) {
            if (n < 0 && errno == EINTR)
                continue;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                return;   // drained
            // EOF or a hard error (ENODEV once the device is unplugged): tear
            // the notifier down, or it keeps firing on the dead fd and
            // busy-spins the event loop.
            qInfo("[input] %s gone (%s)", qPrintable(node->name), qPrintable(node->path));
            closeNode(node);
            emit controllersChanged();
            return;
        }
        if (ev.type != EV_KEY || ev.value == 2)   // ignore autorepeat and non-key events
            continue;
        if (!node->consumer) {
            // Qt has the key too, and acts on it (eventFilter()); this only
            // says whose it is.
            if (ev.value == 1)
                m_keySources.pressed(node->key, ev.code, m_clock.elapsed());
            continue;
        }

        // Let go: it ends what its press began, captured or bound anew since,
        // or the key its press went to the views as.
        const HeldInput button{ node->path, ev.code };
        if (ev.value == 0) {
            releaseAction(m_heldInputs.take(button));
            if (const int qtKey = m_postedKeys.take(button))
                postKey(qtKey, QEvent::KeyRelease, false);
            continue;
        }
        const int extendedId = kEvdevKeyBase + ev.code;
        noteController(node->key);
        // A press taken here, reported to a wait or acted on, is this node's
        // alone: Qt may have a copy of it too, as libinput takes such a node
        // for a keyboard, and eventFilter() drops that (m_consumerTaken).
        if (m_remapCapture) {
            // Capture mode: report the button, never act on it. Acting is what
            // must not happen here — finishing a capture writes the binding
            // into m_keyRemap before this function resumes, so a lookup after
            // the emit would fire the action on the very press that bound it.
            m_consumerTaken.pressed(node->key, ev.code, m_clock.elapsed());
            emit auxButtonPressed(extendedId);
            continue;
        }
        if (capturing(node->key)) {
            m_consumerTaken.pressed(node->key, ev.code, m_clock.elapsed());
            emit controllerInputCaptured(node->key, QString::number(extendedId), evdevKeyName(ev.code));
            continue;
        }

        const Action a = keyActionOn(node->key, extendedId);
        if (a == Action::None) {
            // A button no action takes goes to the views as the key it is,
            // where they act on one (the channel keys, which Qt's keymap has
            // no key for): once per press, with its release, and Qt's copy,
            // where it has one, goes no further.
            const int qtKey = qtKeyForEvdev(ev.code);
            if (qtKey) {
                m_consumerTaken.pressed(node->key, ev.code, m_clock.elapsed());
                m_postedKeys.insert(button, qtKey);
                postKey(qtKey, QEvent::KeyPress, false);
            }
            continue;
        }
        m_consumerTaken.pressed(node->key, ev.code, m_clock.elapsed());
        m_heldInputs.insert(button, a);
        pressFrom(QStringLiteral("keyboard"), a);
    }
}

// The key a button is to the views, where they act on it and nothing in
// Settings → Controls takes it: a TV remote's channel keys and page keys,
// which step through a folder's films during a video. For a Consumer
// Control node's button (readNode()), and for a key of a keyboard node that
// Qt has no symbol for (eventFilter(): libinput's keymap has none for the
// channel keys).
int InputManager::qtKeyForEvdev(int linuxCode) {
    switch (linuxCode) {
    case KEY_CHANNELUP:   return Qt::Key_ChannelUp;
    case KEY_CHANNELDOWN: return Qt::Key_ChannelDown;
    case KEY_PAGEUP:      return Qt::Key_PageUp;
    case KEY_PAGEDOWN:    return Qt::Key_PageDown;
    default:              return 0;
    }
}

// What the nodes have read so far, before the key event in hand is looked at:
// the kernel gives a key to every reader of its node at once, so the press Qt
// reports is in the node's queue by now. A Consumer Control node's press is
// acted on as it is read.
void InputManager::drainNodes() {
    const QList<Node *> nodes = m_nodes;
    for (Node *node : nodes) {
        if (m_nodes.contains(node))
            readNode(node);
    }
}

void InputManager::closeNode(Node *node) {
    m_nodes.removeOne(node);
    // Gone, its releases may never be read: what it holds is let go here.
    releaseHeld(node->path);
    const QString key = node->key;
    if (std::none_of(m_nodes.cbegin(), m_nodes.cend(), [&key](const Node *n) { return n->key == key; })) {
        m_keySources.forget(key);
        m_consumerTaken.forget(key);
    }
    if (node->notifier) {
        node->notifier->setEnabled(false);
        node->notifier->deleteLater();
    }
    ::close(node->fd);
    delete node;
}
#endif

// Display name for a Consumer Control button (extendedId - kEvdevKeyBase is
// the raw Linux KEY_* code). Covers the codes an ordinary remote is likely to
// send; anything else falls back to "Button <code>" rather than nothing.
QString InputManager::evdevKeyName(int linuxCode) {
#ifndef Q_OS_LINUX
    // The Consumer Control evdev path only exists on Linux (see
    // scanNodes()); nothing ever stores a code in this range
    // on other platforms, but the function still has to compile everywhere.
    return QStringLiteral("Button %1").arg(linuxCode);
#else
    switch (linuxCode) {
    case KEY_OK:          return QStringLiteral("OK");
    case KEY_HOMEPAGE:    return QStringLiteral("Home");
    case KEY_BACK:        return QStringLiteral("Back");
    case KEY_MENU:        return QStringLiteral("Menu");
    case KEY_INFO:        return QStringLiteral("Info");
    case KEY_EXIT:        return QStringLiteral("Exit");
    case KEY_SEARCH:      return QStringLiteral("Search");
    case KEY_WWW:         return QStringLiteral("WWW");
    case KEY_MAIL:        return QStringLiteral("Mail");
    case KEY_RED:         return QStringLiteral("Red");
    case KEY_GREEN:       return QStringLiteral("Green");
    case KEY_YELLOW:      return QStringLiteral("Yellow");
    case KEY_BLUE:        return QStringLiteral("Blue");
    case KEY_ZOOMIN:      return QStringLiteral("Zoom In");
    case KEY_ZOOMOUT:     return QStringLiteral("Zoom Out");
    case KEY_ZOOMRESET:   return QStringLiteral("Zoom Reset");
    case KEY_PLAY:        return QStringLiteral("Play");
    case KEY_PLAYPAUSE:   return QStringLiteral("Play/Pause");
    case KEY_PAUSE:       return QStringLiteral("Pause");
    case KEY_STOP:        return QStringLiteral("Stop");
    case KEY_REWIND:      return QStringLiteral("Rewind");
    case KEY_FASTFORWARD: return QStringLiteral("Fast Forward");
    case KEY_NEXTSONG:    return QStringLiteral("Next");
    case KEY_PREVIOUSSONG:return QStringLiteral("Previous");
    case KEY_RECORD:      return QStringLiteral("Record");
    case KEY_VOLUMEUP:    return QStringLiteral("Volume Up");
    case KEY_VOLUMEDOWN:  return QStringLiteral("Volume Down");
    case KEY_MUTE:        return QStringLiteral("Mute");
    case KEY_CHANNELUP:   return QStringLiteral("Channel Up");
    case KEY_CHANNELDOWN: return QStringLiteral("Channel Down");
    case KEY_PAGEUP:      return QStringLiteral("Page Up");
    case KEY_PAGEDOWN:    return QStringLiteral("Page Down");
    case KEY_BUTTONCONFIG:return QStringLiteral("Tools");
    case KEY_CONFIG:      return QStringLiteral("Config");
    case KEY_SELECT:      return QStringLiteral("Select");
    case KEY_POWER:       return QStringLiteral("Power");
    case KEY_SLEEP:       return QStringLiteral("Sleep");
    default:              return QStringLiteral("Button %1").arg(linuxCode);
    }
#endif
}

void InputManager::onDataDirChanged(const QString &) {
    const QFileInfo cfg(m_dataRoot + "/input.cfg");
    const QDateTime modified = cfg.exists() ? cfg.lastModified() : QDateTime();
    if (modified == m_cfgLastModified)
        return;
    m_cfgLastModified = modified;
    qInfo("[input] input.cfg changed — reloading mapping");
    rebuildMapping();
    emit controllersChanged();   // the pads' pages show their maps
}

// ── Input → action → synthesized key event ───────────────────────────────────

// Footer labels follow the controller last touched, so swapping between e.g.
// an Xbox pad and an 8BitDo keeps the face-button labels truthful.
void InputManager::noteActiveController(SDL_JoystickID which) {
    if (which == m_lastActiveController)
        return;
    m_lastActiveController = which;
    updateHints();
}

// When Steam Input is active it presents a managed "Steam Virtual Gamepad"
// alongside the raw controllers it mirrors — on the Steam Deck that's the
// built-in "Steam Deck" pad; on a desktop running Steam it's whatever pad is
// attached. The raw device echoes each press with ~0.5s of lag, so one physical
// press registers twice. The virtual pad is the low-latency one to use (it's
// also the device Gaming Mode drives), so whenever it is present, suppress every
// other controller. With no virtual pad (Steam not running), nothing is
// suppressed and raw controllers work normally. Recomputed on connect/disconnect.
// A pad suppressed lets go of what it held, as its releases are no longer heard.
void InputManager::recomputeSuppressedDevices() {
    const QSet<SDL_JoystickID> before = m_suppressedDevices;
    m_suppressedDevices.clear();
    auto isVirtual = [](SDL_GameController *gc) {
        const char *n = SDL_GameControllerName(gc);
        return n && qstrcmp(n, "Steam Virtual Gamepad") == 0;
    };
    bool hasVirtual = false;
    for (SDL_GameController *gc : std::as_const(m_controllers))
        if (isVirtual(gc)) { hasVirtual = true; break; }
    if (!hasVirtual)
        return;
    for (auto it = m_controllers.cbegin(); it != m_controllers.cend(); ++it)
        if (!isVirtual(it.value()))
            m_suppressedDevices.insert(it.key());
    for (const SDL_JoystickID id : std::as_const(m_suppressedDevices)) {
        if (!before.contains(id))
            letGoOfPad(id);
    }
}

// A pad's input goes by its own map (m_pads). While its page waits for a
// press (captureController()), a press is reported instead. A release lets go
// of what its press began (m_heldInputs), whatever the button does by then.
void InputManager::handleButton(SDL_JoystickID which, Uint8 button, bool pressed) {
    if (m_suppressedDevices.contains(which))
        return;
    const auto pad = m_pads.constFind(which);
    if (pad == m_pads.constEnd())
        return;
    const HeldInput input{ padSource(which), button };
    if (!pressed) {
        if (m_coinInputs.remove(input)) {
            postKey(Qt::Key_5, QEvent::KeyRelease, false);
            return;
        }
        const Action held = m_heldInputs.take(input);
        if (held != Action::None) {
            noteActiveController(which);
            releaseAction(held);
        }
        return;
    }
    const QString key = pad->key;
    noteController(key);
    if (capturing(key)) {
        const QString token = buttonToken(button);
        emit controllerInputCaptured(key, token, padLabel(m_labelOverrides, pad->labels, token, button));
        return;
    }
    // MAME's coin, where the focus takes one: its key, whatever the button
    // is bound to.
    if (!pad->coin.isEmpty() && buttonToken(button) == pad->coin && takesCoin()) {
        noteActiveController(which);
        setLastInputDevice(QStringLiteral("gamepad"));
        m_coinInputs.insert(input);
        postKey(Qt::Key_5, QEvent::KeyPress, false);
        return;
    }
    const Action a = pad->buttons.value(button, Action::None);
    if (a == Action::None)
        return;
    noteActiveController(which);
    m_heldInputs.insert(input, a);
    pressAction(a);
}

void InputManager::handleAxis(SDL_JoystickID which, Uint8 axis, Sint16 value) {
    if (m_suppressedDevices.contains(which))
        return;
    const auto pad = m_pads.constFind(which);
    if (pad == m_pads.constEnd())
        return;
    const QString key = pad->key;
    const controllers::LabelStyle labels = pad->labels;
    const QString coin = pad->coin;
    const QPair<Action, Action> actions = pad->axes.value(axis, { Action::None, Action::None });
    const HeldInput input{ padSource(which), SDL_CONTROLLER_BUTTON_MAX + axis };

    // Every axis, mapped or not: any can be bound on the pad's page, and
    // moving any finds the pad in the list.
    const qint64 stateKey = axisKey(which, axis);
    const int old = m_axisState.value(stateKey, 0);
    int now = old;
    if (old == 0) {
        if (value >= kAxisEngage)       now = +1;
        else if (value <= -kAxisEngage) now = -1;
    } else if (old > 0) {
        if (value < kAxisRelease)       now = (value <= -kAxisEngage) ? -1 : 0;
    } else {
        if (value > -kAxisRelease)      now = (value >= kAxisEngage) ? +1 : 0;
    }
    if (now == old)
        return;
    if (now == 0)
        m_axisState.remove(stateKey);
    else
        m_axisState.insert(stateKey, now);

    // Only a real engage/release counts as "using" this controller — idle
    // stick jitter must not steal label ownership from the pad in use. A
    // release lets go of what the engage began, as a button's does.
    if (old != 0) {
        if (m_coinInputs.remove(input)) {
            postKey(Qt::Key_5, QEvent::KeyRelease, false);
        } else {
            const Action released = m_heldInputs.take(input);
            if (released != Action::None) {
                noteActiveController(which);
                releaseAction(released);
            }
        }
    }
    if (now == 0)
        return;
    noteController(key);
    const QString token = axisToken(axis, now);
    if (capturing(key)) {
        emit controllerInputCaptured(key, token, controllers::inputLabel(labels, token));
        return;
    }
    // The coin on a trigger or the right stick (its Arcade Buttons'), as a
    // button's.
    if (!coin.isEmpty() && token == coin && takesCoin()) {
        noteActiveController(which);
        setLastInputDevice(QStringLiteral("gamepad"));
        m_coinInputs.insert(input);
        postKey(Qt::Key_5, QEvent::KeyPress, false);
        return;
    }
    const Action pressed = now < 0 ? actions.first : actions.second;
    if (pressed == Action::None)
        return;
    noteActiveController(which);
    m_heldInputs.insert(input, pressed);
    pressAction(pressed);
}

void InputManager::pressAction(Action a) {
    if (a == Action::None)
        return;
    pressFrom(QStringLiteral("gamepad"), a);
}

// A press from a device, which the footer's labels then follow: a remote's
// click or Consumer Control button is a keyboard's kind, not the pad's that
// was in use before it.
void InputManager::pressFrom(const QString &device, Action a) {
    setLastInputDevice(device);
    beginPress(a);
}

// Deliver a press and arm the held-direction auto-repeat. Shared by gamepad
// buttons/axes and remapped consumer-control/mouse inputs — the evdev path
// filters out autorepeat (and many remotes never send it), so a held direction
// button repeats through the same timers a held d-pad does. Remapped keyboard
// keys don't come through here: the OS supplies their repeat.
void InputManager::beginPress(Action a) {
    // Drop a press for an action already held. The Steam Deck reports one
    // physical press from two SDL devices (built-in + Steam Input virtual pad),
    // which would otherwise double every navigation. Genuine repeat still comes
    // from the timers below; a real double-tap alternates held state so both taps
    // register.
    if (m_heldActions.contains(a))
        return;
    m_heldActions.insert(a);

    deliverPress(a, false);

    if (isDirectional(a)) {
        // Most recent direction wins the repeat slot.
        m_heldDirection = a;
        m_repeatTimer.stop();
        m_repeatDelayTimer.start();
    }
}

void InputManager::releaseAction(Action a) {
    if (a == Action::None)
        return;
    // Ignore a release for an action that isn't held — the mirror device's
    // duplicate release, or a stray release with no matching press.
    if (!m_heldActions.remove(a))
        return;
    if (m_heldDirection == a) {
        m_heldDirection = Action::None;
        m_repeatDelayTimer.stop();
        m_repeatTimer.stop();
    }
    // mpv's "keypress" command is one-shot — releases only matter for QML.
    if (windowActive())
        postKey(qtKeyForAction(a), QEvent::KeyRelease, false);
}

QString InputManager::padSource(SDL_JoystickID id) {
    return QStringLiteral("pad %1").arg(id);
}

// A pad gone, or no longer listened to (recomputeSuppressedDevices()): what it
// held is let go, no direction left repeating, no stick latched.
void InputManager::letGoOfPad(SDL_JoystickID id) {
    releaseHeld(padSource(id));
    for (auto it = m_axisState.begin(); it != m_axisState.end();)
        it = (it.key() >> 8) == id ? m_axisState.erase(it) : std::next(it);
}

// The actions without which the menus are out of reach: a press on a page
// never takes a controller's last one away.
bool InputManager::essentialAction(Action a) {
    return a == Action::Select || a == Action::Back || a == Action::Up || a == Action::Down;
}

// A controller gone lets go of what its inputs held, for the releases it can
// no longer send.
void InputManager::releaseHeld(const QString &source) {
    QList<Action> held;
    for (auto it = m_heldInputs.begin(); it != m_heldInputs.end();) {
        if (it.key().first == source) {
            held << it.value();
            it = m_heldInputs.erase(it);
        } else {
            ++it;
        }
    }
    for (const Action a : std::as_const(held))
        releaseAction(a);
    for (auto it = m_coinInputs.begin(); it != m_coinInputs.end();) {
        if (it->first == source) {
            it = m_coinInputs.erase(it);
            postKey(Qt::Key_5, QEvent::KeyRelease, false);
        } else {
            ++it;
        }
    }
    for (auto it = m_postedKeys.begin(); it != m_postedKeys.end();) {
        if (it.key().first == source) {
            postKey(it.value(), QEvent::KeyRelease, false);
            it = m_postedKeys.erase(it);
        } else {
            ++it;
        }
    }
}

// While the Qt window is active, actions become posted key events into QML.
// When it isn't — fullscreen mpv owns OS focus on macOS, and a deactivated
// QQuickWindow has no activeFocusItem for key events to land on — actions go
// straight to mpv over IPC instead, mirroring what the Player views' key
// forwarding does on platforms where the window stays active (RPi/EGLFS).
// When mpv isn't running either, sendKey is a no-op, so background presses
// while the user is in another app do nothing — same as keyboard.
void InputManager::deliverPress(Action a, bool autoRepeat) {
    if (windowActive())
        postKey(qtKeyForAction(a), QEvent::KeyPress, autoRepeat);
    else
        emit mpvKeyRequested(mpvKeyForAction(a));
}

// A key that stood for an action (eventFilter()) let go: the action's key is
// let go too, as a view that acts on the release (the Arcade screen's select)
// waits for it. A key isn't held as a pad's button is (m_heldActions): the
// system repeats it itself. mpv's keypress has no release.
void InputManager::deliverRelease(Action a, bool autoRepeat) {
    if (windowActive())
        postKey(qtKeyForAction(a), QEvent::KeyRelease, autoRepeat);
}

void InputManager::onRepeatDelayElapsed() {
    if (m_heldDirection == Action::None)
        return;
    onRepeatTick();
    m_repeatTimer.start();
}

void InputManager::onRepeatTick() {
    if (m_heldDirection == Action::None)
        return;
    deliverPress(m_heldDirection, true);
}

bool InputManager::windowActive() const {
    return m_window && m_window->isActive();
}

bool InputManager::takesCoin() const {
    if (!windowActive())
        return false;
    // The nearest item round the focus that says.
    for (const QQuickItem *item = m_window->activeFocusItem(); item; item = item->parentItem()) {
        const QVariant takes = item->property("takesCoin");
        if (takes.isValid())
            return takes.toBool();
    }
    return false;
}

// Post to the root QQuickWindow, not QGuiApplication::focusWindow(): Qt Quick
// delivers posted key events to the window's activeFocusItem even when the
// window has no OS-level focus, which is exactly the state during fullscreen
// mpv playback on macOS.
void InputManager::postKey(int qtKey, QEvent::Type type, bool autoRepeat) {
    if (!m_window)
        return;
    QCoreApplication::postEvent(
        m_window,
        new QKeyEvent(type, qtKey, Qt::NoModifier,
                      kSyntheticScanCode, 0, 0, QString(), autoRepeat));
}

int InputManager::qtKeyForAction(Action a) {
    switch (a) {
    case Action::Up:        return Qt::Key_Up;
    case Action::Down:      return Qt::Key_Down;
    case Action::Left:      return Qt::Key_Left;
    case Action::Right:     return Qt::Key_Right;
    case Action::Select:    return Qt::Key_Return;
    case Action::Back:      return Qt::Key_Escape;
    case Action::PlayPause: return Qt::Key_Space;
    case Action::None:      break;
    }
    return 0;
}

// Same key names the Player views pass to mpvController.sendKey().
QString InputManager::mpvKeyForAction(Action a) {
    switch (a) {
    case Action::Up:        return QStringLiteral("UP");
    case Action::Down:      return QStringLiteral("DOWN");
    case Action::Left:      return QStringLiteral("LEFT");
    case Action::Right:     return QStringLiteral("RIGHT");
    case Action::Select:    return QStringLiteral("ENTER");
    case Action::Back:      return QStringLiteral("ESC");
    case Action::PlayPause: return QStringLiteral("SPACE");
    case Action::None:      break;
    }
    return QString();
}

bool InputManager::isDirectional(Action a) {
    return a == Action::Up || a == Action::Down || a == Action::Left || a == Action::Right;
}

QString InputManager::buttonLabel(const QString &key, const QString &token) const {
    if (!key.startsWith(QLatin1String("pad-")))
        return QString();
    return padInputLabel(padOf(key, m_seenAs.value(key).name), token);
}

QString InputManager::padInputLabel(const Pad &pad, const QString &token) const {
    int button = -1, axis = -1, sign = 0;
    if (parsePadInput(token, &button, &axis, &sign) && sign == 0)
        return padLabel(m_labelOverrides, pad.labels, token, button);
    return controllers::inputLabel(pad.labels, token);
}

QString InputManager::keyDisplayName(int qtKey) const {
    if (qtKey == 0)
        return QString();
    if (qtKey >= kMouseButtonBase)
        return mouseButtonName(qtKey - kMouseButtonBase);
    if (qtKey >= kEvdevKeyBase)
        return evdevKeyName(qtKey - kEvdevKeyBase);
    return QKeySequence(qtKey).toString(QKeySequence::NativeText);
}

QString InputManager::mouseButtonName(int qtButton) {
    switch (Qt::MouseButton(qtButton)) {
    case Qt::LeftButton:    return QStringLiteral("Mouse: Left Click");
    case Qt::RightButton:   return QStringLiteral("Mouse: Right Click");
    case Qt::MiddleButton:  return QStringLiteral("Mouse: Middle Click");
    case Qt::BackButton:    return QStringLiteral("Mouse: Back");
    case Qt::ForwardButton: return QStringLiteral("Mouse: Forward");
    default:                return QStringLiteral("Mouse Button %1").arg(qtButton);
    }
}

// HID media keys are a separate concern from navigation actions — they always
// target mpv, never QML — so they bypass the Action enum and map straight to the
// canonical mpv key names mpv-media-keys.lua binds.
QString InputManager::mpvKeyForMediaEvent(const QKeyEvent *ke) {
    switch (ke->key()) {
    case Qt::Key_VolumeUp:              return QStringLiteral("VOLUME_UP");
    case Qt::Key_VolumeDown:            return QStringLiteral("VOLUME_DOWN");
    case Qt::Key_VolumeMute:            return QStringLiteral("MUTE");
    case Qt::Key_MediaTogglePlayPause:
    case Qt::Key_MediaPlay:
    case Qt::Key_MediaPause:            return QStringLiteral("PLAYPAUSE");
    case Qt::Key_MediaStop:             return QStringLiteral("STOP");
    case Qt::Key_MediaNext:             return QStringLiteral("NEXT");
    case Qt::Key_MediaPrevious:         return QStringLiteral("PREV");
    case Qt::Key_AudioForward:          return QStringLiteral("FORWARD");
    case Qt::Key_AudioRewind:           return QStringLiteral("REWIND");
    default:                            return QString();
    }
}

// ── Active-device tracking & footer hints ─────────────────────────────────────

bool InputManager::eventFilter(QObject *obj, QEvent *event) {
    const QEvent::Type type = event->type();

    // The mouse wheel: each notch up or down is Up or Down, pressed and let go
    // as a key is. Taken at the window, which Qt Quick would hand it on from
    // to the items under the pointer; none of them scrolls by the wheel.
    if (type == QEvent::Wheel) {
        if (!obj || !obj->isWindowType())
            return false;
        if (m_remapCapture)
            return true;
        m_wheelTurn += static_cast<QWheelEvent *>(event)->angleDelta().y();
        while (qAbs(m_wheelTurn) >= 120) {
            const Action a = m_wheelTurn > 0 ? Action::Up : Action::Down;
            m_wheelTurn += a == Action::Up ? -120 : 120;
            pressFrom(QStringLiteral("keyboard"), a);
            releaseAction(a);
        }
        return true;
    }

    // Some remotes wire their center/OK button as a literal mouse click (see
    // scanNodes()'s comment for the sibling-devices story): an air mouse's OK
    // is a left click while its pointer is on.
    // Qt still delivers that as an ordinary QMouseEvent regardless of what
    // libinput thinks the device is, so no raw evdev reader is needed here,
    // just the same remap table and capture signal as everything else, and
    // left and right as Select / OK and Back where nothing else is bound to
    // them (defaultKeyAction()), on a desktop as on the console.
    if (type == QEvent::MouseButtonPress || type == QEvent::MouseButtonDblClick
        || type == QEvent::MouseButtonRelease) {
        const auto *me = static_cast<QMouseEvent *>(event);
        // A fast second press arrives as DblClick instead of Press — for a
        // remapped button it must count as a press, or every other click
        // leaks into the UI as a real one.
        const bool isPress = type != QEvent::MouseButtonRelease;
        const int extendedId = kMouseButtonBase + int(me->button());
        // Its release lets go of what its press began, as a pad's does.
        const HeldInput button{ QStringLiteral("mouse"), int(me->button()) };
        if (!isPress && m_heldInputs.contains(button)) {
            releaseAction(m_heldInputs.take(button));
            return true;
        }

        if (m_remapCapture) {
            if (isPress)
                emit auxButtonPressed(extendedId);
            return true;
        }

        const Action does = keyActionOn(QString(), extendedId);
        if (does != Action::None) {
            if (isPress) {
                m_heldInputs.insert(button, does);
                pressFrom(QStringLiteral("keyboard"), does);
            }
            return true;
        }
        return false;
    }

    if (type != QEvent::KeyPress && type != QEvent::KeyRelease)
        return false;
    const auto *ke = static_cast<QKeyEvent *>(event);
    const bool synthetic = ke->nativeScanCode() == kSyntheticScanCode;
    const int id = keyId(ke);

    // Whose key it is, from the keyboards' nodes (scanNodes()): worked out
    // where a real key event comes in, at its window, and not again for the
    // items it then goes on to, which see it through this filter too. A
    // Consumer Control node's press that readNode() took (acted on, or
    // reported to a wait) is that node's alone, and Qt's copy of it goes no
    // further.
    QString device;
#ifdef Q_OS_LINUX
    if (!synthetic && obj && obj->isWindowType() && !m_nodes.isEmpty()) {
        drainNodes();
        device = whoseKey(m_keySources, ke, m_clock.elapsed());
        if (type == QEvent::KeyPress && !ke->isAutoRepeat())
            noteController(device);
        if (device.isEmpty() && !whoseKey(m_consumerTaken, ke, m_clock.elapsed()).isEmpty())
            return true;
    }
#else
    Q_UNUSED(obj)
#endif

    if (type == QEvent::KeyPress && !synthetic)
        setLastInputDevice(QStringLiteral("keyboard"));

    // Back, Select and Play/Pause held (or let go late): the first press
    // only (isOneShotKey). Typing in a text field still repeats them.
    if (ke->isAutoRepeat() && isOneShotKey(id) && !textInputHasFocus())
        return true;

    // Right shift acts as Back so the keyboard works one-handed: reuse the
    // gamepad Back path, which posts Escape into QML — or sends ESC to mpv
    // over IPC when fullscreen mpv holds OS focus and the window can't take
    // key events. The bare Shift event is consumed; no view binds Key_Shift.
    //
    // Known gap: during fullscreen playback on macOS the keyboard goes to
    // mpv, not us, and mpv can't bind a bare modifier — so right shift only
    // works in the player on platforms where the app keeps the keyboard
    // (RPi/EGLFS). Same asymmetry as gamepads, minus their SDL workaround.
    // While a text field is focused, let Right Shift behave as an ordinary
    // modifier so the user can type shifted characters (e.g. ':' in a server
    // URL); only repurpose it as Back when nothing editable has focus. Falling
    // through (no return) lets the bare Shift event reach QML, where it's a
    // harmless no-op until the next character key arrives with the modifier.
    if (ke->key() == Qt::Key_Shift && isRightShift(ke) && !textInputHasFocus()) {
        if (!ke->isAutoRepeat()) {
            if (type == QEvent::KeyPress)
                deliverPress(Action::Back, false);
            else
                deliverRelease(Action::Back, false);
        }
        return true;
    }

    // A key a wait took (below) stays swallowed until it is let go: held,
    // its repeats would act as what it has just been given. A press anew
    // means its release went missing.
    if (!synthetic && m_capturedKeys.contains(ke->key())) {
        if (type == QEvent::KeyPress && !ke->isAutoRepeat()) {
            m_capturedKeys.remove(ke->key());
        } else {
            if (type == QEvent::KeyRelease && !ke->isAutoRepeat())
                m_capturedKeys.remove(ke->key());
            return true;
        }
    }

    // Capture mode (see setRemapCapture): swallow every real key and report
    // presses through auxButtonPressed instead of letting them reach QML.
    // Going through the signal rather than key delivery is what lets an
    // already-remapped key be captured as itself — the remap branch below
    // would otherwise consume it and hand the overlay its action's default
    // key. Sits after the right-shift block so that alias still cancels the
    // overlay via its normal Back path.
    if (m_remapCapture && !synthetic) {
        if (type == QEvent::KeyPress && !ke->isAutoRepeat()) {
            m_capturedKeys.insert(ke->key());
            emit auxButtonPressed(id);
        }
        return true;
    }

    // The same for one keyboard's or remote's page (captureController()):
    // its keys are reported, everyone else's go on as usual.
    if (capturing(device)) {
        if (type == QEvent::KeyPress && !ke->isAutoRepeat()) {
            m_capturedKeys.insert(ke->key());
            emit controllerInputCaptured(device, QString::number(id), keyDisplayName(id));
        }
        return true;
    }

    // Custom remote/keyboard remap (Settings > Remote Controls): an extra
    // physical key that also fires one of the six actions, delivered the same
    // way a gamepad press is: a synthesized key event carrying that action's
    // *default* Qt key, so every existing Keys.onPressed handler sees exactly
    // what it always has. The default key itself is untouched (this table is
    // additive), so remapping can't remove the app's own keyboard controls.
    // Skipped for our own synthesized events (nativeScanCode check) so two
    // remapped keys can never feed into each other, and skipped while a text
    // field has focus so typing a remapped character still works normally
    // (the hash miss is checked first — it's cheap, while textInputHasFocus
    // is a synchronous input-method query and shouldn't run per keystroke).
    // A keyboard's or a remote's own extra keys (its page in Settings →
    // Controls) come first, then the ones every keyboard shares. A remote's
    // OK that the views don't take for Return (isRemoteOk()) is Select the
    // same way.
    if (!synthetic) {
        Action remapped = device.isEmpty() ? Action::None : profileAction(device, QString::number(id));
        if (remapped == Action::None)
            remapped = m_keyRemap.value(id, Action::None);
        if (remapped == Action::None && isRemoteOk(id))
            remapped = Action::Select;
        if (remapped != Action::None && !textInputHasFocus()) {
            // Once per press, as the keys that stand for these by default
            // (isOneShotKey): a held key, or a remote's release that comes
            // late, must not select or go back again and again. A direction
            // repeats, as a held arrow does.
            if (ke->isAutoRepeat() && !isDirectional(remapped))
                return true;
            if (type == QEvent::KeyPress)
                deliverPress(remapped, ke->isAutoRepeat());
            else
                deliverRelease(remapped, ke->isAutoRepeat());
            return true;
        }
    }

#ifdef Q_OS_LINUX
    // A key libinput's keymap has no symbol for, known by its scan code (the
    // code plus 8): a remote's channel keys from its keyboard node, delivered
    // as the keys they are where the views act on them (a Consumer Control
    // node's come through readNode()).
    if (!synthetic && ke->key() == 0) {
        if (const int qtKey = qtKeyForEvdev(int(ke->nativeScanCode()) - 8)) {
            postKey(qtKey, type, ke->isAutoRepeat());
            return true;
        }
    }
#endif

    // HID media keys drive mpv directly over IPC (sendKey no-ops when mpv isn't
    // running, so they're harmless while browsing). Volume keys repeat while
    // held; the rest fire once per press so a held key is a single seek/chapter
    // jump. On macOS during fullscreen playback mpv holds the keyboard and these
    // never reach us — mpv binds the same names natively.
    const QString mediaKey = mpvKeyForMediaEvent(ke);
    if (!mediaKey.isEmpty()) {
        if (type == QEvent::KeyPress) {
            // A held volume key repeats as the keyboard repeats it (every 80 ms
            // on the Pi's console), each repeat 5 points in mpv: taken every
            // 150 ms, so that the whole range takes three seconds, as a TV
            // remote's does, rather than two. Everything else fires once per
            // press. mediaKey is the single source of truth for which key
            // this is.
            constexpr int kVolumeRepeatMs = 150;
            const bool isVolume = mediaKey.startsWith(QLatin1String("VOLUME"));
            const bool repeatDue = !m_volumeRepeat.isValid() || m_volumeRepeat.elapsed() >= kVolumeRepeatMs;
            if (!ke->isAutoRepeat() || (isVolume && repeatDue)) {
                if (isVolume)
                    m_volumeRepeat.start();
                emit mpvKeyRequested(mediaKey);
            }
        }
        return true;
    }
    return false;
}

void InputManager::setLastInputDevice(const QString &device) {
    if (m_lastInputDevice == device)
        return;
    m_lastInputDevice = device;
    emit lastInputDeviceChanged();
    updateHints();
}

// hints drives the footer labels in every view. Keyboard values are the exact
// strings the footers used before this existed; gamepad values come from a
// reverse lookup of the map of the pad last touched (enum order puts face
// buttons first), in its labels: input.cfg's for a button, else its style's
// (Settings → Controls, or what the pad says it is). Directional glyphs stay —
// they're d-pad-true on a controller.
void InputManager::updateHints() {
    QVariantMap h;
    h["navigate"]   = QStringLiteral("[▲▼]");
    h["arrows"]     = QStringLiteral("[▲▼◄►]");   // the trees: all four move
    h["change"]     = QStringLiteral("[◄►]");
    h["browse"]     = QStringLiteral("[►]");
    h["back"]       = QStringLiteral("[ESC]");
    h["select"]     = QStringLiteral("[ENTER]");
    h["play_pause"] = QStringLiteral("[SPACE]");
    // MAME's coin, where the focus takes one (takesCoin()): its key, or a
    // pad's Back button (its Arcade Buttons' Coin).
    h["coin"]       = QStringLiteral("[5]");

    if (m_lastInputDevice == QStringLiteral("gamepad")) {
        auto pad = m_pads.constFind(m_lastActiveController);
        if (pad == m_pads.constEnd())
            pad = m_pads.constBegin();
        const bool known = pad != m_pads.constEnd();
        const QHash<int, Action> &buttons = known ? pad->buttons : m_buttonMap;
        const controllers::LabelStyle style = known ? pad->labels : controllers::LabelStyle::Xbox;
        const auto buttonLabel = [&](Action a) -> QString {
            for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b) {
                if (buttons.value(b, Action::None) == a) {
                    const QString label = padLabel(m_labelOverrides, style, buttonToken(b), b);
                    if (!label.isEmpty())
                        return "[" + label + "]";
                }
            }
            // Bound to an axis alone (a trigger as Select): named as the
            // pad's page names it.
            if (known) {
                for (auto it = pad->axes.cbegin(); it != pad->axes.cend(); ++it) {
                    const int sign = it->first == a ? -1 : it->second == a ? +1 : 0;
                    if (sign == 0)
                        continue;
                    const QString label = controllers::inputLabel(style, axisToken(it.key(), sign));
                    if (!label.isEmpty())
                        return "[" + label + "]";
                }
            }
            return QString();  // unbound → keep keyboard label
        };
        const QString back = buttonLabel(Action::Back);
        const QString select = buttonLabel(Action::Select);
        const QString playPause = buttonLabel(Action::PlayPause);
        const QString coin = known ? padInputLabel(*pad, pad->coin)
                                   : padLabel(m_labelOverrides, style, buttonToken(SDL_CONTROLLER_BUTTON_BACK),
                                              SDL_CONTROLLER_BUTTON_BACK);
        if (!back.isEmpty())      h["back"]       = back;
        if (!select.isEmpty())    h["select"]     = select;
        if (!playPause.isEmpty()) h["play_pause"] = playPause;
        if (!coin.isEmpty())      h["coin"]       = "[" + coin + "]";
    }

    if (h != m_hints) {
        m_hints = h;
        emit hintsChanged();
    }
}
