#include "ControllerProfile.h"

#include <QProcessEnvironment>

#include <algorithm>

#ifdef Q_OS_LINUX
#include <linux/input-event-codes.h>
#endif

namespace controllers {

namespace {

struct Style {
    LabelStyle style;
    const char *name;
    const char *title;
};
const Style kStyles[] = {
    { LabelStyle::Auto,        "auto",        "Auto" },
    { LabelStyle::Xbox,        "xbox",        "Xbox" },
    { LabelStyle::PlayStation, "playstation", "PlayStation" },
    { LabelStyle::Nintendo,    "nintendo",    "Nintendo" },
    { LabelStyle::Arcade,      "arcade",      "Arcade" },
};

// A pad's buttons and triggers as each style prints them, by input.cfg's
// token: the face buttons by position (a is south), the rest by their place.
// The font has no ✕ ○ □ △: PlayStation's are spelled as the hints always have.
struct Names {
    const char *token;
    const char *xbox;
    const char *playstation;
    const char *nintendo;
    const char *arcade;
};
const Names kNames[] = {
    { "a",             "A",        "X",        "B",       "1" },
    { "b",             "B",        "O",        "A",       "2" },
    { "x",             "X",        "SQ",       "Y",       "3" },
    { "y",             "Y",        "TR",       "X",       "4" },
    { "leftshoulder",  "LB",       "L1",       "L",       "5" },
    { "rightshoulder", "RB",       "R1",       "R",       "6" },
    { "lefttrigger+",  "LT",       "L2",       "ZL",      "7" },
    { "righttrigger+", "RT",       "R2",       "ZR",      "8" },
    { "back",          "BACK",     "SELECT",   "-",       "COIN" },
    { "start",         "START",    "START",    "+",       "START" },
    { "guide",         "GUIDE",    "PS",       "HOME",    "MODE" },
    { "leftstick",     "LS",       "L3",       "LS",      "LS" },
    { "rightstick",    "RS",       "R3",       "RS",      "RS" },
    { "misc1",         "SHARE",    "MIC",      "CAPTURE", "MISC" },
    { "touchpad",      "TOUCHPAD", "TOUCHPAD", "TOUCHPAD", "TOUCHPAD" },
};

const char *const kActions[] = { "up", "down", "left", "right", "select", "back", "play_pause" };

bool knownAction(const QString &action) {
    if (action == QLatin1String("none"))
        return true;
    for (const char *a : kActions) {
        if (action == QLatin1String(a))
            return true;
    }
    return false;
}

// How long a press read from a node waits for the key event Qt makes of it.
// Qt's comes at once; this only bounds what a key Qt never delivers leaves.
constexpr qint64 kPressWindowMs = 2000;
constexpr int kMaxPending = 64;

QList<QPair<QString, QString>> &playerVariables() {
    static QList<QPair<QString, QString>> variables;
    return variables;
}

QList<ArcadeBinds> &playerArcadeBinds() {
    static QList<ArcadeBinds> binds;
    return binds;
}

const char *const kArcadeNames[kArcadeInputs] = { "1", "2", "3", "4", "5", "6", "7", "8", "coin", "start" };

struct Layout {
    const char *name;
    const char *title;
    const char *inputs[kArcadeInputs];   // none for default and custom
};
const Layout kLayouts[] = {
    { "default",   "Default",    {} },
    { "fighting6", "Fighting 6", { "x", "y", "rightshoulder", "a", "b", "righttrigger+",
                                   "leftshoulder", "lefttrigger+", "back", "start" } },
    { "neogeo4",   "Neo Geo 4",  { "a", "b", "x", "y", "leftshoulder", "rightshoulder",
                                   "lefttrigger+", "righttrigger+", "back", "start" } },
    { "custom",    "Custom",     {} },
};

// SDL's game controller buttons by input.cfg's token, as RetroArch binds
// them (SDL_GameControllerButton): the D-pad's are the joystick's.
struct Button {
    const char *token;
    int sdl;
};
const Button kButtons[] = {
    { "a", 0 }, { "b", 1 }, { "x", 2 }, { "y", 3 }, { "back", 4 }, { "guide", 5 }, { "start", 6 },
    { "leftstick", 7 }, { "rightstick", 8 }, { "leftshoulder", 9 }, { "rightshoulder", 10 },
    { "misc1", 15 }, { "paddle1", 16 }, { "paddle2", 17 }, { "paddle3", 18 }, { "paddle4", 19 },
    { "touchpad", 20 },
};
// And the axes that can be a button, pushed: the triggers pressed, the right
// stick either way (SDL_GameControllerAxis, its sign).
struct Axis {
    const char *token;
    const char *bind;
    const char *mame;
};
const Axis kAxes[] = {
    { "lefttrigger+",  "+4", "SLIDER1_NEG_SWITCH" },
    { "righttrigger+", "+5", "SLIDER2_NEG_SWITCH" },
    { "rightx+",       "+2", "ZAXIS_POS_SWITCH" },
    { "rightx-",       "-2", "ZAXIS_NEG_SWITCH" },
    { "righty+",       "+3", "RZAXIS_POS_SWITCH" },
    { "righty-",       "-3", "RZAXIS_NEG_SWITCH" },
};

// The buttons MAME's sdlgame numbers, in its order (its generalbuttons).
const char *const kMameNumbered[] = {
    "a", "b", "x", "y", "leftshoulder", "rightshoulder", "lefttrigger", "righttrigger",
    "leftstick", "rightstick", "paddle1", "paddle2", "paddle3", "paddle4", "guide", "misc1", "touchpad",
};

// kButtons' and kAxes' tokens in the order the page offers them. Not Guide,
// which shows MAME's menu.
const char *const kArcadeTokens[] = {
    "a", "b", "x", "y", "leftshoulder", "rightshoulder", "lefttrigger+", "righttrigger+", "back", "start",
    "leftstick", "rightstick", "misc1", "paddle1", "paddle2", "paddle3", "paddle4", "touchpad",
    "rightx-", "rightx+", "righty-", "righty+",
};

QString withoutSign(const QString &token) {
    return token.endsWith(QLatin1Char('+')) || token.endsWith(QLatin1Char('-')) ? token.chopped(1) : token;
}

} // namespace

QString arcadeInputName(int index) {
    return index >= 0 && index < kArcadeInputs ? QLatin1String(kArcadeNames[index]) : QString();
}

QString arcadeInputLabel(int index) {
    if (index == kArcadeCoin)
        return QStringLiteral("Coin");
    if (index == kArcadeStart)
        return QStringLiteral("Start");
    return index >= 0 && index < kArcadeInputs ? QStringLiteral("Button %1").arg(index + 1) : QString();
}

QStringList arcadeTokens() {
    QStringList tokens;
    for (const char *token : kArcadeTokens)
        tokens << QLatin1String(token);
    return tokens;
}

bool isArcadeToken(const QString &token) {
    return std::any_of(std::begin(kArcadeTokens), std::end(kArcadeTokens),
                       [&token](const char *t) { return token == QLatin1String(t); });
}

bool PadShape::has(const QString &token) const {
    return inputs.contains(withoutSign(token));
}

QStringList arcadeLayouts() {
    QStringList names;
    for (const Layout &l : kLayouts)
        names << QLatin1String(l.name);
    return names;
}

QString arcadeLayoutTitle(const QString &layout) {
    for (const Layout &l : kLayouts) {
        if (layout == QLatin1String(l.name))
            return QLatin1String(l.title);
    }
    return QLatin1String(kLayouts[0].title);
}

QStringList arcadeLayoutInputs(const QString &layout) {
    for (const Layout &l : kLayouts) {
        if (layout != QLatin1String(l.name) || !l.inputs[0])
            continue;
        QStringList inputs;
        for (const char *token : l.inputs)
            inputs << QLatin1String(token);
        return inputs;
    }
    return {};
}

QStringList arcadeDefaultInputs() {
    return arcadeLayoutInputs(QStringLiteral("neogeo4"));
}

QStringList arcadeActing(QStringList inputs, const PadShape &shape) {
    // Coin's own and Start's, then the other's.
    const QLatin1String back("back"), start("start");
    const std::pair<int, std::pair<QLatin1String, QLatin1String>> own[] = { { kArcadeCoin, { back, start } },
                                                                            { kArcadeStart, { start, back } } };
    for (const auto &[index, tokens] : own) {
        if (index >= inputs.size() || shape.has(inputs.at(index)))
            continue;
        for (const QLatin1String &token : { tokens.first, tokens.second }) {
            const int other = int(inputs.indexOf(token));
            if (!shape.has(token) || other == kArcadeCoin || other == kArcadeStart)
                continue;
            if (other >= 0)
                inputs[other] = QString();
            inputs[index] = token;
            break;
        }
    }
    return inputs;
}

QString mameInput(const PadShape &shape, const QString &token) {
    if (!shape.has(token))
        return QString();
    const QString name = withoutSign(token);
    int number = 0;
    for (const char *numbered : kMameNumbered) {
        const QLatin1String button(numbered);
        const bool trigger = button.endsWith(QLatin1String("trigger"));
        if (!shape.inputs.contains(button) || (trigger && !shape.buttonTriggers.contains(button)))
            continue;
        ++number;
        if (name == button)
            return trigger && !token.endsWith(QLatin1Char('+')) ? QString() : QStringLiteral("BUTTON%1").arg(number);
    }
    if (token == QLatin1String("back"))
        return QStringLiteral("SELECT");
    if (token == QLatin1String("start"))
        return QStringLiteral("START");
    for (const Axis &a : kAxes) {
        if (token == QLatin1String(a.token))
            return QLatin1String(a.mame);
    }
    return QString();
}

QString retroarchBind(const QString &token) {
    for (const Button &b : kButtons) {
        if (token == QLatin1String(b.token))
            return QString::number(b.sdl);
    }
    for (const Axis &a : kAxes) {
        if (token == QLatin1String(a.token))
            return QLatin1String(a.bind);
    }
    return QString();
}

LabelStyle labelStyleFromName(const QString &name) {
    for (const Style &s : kStyles) {
        if (name.compare(QLatin1String(s.name), Qt::CaseInsensitive) == 0)
            return s.style;
    }
    return LabelStyle::Auto;
}

QString labelStyleName(LabelStyle style) {
    return QLatin1String(kStyles[int(style)].name);
}

QString labelStyleTitle(LabelStyle style) {
    return QLatin1String(kStyles[int(style)].title);
}

QStringList labelStyleNames() {
    QStringList names;
    for (const Style &s : kStyles)
        names << QLatin1String(s.name);
    return names;
}

QString inputLabel(LabelStyle style, const QString &token) {
    static const QHash<QString, QString> dpad = {
        { QStringLiteral("dpup"),    QStringLiteral("D-PAD ▲") },
        { QStringLiteral("dpdown"),  QStringLiteral("D-PAD ▼") },
        { QStringLiteral("dpleft"),  QStringLiteral("D-PAD ◄") },
        { QStringLiteral("dpright"), QStringLiteral("D-PAD ►") },
    };
    if (dpad.contains(token))
        return dpad.value(token);

    // A stick's direction: leftx-, lefty+, rightx+... An arcade stick's
    // joystick comes as the left stick.
    const bool left = token.startsWith(QLatin1String("leftx")) || token.startsWith(QLatin1String("lefty"));
    const bool right = token.startsWith(QLatin1String("rightx")) || token.startsWith(QLatin1String("righty"));
    if ((left || right) && (token.endsWith('-') || token.endsWith('+'))) {
        const bool across = token.at(left ? 4 : 5) == QLatin1Char('x');
        const bool minus = token.endsWith('-');
        const QString arrow = across ? (minus ? QStringLiteral("◄") : QStringLiteral("►"))
                                     : (minus ? QStringLiteral("▲") : QStringLiteral("▼"));
        const QString stick = left ? (style == LabelStyle::Arcade ? QStringLiteral("JOYSTICK") : QStringLiteral("L STICK"))
                                   : QStringLiteral("R STICK");
        return stick + QLatin1Char(' ') + arrow;
    }

    for (const Names &n : kNames) {
        if (token != QLatin1String(n.token))
            continue;
        switch (style) {
        case LabelStyle::PlayStation: return QString::fromUtf8(n.playstation);
        case LabelStyle::Nintendo:    return QString::fromUtf8(n.nintendo);
        case LabelStyle::Arcade:      return QString::fromUtf8(n.arcade);
        case LabelStyle::Auto:
        case LabelStyle::Xbox:        return QString::fromUtf8(n.xbox);
        }
    }
    // The paddles and anything else: as input.cfg names it.
    QString plain = token;
    if (plain.endsWith('+') || plain.endsWith('-'))
        plain.chop(1);
    return plain.toUpper();
}

QString padKey(const QString &sdlGuid) {
    return QStringLiteral("pad-") + sdlGuid.toLower();
}

bool isJammaStick(const QString &name) {
    return name.startsWith(QLatin1String("OSD/OS JAMMA"));
}

QString legacyJammaKey(const QString &sdlGuid, const QString &name) {
    if (sdlGuid.size() != 32)
        return QString();
    // SDL's GUID of a device with neither: its bus and CRC, then its name in
    // the twelve bytes left, a nul at its end.
    QByteArray named = name.toUtf8().left(11);
    named.append(QByteArray(12 - named.size(), '\0'));
    return padKey(sdlGuid.left(8) + QString::fromLatin1(named.toHex()));
}

QString evdevKey(int bus, int vendor, int product) {
    return QStringLiteral("kbd-%1-%2-%3")
        .arg(bus & 0xffff, 4, 16, QLatin1Char('0'))
        .arg(vendor & 0xffff, 4, 16, QLatin1Char('0'))
        .arg(product & 0xffff, 4, 16, QLatin1Char('0'));
}

NodeKind classifyNode(const QSet<int> &keys) {
#ifdef Q_OS_LINUX
    // A joystick's or a gamepad's buttons: SDL's to read.
    for (const int k : keys) {
        if ((k >= BTN_JOYSTICK && k < BTN_DIGI) || (k >= BTN_TRIGGER_HAPPY && k <= BTN_TRIGGER_HAPPY40))
            return NodeKind::None;
    }
    if (keys.contains(KEY_A) && keys.contains(KEY_Z) && keys.contains(KEY_ENTER))
        return NodeKind::Keyboard;
    // What a remote has, its own buttons (Consumer Control) or a few keys; a
    // mouse has only buttons, a power button only that.
    static const int kRemoteKeys[] = {
        KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_ENTER, KEY_OK, KEY_SELECT, KEY_ESC, KEY_BACK,
        KEY_HOMEPAGE, KEY_MENU, KEY_INFO, KEY_EXIT, KEY_PLAYPAUSE, KEY_PLAY, KEY_PAUSE, KEY_STOP,
        KEY_REWIND, KEY_FASTFORWARD, KEY_NEXTSONG, KEY_PREVIOUSSONG, KEY_VOLUMEUP, KEY_VOLUMEDOWN,
        KEY_MUTE, KEY_CHANNELUP, KEY_CHANNELDOWN, KEY_RED, KEY_GREEN, KEY_YELLOW, KEY_BLUE,
    };
    for (const int k : kRemoteKeys) {
        if (keys.contains(k))
            return NodeKind::Remote;
    }
#else
    Q_UNUSED(keys)
#endif
    return NodeKind::None;
}

Profile Profile::fromVariant(const QVariant &value) {
    Profile p;
    const QVariantMap map = value.toMap();
    p.name = map.value(QStringLiteral("name")).toString();
    p.kind = map.value(QStringLiteral("kind")).toString();
    p.labels = labelStyleFromName(map.value(QStringLiteral("labels")).toString());
    const QVariantMap bindings = map.value(QStringLiteral("bindings")).toMap();
    for (auto it = bindings.cbegin(); it != bindings.cend(); ++it) {
        const QString action = it.value().toString().toLower();
        if (!it.key().isEmpty() && knownAction(action))
            p.bindings.insert(it.key().toLower(), action);
    }
    // Its Arcade Buttons: each input one only (the first that has it, of a
    // file edited by hand), and none the page couldn't have set: none at
    // all without a Coin and a Start.
    const QVariantMap arcade = map.value(QStringLiteral("arcade")).toMap();
    const QString layout = arcade.value(QStringLiteral("layout")).toString().toLower();
    if (layout != QLatin1String("default") && arcadeLayouts().contains(layout)) {
        const QVariantMap inputs = arcade.value(QStringLiteral("inputs")).toMap();
        p.arcade = inputs.isEmpty() ? arcadeLayoutInputs(layout) : QStringList();
        if (p.arcade.isEmpty()) {
            for (int i = 0; i < kArcadeInputs; ++i) {
                const QString token = inputs.value(arcadeInputName(i)).toString().toLower();
                p.arcade << (isArcadeToken(token) && !p.arcade.contains(token) ? token : QString());
            }
        }
        if (p.arcade.value(kArcadeCoin).isEmpty() || p.arcade.value(kArcadeStart).isEmpty())
            p.arcade.clear();
        else
            p.arcadeLayout = layout;
    }
    return p;
}

QVariantMap Profile::toVariant() const {
    QVariantMap bindingsMap;
    for (auto it = bindings.cbegin(); it != bindings.cend(); ++it)
        bindingsMap.insert(it.key(), it.value());
    QVariantMap map{ { QStringLiteral("name"), name },
                     { QStringLiteral("kind"), kind },
                     { QStringLiteral("labels"), labelStyleName(labels) },
                     { QStringLiteral("bindings"), bindingsMap } };
    if (!arcadeLayout.isEmpty()) {
        QVariantMap inputs;
        for (int i = 0; i < kArcadeInputs; ++i)
            inputs.insert(arcadeInputName(i), arcade.value(i));
        map.insert(QStringLiteral("arcade"), QVariantMap{ { QStringLiteral("layout"), arcadeLayout },
                                                          { QStringLiteral("inputs"), inputs } });
    }
    return map;
}

QStringList actionNames() {
    QStringList names;
    for (const char *a : kActions)
        names << QLatin1String(a);
    return names;
}

QString actionLabel(const QString &action) {
    if (action == QLatin1String("up"))         return QStringLiteral("Up");
    if (action == QLatin1String("down"))       return QStringLiteral("Down");
    if (action == QLatin1String("left"))       return QStringLiteral("Left");
    if (action == QLatin1String("right"))      return QStringLiteral("Right");
    if (action == QLatin1String("select"))     return QStringLiteral("Select / OK");
    if (action == QLatin1String("back"))       return QStringLiteral("Back");
    if (action == QLatin1String("play_pause")) return QStringLiteral("Play / Pause");
    return action;
}

void setPlayerEnvironment(const QList<QPair<QString, QString>> &variables) {
    playerVariables() = variables;
}

void applyPlayersTo(QProcessEnvironment &env) {
    // None inherited: a player without a controller has none.
    for (int p = 1; p <= kPlayers; ++p) {
        for (const char *part : { "_NAME", "_GUID", "_DEVICE" })
            env.remove(QStringLiteral("OSDOS_PLAYER%1%2").arg(p).arg(QLatin1String(part)));
    }
    for (const auto &variable : std::as_const(playerVariables()))
        env.insert(variable.first, variable.second);
}

namespace {

// One variable of each player's, OSDOS_PLAYER<n><part>: "" for a player
// without a controller.
QStringList playerValues(const char *part) {
    QStringList values;
    for (int p = 1; p <= kPlayers; ++p) {
        const QString name = QStringLiteral("OSDOS_PLAYER%1").arg(p) + QLatin1String(part);
        QString value;
        for (const auto &variable : std::as_const(playerVariables())) {
            if (variable.first == name)
                value = variable.second;
        }
        values << value;
    }
    return values;
}

} // namespace

QStringList playerGuids() {
    return playerValues("_GUID");
}

QStringList playerNames() {
    return playerValues("_NAME");
}

void setPlayerArcade(const QList<ArcadeBinds> &players) {
    playerArcadeBinds() = players;
}

QList<ArcadeBinds> playerArcade() {
    QList<ArcadeBinds> binds = playerArcadeBinds();
    binds.resize(kPlayers);
    return binds;
}

void KeySources::pressed(const QString &device, int code, qint64 ms) {
    while (!m_pending.isEmpty()
           && (ms - m_pending.first().ms > kPressWindowMs || m_pending.size() >= kMaxPending))
        m_pending.removeFirst();
    m_pending.append({ device, code, ms });
}

QString KeySources::takePress(quint32 nativeScanCode, qint64 ms) {
    // libinput and X11 report the evdev code plus 8. The latest press of it is
    // this one: an older one may be a press Qt never had (its window wasn't
    // the active one), which a newer press of the key elsewhere must not take.
    const int codes[] = { int(nativeScanCode) - 8, int(nativeScanCode) };
    for (const int code : codes) {
        for (int i = m_pending.size() - 1; i >= 0; --i) {
            const Press &p = m_pending.at(i);
            if (p.code != code || ms - p.ms > kPressWindowMs)
                continue;
            const QString device = p.device;
            m_pending.removeAt(i);
            m_held.insert(code, device);
            return device;
        }
    }
    return QString();
}

QString KeySources::heldBy(quint32 nativeScanCode) const {
    const QString device = m_held.value(int(nativeScanCode) - 8);
    return device.isEmpty() ? m_held.value(int(nativeScanCode)) : device;
}

void KeySources::released(quint32 nativeScanCode) {
    if (!m_held.remove(int(nativeScanCode) - 8))
        m_held.remove(int(nativeScanCode));
}

void KeySources::forget(const QString &device) {
    m_pending.erase(std::remove_if(m_pending.begin(), m_pending.end(),
                                   [&](const Press &p) { return p.device == device; }),
                    m_pending.end());
    for (auto it = m_held.begin(); it != m_held.end();) {
        if (it.value() == device)
            it = m_held.erase(it);
        else
            ++it;
    }
}

} // namespace controllers
