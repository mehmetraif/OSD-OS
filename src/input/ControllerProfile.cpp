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

} // namespace

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
    return p;
}

QVariantMap Profile::toVariant() const {
    QVariantMap bindingsMap;
    for (auto it = bindings.cbegin(); it != bindings.cend(); ++it)
        bindingsMap.insert(it.key(), it.value());
    return QVariantMap{ { QStringLiteral("name"), name },
                        { QStringLiteral("kind"), kind },
                        { QStringLiteral("labels"), labelStyleName(labels) },
                        { QStringLiteral("bindings"), bindingsMap } };
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
