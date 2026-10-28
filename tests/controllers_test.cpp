#include "AppCore.h"
#include "input/ControllerProfile.h"
#include "input/InputManager.h"
#include "input/JammaPins.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QProcessEnvironment>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSaveFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <QWindow>

#include <SDL.h>
#include <functional>
#include <memory>
#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <linux/input.h>
#include <unistd.h>
#endif

// Settings → Controls, each controller apart. First what needs no device
// (ControllerProfile): what a pad's buttons are called in each label style,
// a profile as config.json keeps it (its Arcade Buttons too, and what MAME
// and RetroArch call each input), which /dev/input nodes are keyboards and
// remotes, and whose a key is. Then InputManager with pads SDL makes up (its
// virtual joysticks): each pad's own map over the shared one (the defaults
// and input.cfg), its presses reported instead of acting while its page waits
// for one, its labels in the hints, its Arcade Buttons and what its player's
// games get of them, a model of pad plugged in twice, the JAMMA board's
// sticks and their button test (both taken at once, with the board's pins
// from a stand-in osdos-jamma), the players' pads, and all of it kept in
// config.json for the next start; and a pad's Back button, or its Arcade
// Buttons' Coin, as MAME's coin key where the focus takes a coin.
class ControllersTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_app;
    QTemporaryDir m_data;

    // A pad SDL makes up, with every button and axis a game controller has.
    // It lasts as long as the InputManager whose SDL it was made in.
    struct VirtualPad {
        SDL_JoystickID id = -1;
        SDL_Joystick *joystick = nullptr;

        bool attach(const char *name, Uint16 vendor = 0x1209, Uint16 product = 0x0001) {
            SDL_VirtualJoystickDesc desc;
            SDL_zero(desc);
            desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
            desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
            desc.naxes = SDL_CONTROLLER_AXIS_MAX;
            desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
            desc.vendor_id = vendor;
            desc.product_id = product;
            desc.button_mask = (1u << SDL_CONTROLLER_BUTTON_MAX) - 1;
            desc.axis_mask = (1u << SDL_CONTROLLER_AXIS_MAX) - 1;
            desc.name = name;
            const int index = SDL_JoystickAttachVirtualEx(&desc);
            if (index < 0) {
                qWarning("virtual pad: %s", SDL_GetError());
                return false;
            }
            id = SDL_JoystickGetDeviceInstanceID(index);
            joystick = SDL_JoystickOpen(index);
            return joystick != nullptr;
        }
        void button(SDL_GameControllerButton b, bool down) { SDL_JoystickSetVirtualButton(joystick, b, down ? 1 : 0); }
        void axis(SDL_GameControllerAxis a, Sint16 value) { SDL_JoystickSetVirtualAxis(joystick, a, value); }
        void detach() {
            for (int i = 0; i < SDL_NumJoysticks(); ++i) {
                if (SDL_JoystickGetDeviceInstanceID(i) != id)
                    continue;
                SDL_JoystickClose(joystick);
                joystick = nullptr;
                SDL_JoystickDetachVirtual(i);
                return;
            }
        }
    };

    static QVariantMap entry(const InputManager &im, const QString &name) {
        const QVariantList list = im.controllers();
        for (const QVariant &v : list) {
            if (v.toMap().value("name").toString() == name)
                return v.toMap();
        }
        return {};
    }
    static QVariantMap player(const InputManager &im, int number) {
        const QVariantList list = im.players();
        for (const QVariant &v : list) {
            if (v.toMap().value("player").toInt() == number)
                return v.toMap();
        }
        return {};
    }
    static QString rowValue(const QVariantMap &page, const QString &action) {
        const QVariantList rows = page.value("rows").toList();
        for (const QVariant &v : rows) {
            if (v.toMap().value("action").toString() == action)
                return v.toMap().value("value").toString();
        }
        return QStringLiteral("<no row>");
    }
    static QStringList keysOf(const QSignalSpy &spy) {
        QStringList keys;
        for (const QList<QVariant> &args : spy)
            keys << args.at(0).toString();
        return keys;
    }
    // A press and its release, and the key (mpv's name for it) the action
    // sent; nothing, as "", when it sent none. The window isn't active (the
    // tests have none), so an action goes to mpv by name. Each returns once
    // SDL has seen the release (InputManager's poll has read it), so that a
    // press straight after can't fall between two of its polls.
    static QString tap(VirtualPad &pad, SDL_GameControllerButton b, QSignalSpy &keys) {
        keys.clear();
        pad.button(b, true);
        const bool sent = keys.wait(1000);
        const QString key = sent ? keys.first().at(0).toString() : QString();
        release(pad, b);
        return key;
    }
    static QString push(VirtualPad &pad, SDL_GameControllerAxis a, Sint16 value, QSignalSpy &keys) {
        keys.clear();
        pad.axis(a, value);
        const bool sent = keys.wait(1000);
        const QString key = sent ? keys.first().at(0).toString() : QString();
        centre(pad, a);
        return key;
    }
    static void release(VirtualPad &pad, SDL_GameControllerButton b) {
        pad.button(b, false);
        QVERIFY2(QTest::qWaitFor([&] { return SDL_JoystickGetButton(pad.joystick, b) == 0; }, 1000),
                 "SDL never saw the release");
    }
    static void centre(VirtualPad &pad, SDL_GameControllerAxis a) {
        pad.axis(a, 0);
        QVERIFY2(QTest::qWaitFor([&] { return SDL_JoystickGetAxis(pad.joystick, a) == 0; }, 1000),
                 "SDL never saw the stick back");
    }
    static QString outcome(const QVariantMap &result) { return result.value("outcome").toString(); }
    QJsonObject savedApp(const char *key) const {
        QFile f(m_data.path() + "/config.json");
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return QJsonDocument::fromJson(f.readAll()).object().value("app").toObject().value(key).toObject();
    }
    QJsonObject savedControllers() const { return savedApp("controllers"); }
    bool writeData(const QString &name, const QByteArray &data) const {
        QFile f(m_data.path() + "/" + name);
        return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
    }
    // A stand-in for osdos-jamma, a shell script, in the app's folder.
    QString script(const QString &name, const QByteArray &body) const {
        const QString path = m_app.path() + "/" + name;
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write("#!/bin/sh\n" + body) < 0)
            return QString();
        f.close();
        f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return path;
    }

private slots:
    void initTestCase() {
        QVERIFY(m_app.isValid());
        QVERIFY(m_data.isValid());
    }

    void init() {
        QFile::remove(m_data.path() + "/config.json");
        QFile::remove(m_data.path() + "/input.cfg");
    }

    // ── What needs no device ─────────────────────────────────────────────

    void labelsByStyle() {
        using controllers::inputLabel;
        using S = controllers::LabelStyle;
        // The face buttons by position, as each maker prints them.
        QCOMPARE(inputLabel(S::Xbox, "a"), QString("A"));
        QCOMPARE(inputLabel(S::PlayStation, "a"), QString("X"));
        QCOMPARE(inputLabel(S::Nintendo, "a"), QString("B"));
        QCOMPARE(inputLabel(S::Arcade, "a"), QString("1"));
        QCOMPARE(inputLabel(S::Xbox, "b"), QString("B"));
        QCOMPARE(inputLabel(S::PlayStation, "b"), QString("O"));
        QCOMPARE(inputLabel(S::Nintendo, "b"), QString("A"));
        QCOMPARE(inputLabel(S::Arcade, "y"), QString("4"));
        QCOMPARE(inputLabel(S::PlayStation, "x"), QString("SQ"));
        QCOMPARE(inputLabel(S::Nintendo, "y"), QString("X"));
        // The JAMMA board's eight buttons: B7 down to B0 are a, b, x, y, the
        // shoulders and the triggers (osdos-jamma).
        QCOMPARE(inputLabel(S::Arcade, "rightshoulder"), QString("6"));
        QCOMPARE(inputLabel(S::Arcade, "righttrigger+"), QString("8"));
        QCOMPARE(inputLabel(S::Arcade, "back"), QString("COIN"));
        QCOMPARE(inputLabel(S::Arcade, "start"), QString("START"));
        QCOMPARE(inputLabel(S::PlayStation, "lefttrigger+"), QString("L2"));
        QCOMPARE(inputLabel(S::Nintendo, "start"), QString("+"));
        QCOMPARE(inputLabel(S::Xbox, "leftshoulder"), QString("LB"));
        // The d-pad and the sticks with their direction.
        QCOMPARE(inputLabel(S::Xbox, "dpup"), QString("D-PAD ▲"));
        QCOMPARE(inputLabel(S::Nintendo, "dpright"), QString("D-PAD ►"));
        QCOMPARE(inputLabel(S::Xbox, "leftx-"), QString("L STICK ◄"));
        QCOMPARE(inputLabel(S::Xbox, "lefty+"), QString("L STICK ▼"));
        QCOMPARE(inputLabel(S::PlayStation, "righty-"), QString("R STICK ▲"));
        QCOMPARE(inputLabel(S::Arcade, "leftx+"), QString("JOYSTICK ►"));
        // What has no name of its own: as input.cfg calls it.
        QCOMPARE(inputLabel(S::Xbox, "paddle1"), QString("PADDLE1"));

        QCOMPARE(controllers::labelStyleFromName("PlayStation"), S::PlayStation);
        QCOMPARE(controllers::labelStyleFromName("bogus"), S::Auto);
        QCOMPARE(controllers::labelStyleNames(),
                 QStringList({ "auto", "xbox", "playstation", "nintendo", "arcade" }));
    }

    void profileAsKept() {
        const QVariantMap stored{
            { "name", "8BitDo Pro 2" }, { "kind", "gamepad" }, { "labels", "Nintendo" },
            { "bindings", QVariantMap{ { "A", "Select" }, { "leftx-", "back" }, { "x", "bogus" },
                                       { "", "up" }, { "b", "none" } } } };
        const controllers::Profile p = controllers::Profile::fromVariant(stored);
        QCOMPARE(p.name, QString("8BitDo Pro 2"));
        QCOMPARE(p.kind, QString("gamepad"));
        QCOMPARE(p.labels, controllers::LabelStyle::Nintendo);
        // An action it doesn't know, or no input, isn't one.
        QCOMPARE(p.bindings.size(), 3);
        QCOMPARE(p.bindings.value("a"), QString("select"));
        QCOMPARE(p.bindings.value("leftx-"), QString("back"));
        QCOMPARE(p.bindings.value("b"), QString("none"));

        const QVariantMap kept = p.toVariant();
        QCOMPARE(kept.value("labels").toString(), QString("nintendo"));
        QCOMPARE(controllers::Profile::fromVariant(kept).bindings, p.bindings);

        // A name alone is nothing to keep.
        controllers::Profile named;
        named.name = "Keyboard";
        QVERIFY(named.isEmpty());

        QCOMPARE(controllers::padKey("030000005E040000EA02000001000000"),
                 QString("pad-030000005e040000ea02000001000000"));
        QCOMPARE(controllers::evdevKey(0x5, 0x45e, 0x2fd), QString("kbd-0005-045e-02fd"));
    }

    // A pad's Arcade Buttons as config.json keeps them, and as MAME and
    // RetroArch name each of its inputs.
    void arcadeAsKept() {
        using namespace controllers;
        QCOMPARE(arcadeLayouts(), QStringList({ "default", "fighting6", "neogeo4", "custom" }));
        QCOMPARE(arcadeLayoutTitle("fighting6"), QString("Fighting 6"));
        QCOMPARE(arcadeLayoutTitle(""), QString("Default"));
        QCOMPARE(arcadeInputName(kArcadeCoin), QString("coin"));
        QCOMPARE(arcadeInputLabel(2), QString("Button 3"));
        QCOMPARE(arcadeInputLabel(kArcadeStart), QString("Start"));
        // A button, a trigger pressed, the right stick pushed; never the
        // D-pad or the left stick, the games' joystick, nor Guide, MAME's
        // menu.
        for (const char *token : { "a", "back", "paddle2", "lefttrigger+", "rightx-", "righty+" })
            QVERIFY2(isArcadeToken(token), token);
        for (const char *token : { "dpup", "leftx-", "lefty+", "lefttrigger", "lefttrigger-", "guide", "bogus", "" })
            QVERIFY2(!isArcadeToken(token), token);

        // A preset with no inputs given has the preset's; Default is none.
        Profile p = Profile::fromVariant(QVariantMap{ { "arcade", QVariantMap{ { "layout", "Fighting6" } } } });
        QCOMPARE(p.arcadeLayout, QString("fighting6"));
        QCOMPARE(p.arcade, arcadeLayoutInputs("fighting6"));
        QVERIFY(!p.isEmpty());
        QVERIFY(Profile::fromVariant(QVariantMap{ { "arcade", QVariantMap{ { "layout", "default" } } } }).isEmpty());
        // Set by hand: an input it can't be, or one another has (the first
        // keeps it), is none; one without a Coin or a Start is none at all.
        QVariantMap inputs{ { "1", "x" }, { "2", "dpup" }, { "3", "X" }, { "4", "rightx-" },
                            { "coin", "back" }, { "start", "start" } };
        p = Profile::fromVariant(QVariantMap{ { "arcade", QVariantMap{ { "layout", "custom" }, { "inputs", inputs } } } });
        QCOMPARE(p.arcadeLayout, QString("custom"));
        QCOMPARE(p.arcade, QStringList({ "x", "", "", "rightx-", "", "", "", "", "back", "start" }));
        const QVariantMap kept = p.toVariant().value("arcade").toMap();
        QCOMPARE(kept.value("layout").toString(), QString("custom"));
        QCOMPARE(kept.value("inputs").toMap().value("4").toString(), QString("rightx-"));
        QCOMPARE(kept.value("inputs").toMap().value("2").toString(), QString());
        QCOMPARE(Profile::fromVariant(p.toVariant()).arcade, p.arcade);
        inputs.remove("coin");
        p = Profile::fromVariant(QVariantMap{ { "arcade", QVariantMap{ { "layout", "custom" }, { "inputs", inputs } } } });
        QVERIFY(p.arcadeLayout.isEmpty() && p.arcade.isEmpty());
        QVERIFY(!Profile().toVariant().contains("arcade"));

        // MAME numbers a pad's buttons in its order, the triggers among them
        // only where they are buttons; Back and Start are SELECT and START,
        // an analog trigger pressed is its slider, negative.
        PadShape xbox;
        for (const char *input : { "a", "b", "x", "y", "back", "guide", "start", "leftstick", "rightstick",
                                   "leftshoulder", "rightshoulder", "leftx", "lefty", "rightx", "righty",
                                   "lefttrigger", "righttrigger" })
            xbox.inputs.insert(input);
        QCOMPARE(mameInput(xbox, "a"), QString("BUTTON1"));
        QCOMPARE(mameInput(xbox, "rightshoulder"), QString("BUTTON6"));
        QCOMPARE(mameInput(xbox, "leftstick"), QString("BUTTON7"));
        QCOMPARE(mameInput(xbox, "guide"), QString("BUTTON9"));
        QCOMPARE(mameInput(xbox, "back"), QString("SELECT"));
        QCOMPARE(mameInput(xbox, "start"), QString("START"));
        QCOMPARE(mameInput(xbox, "lefttrigger+"), QString("SLIDER1_NEG_SWITCH"));
        QCOMPARE(mameInput(xbox, "righttrigger+"), QString("SLIDER2_NEG_SWITCH"));
        QCOMPARE(mameInput(xbox, "rightx-"), QString("ZAXIS_NEG_SWITCH"));
        QCOMPARE(mameInput(xbox, "righty+"), QString("RZAXIS_POS_SWITCH"));
        QCOMPARE(mameInput(xbox, "paddle1"), QString());
        QCOMPARE(mameInput(xbox, ""), QString());
        // Triggers that are buttons (a JAMMA stick's, a Switch Pro's).
        PadShape stick = xbox;
        stick.buttonTriggers = { "lefttrigger", "righttrigger" };
        QCOMPARE(mameInput(stick, "lefttrigger+"), QString("BUTTON7"));
        QCOMPARE(mameInput(stick, "righttrigger+"), QString("BUTTON8"));
        QCOMPARE(mameInput(stick, "leftstick"), QString("BUTTON9"));
        QVERIFY(stick.has("lefttrigger+"));
        QVERIFY(!stick.has("paddle1"));
        // A pad with fewer buttons: those it has, numbered on.
        PadShape small;
        for (const char *input : { "a", "b", "back", "start", "leftshoulder" })
            small.inputs.insert(input);
        QCOMPARE(mameInput(small, "a"), QString("BUTTON1"));
        QCOMPARE(mameInput(small, "b"), QString("BUTTON2"));
        QCOMPARE(mameInput(small, "leftshoulder"), QString("BUTTON3"));
        QCOMPARE(mameInput(small, "x"), QString());
        QCOMPARE(mameInput(small, "lefttrigger+"), QString());

        // As they act on a pad: a Coin or a Start it hasn't is its own Back
        // or Start, which a button that had it gives up, or where the other
        // of the two has that, the other's own; one of the two never takes
        // the other's.
        const QStringList away{ "a", "b", "paddle1", "", "", "", "", "back", "touchpad", "start" };
        QCOMPARE(arcadeActing(away, small),
                 QStringList({ "a", "b", "paddle1", "", "", "", "", "", "back", "start" }));
        QCOMPARE(arcadeActing(away, xbox), arcadeActing(away, small));
        const QStringList swapped{ "a", "", "", "", "", "", "", "", "start", "back" };
        QCOMPARE(arcadeActing(swapped, small), swapped);
        QCOMPARE(arcadeActing({ "a", "", "", "", "", "", "", "", "start", "paddle1" }, small), swapped);
        QCOMPARE(arcadeActing({ "start", "", "", "", "", "", "", "", "paddle1", "back" }, small),
                 QStringList({ "", "", "", "", "", "", "", "", "start", "back" }));
        PadShape noBack = small;
        noBack.inputs.remove("back");
        QCOMPARE(arcadeActing(away, noBack), away);
        QCOMPARE(arcadeActing(arcadeDefaultInputs(), small), arcadeDefaultInputs());

        // RetroArch's: SDL's button numbers, an axis pushed.
        QCOMPARE(retroarchBind("a"), QString("0"));
        QCOMPARE(retroarchBind("back"), QString("4"));
        QCOMPARE(retroarchBind("rightshoulder"), QString("10"));
        QCOMPARE(retroarchBind("paddle1"), QString("16"));
        QCOMPARE(retroarchBind("lefttrigger+"), QString("+4"));
        QCOMPARE(retroarchBind("rightx-"), QString("-2"));
        QCOMPARE(retroarchBind("dpup"), QString());
        QCOMPARE(retroarchBind(""), QString());
    }

    void nodeKinds() {
#ifdef Q_OS_LINUX
        using controllers::classifyNode;
        using K = controllers::NodeKind;
        QSet<int> keyboard;
        for (int k = KEY_ESC; k <= KEY_KPDOT; ++k)
            keyboard.insert(k);
        QCOMPARE(classifyNode(keyboard), K::Keyboard);
        // A remote's Consumer Control, an IR remote's few keys.
        QCOMPARE(classifyNode({ KEY_VOLUMEUP, KEY_VOLUMEDOWN, KEY_PLAYPAUSE, KEY_HOMEPAGE }), K::Remote);
        QCOMPARE(classifyNode({ KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_OK }), K::Remote);
        // A USB sound card's volume / media HID alone is not a remote (CM108).
        QCOMPARE(classifyNode({ KEY_MUTE, KEY_VOLUMEUP, KEY_VOLUMEDOWN }), K::None);
        QCOMPARE(classifyNode({ KEY_VOLUMEUP, KEY_VOLUMEDOWN }), K::None);
        QCOMPARE(classifyNode({ KEY_MUTE }), K::None);
        QCOMPARE(classifyNode({ KEY_MUTE, KEY_VOLUMEUP, KEY_PLAYPAUSE, KEY_NEXTSONG }), K::None);
        QVERIFY(controllers::looksLikeUsbAudioHid(QStringLiteral("USB Audio Device")));
        QVERIFY(controllers::looksLikeUsbAudioHid(QStringLiteral("C-Media USB Headphone Set")));
        QVERIFY(!controllers::looksLikeUsbAudioHid(QStringLiteral("Xbox Wireless Controller")));
        // A pad is SDL's, even with keys; a mouse, a power button, nothing.
        QSet<int> pad = keyboard;
        pad.insert(BTN_SOUTH);
        QCOMPARE(classifyNode(pad), K::None);
        QCOMPARE(classifyNode({ BTN_TRIGGER_HAPPY1, KEY_UP }), K::None);
        QCOMPARE(classifyNode({ BTN_LEFT, BTN_RIGHT, BTN_MIDDLE }), K::None);
        QCOMPARE(classifyNode({ KEY_POWER }), K::None);
#else
        QSKIP("the nodes are Linux's");
#endif
    }

    void whoseKey() {
        controllers::KeySources sources;
        // libinput's and X11's codes are evdev's plus 8.
        sources.pressed("kbd-a", 30, 0);
        QCOMPARE(sources.takePress(38, 5), QString("kbd-a"));
        QCOMPARE(sources.takePress(38, 6), QString());   // taken
        QCOMPARE(sources.heldBy(38), QString("kbd-a"));  // a repeat, the release
        sources.released(38);
        QCOMPARE(sources.heldBy(38), QString());

        // Without the 8.
        sources.pressed("kbd-b", 30, 10);
        QCOMPARE(sources.takePress(30, 12), QString("kbd-b"));
        sources.released(30);

        // The latest press: an older one may never have reached the window.
        sources.pressed("kbd-a", 30, 100);
        sources.pressed("kbd-b", 30, 200);
        QCOMPARE(sources.takePress(38, 210), QString("kbd-b"));
        QCOMPARE(sources.takePress(38, 220), QString("kbd-a"));
        sources.released(38);

        // Too old to be the key in hand.
        sources.pressed("kbd-a", 31, 1000);
        QCOMPARE(sources.takePress(39, 3500), QString());

        // A device gone: nothing of it is held or waits.
        sources.pressed("kbd-a", 32, 4000);
        sources.pressed("kbd-a", 33, 4000);
        QCOMPARE(sources.takePress(40, 4001), QString("kbd-a"));
        sources.forget("kbd-a");
        QCOMPARE(sources.heldBy(40), QString());
        QCOMPARE(sources.takePress(41, 4002), QString());

        // However many never come to Qt, the queue stays short and the
        // newest is there.
        for (int i = 0; i < 200; ++i)
            sources.pressed("kbd-c", 50, 5000 + i);
        QCOMPARE(sources.takePress(58, 5300), QString("kbd-c"));
    }

    // ── InputManager with pads SDL makes up ──────────────────────────────

    void padOfItsOwn() {
        AppCore core(m_app.path(), m_data.path());
        auto im = std::make_unique<InputManager>(m_data.path(), &core);
        QSignalSpy keys(im.get(), &InputManager::mpvKeyRequested);
        VirtualPad pad;
        QVERIFY(pad.attach("OSD/OS Test Pad"));
        QTRY_VERIFY(entry(*im, "OSD/OS Test Pad").value("present").toBool());
        const QVariantMap listed = entry(*im, "OSD/OS Test Pad");
        const QString key = listed.value("key").toString();
        QVERIFY2(key.startsWith("pad-"), qPrintable(key));
        QCOMPARE(listed.value("kind").toString(), QString("gamepad"));
        QCOMPARE(listed.value("count").toInt(), 1);
        QVERIFY(listed.value("present").toBool());

        // The shared map, until it has one of its own.
        QCOMPARE(tap(pad, SDL_CONTROLLER_BUTTON_A, keys), QString("ENTER"));
        QCOMPARE(im->activeController(), key);
        QCOMPARE(tap(pad, SDL_CONTROLLER_BUTTON_B, keys), QString("ESC"));
        QCOMPARE(push(pad, SDL_CONTROLLER_AXIS_LEFTX, -30000, keys), QString("LEFT"));
        QCOMPARE(im->lastInputDevice(), QString("gamepad"));
        QCOMPARE(im->hints().value("select").toString(), QString("[A]"));
        QCOMPARE(im->hints().value("back").toString(), QString("[B]"));
        QCOMPARE(im->hints().value("play_pause").toString(), QString("[START]"));

        QVariantMap page = im->controllerPage(key);
        QCOMPARE(page.value("name").toString(), QString("OSD/OS Test Pad"));
        QCOMPARE(page.value("labels").toString(), QString("auto"));
        QCOMPARE(page.value("labelsInUse").toString(), QString("Xbox"));
        QCOMPARE(rowValue(page, "select"), QString("A"));
        QCOMPARE(rowValue(page, "back"), QString("B, BACK"));
        QCOMPARE(rowValue(page, "up"), QString("D-PAD ▲, L STICK ▲"));
        QCOMPARE(rowValue(page, "left"), QString("LB, D-PAD ◄, L STICK ◄"));
        QCOMPARE(rowValue(page, "play_pause"), QString("START"));

        // Its page waiting for a press: the press is reported, and does
        // nothing else; a stick's too.
        QSignalSpy captured(im.get(), &InputManager::controllerInputCaptured);
        im->captureController(key);
        QCOMPARE(tap(pad, SDL_CONTROLLER_BUTTON_B, keys), QString());
        QCOMPARE(captured.count(), 1);
        QCOMPARE(captured.at(0), QList<QVariant>({ key, QString("b"), QString("B") }));
        QCOMPARE(push(pad, SDL_CONTROLLER_AXIS_LEFTX, -30000, keys), QString());
        QCOMPARE(captured.count(), 2);
        QCOMPARE(captured.at(1), QList<QVariant>({ key, QString("leftx-"), QString("L STICK ◄") }));
        im->captureController(QString());
        QCOMPARE(tap(pad, SDL_CONTROLLER_BUTTON_B, keys), QString("ESC"));

        // Its own: X selects, A does nothing, the stick's left is Back.
        im->bindControllerInput(key, "x", "select");
        im->bindControllerInput(key, "a", "none");
        im->bindControllerInput(key, "leftx-", "back");
        QCOMPARE(tap(pad, SDL_CONTROLLER_BUTTON_X, keys), QString("ENTER"));
        QCOMPARE(tap(pad, SDL_CONTROLLER_BUTTON_A, keys), QString());
        QCOMPARE(push(pad, SDL_CONTROLLER_AXIS_LEFTX, -30000, keys), QString("ESC"));
        QCOMPARE(push(pad, SDL_CONTROLLER_AXIS_LEFTX, 30000, keys), QString("RIGHT"));
        page = im->controllerPage(key);
        QCOMPARE(rowValue(page, "select"), QString("X"));
        QCOMPARE(rowValue(page, "back"), QString("B, BACK, L STICK ◄"));
        QCOMPARE(rowValue(page, "left"), QString("LB, D-PAD ◄"));
        QCOMPARE(im->hints().value("select").toString(), QString("[X]"));

        // Its labels, here and in the hints.
        im->setControllerLabels(key, "playstation");
        QCOMPARE(im->controllerPage(key).value("labelsInUse").toString(), QString("PlayStation"));
        QCOMPARE(im->hints().value("select").toString(), QString("[SQ]"));
        QCOMPARE(im->hints().value("back").toString(), QString("[O]"));
        QCOMPARE(rowValue(im->controllerPage(key), "left"), QString("L1, D-PAD ◄"));
        im->setControllerLabels(key, "nintendo");
        QCOMPARE(im->hints().value("select").toString(), QString("[Y]"));
        QCOMPARE(im->hints().value("play_pause").toString(), QString("[+]"));

        // Kept in config.json, by its key.
        const QJsonObject saved = savedControllers().value(key).toObject();
        QCOMPARE(saved.value("name").toString(), QString("OSD/OS Test Pad"));
        QCOMPARE(saved.value("kind").toString(), QString("gamepad"));
        QCOMPARE(saved.value("labels").toString(), QString("nintendo"));
        QCOMPARE(saved.value("bindings").toObject(),
                 QJsonObject({ { "x", "select" }, { "a", "none" }, { "leftx-", "back" } }));

        // At the next start, the same; the pad away, its settings listed
        // under Not Connected, then gone with Reset to Defaults.
        im.reset();
        im = std::make_unique<InputManager>(m_data.path(), &core);
        QSignalSpy keys2(im.get(), &InputManager::mpvKeyRequested);
        VirtualPad again;
        QVERIFY(again.attach("OSD/OS Test Pad"));
        QTRY_VERIFY(entry(*im, "OSD/OS Test Pad").value("present").toBool());
        QCOMPARE(entry(*im, "OSD/OS Test Pad").value("key").toString(), key);
        QCOMPARE(tap(again, SDL_CONTROLLER_BUTTON_X, keys2), QString("ENTER"));
        QCOMPARE(tap(again, SDL_CONTROLLER_BUTTON_A, keys2), QString());

        again.detach();
        QTRY_VERIFY(!entry(*im, "OSD/OS Test Pad").value("present").toBool());
        QCOMPARE(entry(*im, "OSD/OS Test Pad").value("count").toInt(), 0);
        page = im->controllerPage(key);
        QVERIFY(!page.value("present").toBool());
        QCOMPARE(rowValue(page, "select"), QString("Y"));   // Nintendo's name for X
        im->resetController(key);
        QVERIFY(entry(*im, "OSD/OS Test Pad").isEmpty());
        QVERIFY(!savedControllers().contains(key));
    }

    // A pad's Arcade Buttons: Default, a preset, set by hand (one input, one
    // of them; never the joystick; Coin and Start never none), all ten at
    // once, what its player's games get by what the pad has, kept for the
    // next start, Default again, Reset to Defaults, and a pad away.
    void arcadeButtons() {
        AppCore core(m_app.path(), m_data.path());
        auto im = std::make_unique<InputManager>(m_data.path(), &core);
        VirtualPad pad;
        QVERIFY(pad.attach("OSD/OS Test Pad"));
        QTRY_VERIFY(entry(*im, "OSD/OS Test Pad").value("present").toBool());
        const QString key = entry(*im, "OSD/OS Test Pad").value("key").toString();
        const auto values = [&im, &key] {
            QStringList list;
            for (const QVariant &v : im->arcadePage(key).value("rows").toList())
                list << v.toMap().value("value").toString();
            return list;
        };
        const auto listed = [](const QVariantList &list, const char *field) {
            QStringList out;
            for (const QVariant &v : list)
                out << v.toMap().value(field).toString();
            return out;
        };

        // Default: MAME's and FinalBurn Neo's own, Back and Start their Coin
        // and Start; nothing of its for the games.
        QVariantMap page = im->arcadePage(key);
        QCOMPARE(page.value("layout").toString(), QString("default"));
        QCOMPARE(page.value("layoutTitle").toString(), QString("Default"));
        QVERIFY(page.value("present").toBool());
        QCOMPARE(im->controllerPage(key).value("arcade").toString(), QString("Default"));
        QCOMPARE(values(), QStringList({ "Auto", "Auto", "Auto", "Auto", "Auto", "Auto", "Auto", "Auto", "BACK", "START" }));
        QCOMPARE(listed(page.value("layouts").toList(), "title"),
                 QStringList({ "Default", "Fighting 6", "Neo Geo 4", "Custom" }));
        // Every input it has that can be one: all but a touchpad, which SDL
        // gives no pad it makes up.
        QStringList has = controllers::arcadeTokens();
        has.removeAll("touchpad");
        QCOMPARE(listed(page.value("choices").toList(), "token"), has);
        QVERIFY(controllers::playerArcade().value(0).isEmpty());
        QCOMPARE(im->arcadeLabel(key, "coin"), QString("BACK"));
        QCOMPARE(im->arcadeLabel("kbd-0003-046d-c31c", "coin"), QString());

        // Fighting 6: the punches over the kicks. Player 1's games get them
        // as MAME and RetroArch name them, by what the pad has: its
        // triggers are axes.
        im->setArcadeLayout(key, "fighting6");
        QCOMPARE(values(), QStringList({ "X", "Y", "RB", "A", "B", "RT", "LB", "LT", "BACK", "START" }));
        QCOMPARE(im->controllerPage(key).value("arcade").toString(), QString("Fighting 6"));
        const controllers::ArcadeBinds binds = controllers::playerArcade().value(0);
        QCOMPARE(binds.mame, QStringList({ "BUTTON3", "BUTTON4", "BUTTON6", "BUTTON1", "BUTTON2", "SLIDER2_NEG_SWITCH",
                                           "BUTTON5", "SLIDER1_NEG_SWITCH", "SELECT", "START" }));
        QCOMPARE(binds.retroarch, QStringList({ "2", "3", "10", "0", "1", "+5", "9", "+4", "4", "6" }));

        // Set by hand: Custom, an input another had swapped with it.
        QVariantMap result = im->setArcadeInput(key, "1", "a");
        QCOMPARE(outcome(result), QString("swapped"));
        QCOMPARE(result.value("label").toString(), QString("A"));
        QCOMPARE(result.value("other").toString(), QString("Button 4"));
        QCOMPARE(im->arcadePage(key).value("layout").toString(), QString("custom"));
        QCOMPARE(values().mid(0, 4), QStringList({ "A", "Y", "RB", "X" }));
        QCOMPARE(outcome(im->setArcadeInput(key, "1", "a")), QString("already"));
        // Never the joystick; Coin and Start never none, nor left none by a
        // swap.
        QCOMPARE(outcome(im->setArcadeInput(key, "2", "dpup")), QString("joystick"));
        QCOMPARE(outcome(im->setArcadeInput(key, "2", "leftx-")), QString("joystick"));
        QCOMPARE(outcome(im->setArcadeInput(key, "2", "guide")), QString("menu"));
        QCOMPARE(outcome(im->setArcadeInput(key, "8", "")), QString("set"));
        QCOMPARE(values().at(7), QString("None"));
        QVERIFY(im->setArcadeInput(key, "coin", "").isEmpty());
        QVERIFY(im->setArcadeInput(key, "9", "a").isEmpty());
        result = im->setArcadeInput(key, "8", "back");
        QCOMPARE(outcome(result), QString("kept"));
        QCOMPARE(result.value("other").toString(), QString("Coin"));
        // The right stick pushed can be one, Coin even.
        QCOMPARE(outcome(im->setArcadeInput(key, "coin", "rightx-")), QString("set"));
        QCOMPARE(im->arcadeLabel(key, "coin"), QString("R STICK ◄"));
        QCOMPARE(controllers::playerArcade().value(0).mame.value(controllers::kArcadeCoin), QString("ZAXIS_NEG_SWITCH"));
        QCOMPARE(controllers::playerArcade().value(0).retroarch.value(controllers::kArcadeCoin), QString("-2"));

        // All ten at once: different ones of its, Coin and Start among them.
        const QStringList all{ "a", "b", "x", "y", "leftshoulder", "rightshoulder", "", "", "back", "start" };
        QCOMPARE(im->setArcadeInputs(key, all), QString("set"));
        QCOMPARE(values(), QStringList({ "A", "B", "X", "Y", "LB", "RB", "None", "None", "BACK", "START" }));
        QCOMPARE(im->setArcadeInputs(key, { "a", "a", "", "", "", "", "", "", "back", "start" }), QString());
        QCOMPARE(im->setArcadeInputs(key, { "a", "b", "", "", "", "", "", "", "", "start" }), QString());
        QCOMPARE(im->setArcadeInputs(key, { "dpup", "b", "", "", "", "", "", "", "back", "start" }), QString());
        QCOMPARE(im->setArcadeInputs(key, all.mid(0, 9)), QString());
        QCOMPARE(values().at(0), QString("A"));

        // Kept in config.json, for the next start.
        const QJsonObject saved = savedControllers().value(key).toObject().value("arcade").toObject();
        QCOMPARE(saved.value("layout").toString(), QString("custom"));
        QCOMPARE(saved.value("inputs").toObject().value("5").toString(), QString("leftshoulder"));
        QCOMPARE(saved.value("inputs").toObject().value("7").toString(), QString());
        QCOMPARE(saved.value("inputs").toObject().value("coin").toString(), QString("back"));
        im.reset();
        im = std::make_unique<InputManager>(m_data.path(), &core);
        VirtualPad again;
        QVERIFY(again.attach("OSD/OS Test Pad"));
        QTRY_VERIFY(entry(*im, "OSD/OS Test Pad").value("present").toBool());
        QCOMPARE(values(), QStringList({ "A", "B", "X", "Y", "LB", "RB", "None", "None", "BACK", "START" }));
        QTRY_COMPARE(controllers::playerArcade().value(0).mame.value(4), QString("BUTTON5"));

        // Default again: nothing for the games, nothing kept; Reset to
        // Defaults forgets a layout too.
        im->setArcadeLayout(key, "default");
        QVERIFY(controllers::playerArcade().value(0).isEmpty());
        QVERIFY(!savedControllers().value(key).toObject().contains("arcade"));
        im->setArcadeLayout(key, "bogus");
        QCOMPARE(im->arcadePage(key).value("layout").toString(), QString("default"));
        im->setArcadeLayout(key, "neogeo4");
        QCOMPARE(values().mid(0, 4), QStringList({ "A", "B", "X", "Y" }));
        im->resetController(key);
        QCOMPARE(im->arcadePage(key).value("layout").toString(), QString("default"));
        QVERIFY(controllers::playerArcade().value(0).isEmpty());

        // Custom from Default starts from the pad's own; and a pad away is
        // set as one that has every input.
        im->setArcadeLayout(key, "custom");
        QCOMPARE(values(), QStringList({ "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "BACK", "START" }));
        im->resetController(key);
        again.detach();
        QTRY_VERIFY(entry(*im, "OSD/OS Test Pad").isEmpty());
        page = im->arcadePage(key);
        QVERIFY(!page.value("present").toBool());
        QCOMPARE(page.value("name").toString(), QString("OSD/OS Test Pad"));
        QCOMPARE(page.value("choices").toList().size(), controllers::arcadeTokens().size());
        QCOMPARE(outcome(im->setArcadeInput(key, "1", "paddle1")), QString("set"));
        QCOMPARE(values().at(0), QString("PADDLE1"));
        QVERIFY(!entry(*im, "OSD/OS Test Pad").value("present").toBool());
        QVERIFY(im->arcadePage("kbd-0003-046d-c31c").isEmpty());

        // Coin set to an input it turns out not to have (a touchpad): back,
        // its own Back is Coin, which Button 8 gives up.
        QCOMPARE(outcome(im->setArcadeInput(key, "coin", "touchpad")), QString("set"));
        QCOMPARE(outcome(im->setArcadeInput(key, "8", "back")), QString("set"));
        QCOMPARE(values().mid(7), QStringList({ "BACK", "TOUCHPAD", "START" }));
        VirtualPad third;
        QVERIFY(third.attach("OSD/OS Test Pad"));
        QTRY_VERIFY(entry(*im, "OSD/OS Test Pad").value("present").toBool());
        QCOMPARE(values().mid(7), QStringList({ "None", "BACK", "START" }));
        QCOMPARE(im->arcadeLabel(key, "coin"), QString("BACK"));
        QTRY_COMPARE(controllers::playerArcade().value(0).mame.value(controllers::kArcadeCoin), QString("SELECT"));
        QCOMPARE(controllers::playerArcade().value(0).mame.value(7), QString());
        QCOMPARE(controllers::playerArcade().value(0).retroarch.value(controllers::kArcadeCoin), QString("4"));
        QCOMPARE(controllers::playerArcade().value(0).mame.value(0), QString("BUTTON9"));

        // A line on an input the pad hasn't has none to give: Coin keeps its
        // Back, and a button that had the input pressed is left none.
        QCOMPARE(outcome(im->setArcadeInput(key, "7", "touchpad")), QString("set"));
        QCOMPARE(values().mid(6), QStringList({ "None", "None", "BACK", "START" }));
        result = im->setArcadeInput(key, "7", "back");
        QCOMPARE(outcome(result), QString("kept"));
        QCOMPARE(result.value("other").toString(), QString("Coin"));
        QCOMPARE(values().mid(6), QStringList({ "None", "None", "BACK", "START" }));
        result = im->setArcadeInput(key, "7", "b");
        QCOMPARE(outcome(result), QString("swapped"));
        QCOMPARE(result.value("other").toString(), QString("Button 2"));
        QCOMPARE(values().mid(0, 3), QStringList({ "PADDLE1", "None", "X" }));
        QCOMPARE(values().at(6), QString("B"));
        const QJsonObject inputs = savedControllers().value(key).toObject().value("arcade").toObject().value("inputs").toObject();
        QCOMPARE(inputs.value("2").toString(), QString());
        QCOMPARE(inputs.value("7").toString(), QString("b"));
    }

    // input.cfg is every pad's map, a pad's own over it.
    void inputCfgUnderneath() {
        QVERIFY(writeData("input.cfg", "a back\nb select\nlabel start GO\n"));
        AppCore core(m_app.path(), m_data.path());
        InputManager im(m_data.path(), &core);
        QSignalSpy keys(&im, &InputManager::mpvKeyRequested);
        VirtualPad pad;
        QVERIFY(pad.attach("OSD/OS Test Pad"));
        QTRY_VERIFY(entry(im, "OSD/OS Test Pad").value("present").toBool());
        const QString key = entry(im, "OSD/OS Test Pad").value("key").toString();
        QCOMPARE(tap(pad, SDL_CONTROLLER_BUTTON_A, keys), QString("ESC"));
        QCOMPARE(tap(pad, SDL_CONTROLLER_BUTTON_B, keys), QString("ENTER"));
        QCOMPARE(im.hints().value("play_pause").toString(), QString("[GO]"));
        QCOMPARE(rowValue(im.controllerPage(key), "play_pause"), QString("GO"));

        im.bindControllerInput(key, "a", "select");
        QCOMPARE(tap(pad, SDL_CONTROLLER_BUTTON_A, keys), QString("ENTER"));
        QCOMPARE(rowValue(im.controllerPage(key), "select"), QString("A, B"));
        im.resetController(key);
        QCOMPARE(tap(pad, SDL_CONTROLLER_BUTTON_A, keys), QString("ESC"));

        // input.cfg saved anew, as an editor replaces it: the pads' pages are
        // told, to show it.
        QSignalSpy changed(&im, &InputManager::controllersChanged);
        QSaveFile cfg(m_data.path() + "/input.cfg");
        QVERIFY(cfg.open(QIODevice::WriteOnly));
        cfg.write("x select\n");
        QVERIFY(cfg.commit());
        QTRY_VERIFY(changed.count() > 0);
        QCOMPARE(rowValue(im.controllerPage(key), "select"), QString("A, X"));
    }

    // An input let go ends what its press began, whatever it does by then,
    // and a pad that goes lets go of what it held.
    void letGoAsPressed() {
        AppCore core(m_app.path(), m_data.path());
        InputManager im(m_data.path(), &core);
        QSignalSpy keys(&im, &InputManager::mpvKeyRequested);
        VirtualPad pad, other;
        QVERIFY(pad.attach("OSD/OS Test Pad"));
        QVERIFY(other.attach("OSD/OS Other Pad", 0x1209, 0x0003));
        QTRY_VERIFY(entry(im, "OSD/OS Test Pad").value("present").toBool());
        QTRY_VERIFY(entry(im, "OSD/OS Other Pad").value("present").toBool());
        const QString key = entry(im, "OSD/OS Test Pad").value("key").toString();

        // X selects, and while it is held Reset to Defaults makes it nothing:
        // let go, Select is let go too, and A's next press selects.
        QCOMPARE(outcome(im.bindControllerInput(key, "x", "select")), QString("set"));
        keys.clear();
        pad.button(SDL_CONTROLLER_BUTTON_X, true);
        QVERIFY(keys.wait(1000));
        QCOMPARE(keys.first().at(0).toString(), QString("ENTER"));
        im.resetController(key);
        release(pad, SDL_CONTROLLER_BUTTON_X);
        QCOMPARE(tap(pad, SDL_CONTROLLER_BUTTON_A, keys), QString("ENTER"));

        // A stick's direction bound anew while it is held: let go, it stops
        // repeating.
        keys.clear();
        pad.axis(SDL_CONTROLLER_AXIS_LEFTY, 30000);
        QVERIFY(keys.wait(1000));
        QCOMPARE(keys.first().at(0).toString(), QString("DOWN"));
        QCOMPARE(outcome(im.bindControllerInput(key, "lefty+", "up")), QString("set"));
        centre(pad, SDL_CONTROLLER_AXIS_LEFTY);
        keys.clear();
        QTest::qWait(700);   // past the repeat's first 400 ms
        QCOMPARE(keysOf(keys), QStringList());
        QCOMPARE(push(pad, SDL_CONTROLLER_AXIS_LEFTY, 30000, keys), QString("UP"));

        // Held while its page waits for a press: let go, it ends too.
        keys.clear();
        pad.button(SDL_CONTROLLER_BUTTON_DPAD_DOWN, true);
        QVERIFY(keys.wait(1000));
        im.captureController(key);
        release(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        im.captureController(QString());
        keys.clear();
        QTest::qWait(700);
        QCOMPARE(keysOf(keys), QStringList());

        // Another pad going doesn't let go of what this one holds; this one
        // going does.
        keys.clear();
        pad.button(SDL_CONTROLLER_BUTTON_DPAD_DOWN, true);
        QVERIFY(keys.wait(1000));
        other.detach();
        QTRY_VERIFY(!entry(im, "OSD/OS Other Pad").value("present").toBool());
        keys.clear();
        QTRY_VERIFY(keys.count() > 0);   // still held, so repeating
        pad.detach();
        QTRY_VERIFY(!entry(im, "OSD/OS Test Pad").value("present").toBool());
        keys.clear();
        QTest::qWait(700);
        QCOMPARE(keysOf(keys), QStringList());
    }

    // No press on a page takes the menus out of a controller's reach.
    void aWayAround() {
        AppCore core(m_app.path(), m_data.path());
        InputManager im(m_data.path(), &core);
        QSignalSpy keys(&im, &InputManager::mpvKeyRequested);
        VirtualPad pad;
        QVERIFY(pad.attach("OSD/OS Test Pad"));
        QTRY_VERIFY(entry(im, "OSD/OS Test Pad").value("present").toBool());
        const QString pkey = entry(im, "OSD/OS Test Pad").value("key").toString();

        // A pad's only Select / OK stays, pressed again to start the wait, say.
        QVariantMap result = im.bindControllerInput(pkey, "a", "up");
        QCOMPARE(outcome(result), QString("only"));
        QCOMPARE(result.value("action").toString(), QString("select"));
        QCOMPARE(result.value("label").toString(), QString("Select / OK"));
        QCOMPARE(outcome(im.bindControllerInput(pkey, "a", "none")), QString("only"));
        QCOMPARE(tap(pad, SDL_CONTROLLER_BUTTON_A, keys), QString("ENTER"));
        // With another, it can go.
        QCOMPARE(outcome(im.bindControllerInput(pkey, "x", "select")), QString("set"));
        QCOMPARE(outcome(im.bindControllerInput(pkey, "a", "up")), QString("set"));
        QCOMPARE(tap(pad, SDL_CONTROLLER_BUTTON_A, keys), QString("UP"));
        // Back: B and BACK do it, so one can go and the other then stays.
        QCOMPARE(outcome(im.bindControllerInput(pkey, "b", "none")), QString("set"));
        QCOMPARE(outcome(im.bindControllerInput(pkey, "back", "play_pause")), QString("only"));
        // Down too: the d-pad's can go, the stick's then stays.
        QCOMPARE(outcome(im.bindControllerInput(pkey, "dpdown", "none")), QString("set"));
        result = im.bindControllerInput(pkey, "lefty+", "play_pause");
        QCOMPARE(outcome(result), QString("only"));
        QCOMPARE(result.value("label").toString(), QString("Down"));
        // What it does already.
        QCOMPARE(outcome(im.bindControllerInput(pkey, "x", "select")), QString("already"));
        result = im.bindControllerInput(pkey, "b", "none");
        QCOMPARE(outcome(result), QString("already"));
        QCOMPARE(result.value("action").toString(), QString("none"));

        // A keyboard's: Back ends the wait; Select / OK's and the menus' keys
        // keep what they do; any other is set.
        const QString kbd = QStringLiteral("kbd-0005-046d-b342");
        const auto key = [](int qtKey) { return QString::number(qtKey); };
        QCOMPARE(outcome(im.keyCapture(kbd, Qt::Key_Escape, "up")), QString("cancel"));
        QCOMPARE(outcome(im.keyCapture(kbd, Qt::Key_Backspace, "none")), QString("cancel"));
        result = im.keyCapture(kbd, Qt::Key_Return, "back");
        QCOMPARE(outcome(result), QString("already"));
        QCOMPARE(result.value("label").toString(), QString("Select / OK"));
        QCOMPARE(outcome(im.keyCapture(kbd, Qt::Key_Enter, "back")), QString("already"));
        QCOMPARE(outcome(im.keyCapture(kbd, Qt::Key_Down, "up")), QString("already"));
        QCOMPARE(im.keyCapture(kbd, Qt::Key_Space, "select").value("action").toString(), QString("play_pause"));
        QCOMPARE(outcome(im.keyCapture(kbd, Qt::Key_O, "select")), QString("set"));

        // Bound on every keyboard: its Back cancels, its Select / OK, Up and
        // Down stay, any other goes to the action, as on All Keyboards &
        // Remotes.
        const int ccBack = 0x02000000 + 158;      // a Consumer Control Back
        const int leftClick = 0x03000000 + 1;
        core.save_setting(QString(), "remote_keymap.back", ccBack);
        core.save_setting(QString(), "remote_keymap.select", leftClick);
        core.save_setting(QString(), "remote_keymap.down", int(Qt::Key_F7));
        core.save_setting(QString(), "remote_keymap.left", int(Qt::Key_F5));
        QCOMPARE(outcome(im.keyCapture(QString(), ccBack, "up")), QString("cancel"));
        QCOMPARE(outcome(im.keyCapture(QString(), leftClick, "back")), QString("already"));
        result = im.keyCapture(QString(), Qt::Key_F7, "up");
        QCOMPARE(outcome(result), QString("already"));
        QCOMPARE(result.value("label").toString(), QString("Down"));
        QCOMPARE(outcome(im.keyCapture(QString(), Qt::Key_F5, "right")), QString("set"));
        QCOMPARE(outcome(im.keyCapture(QString(), Qt::Key_F5, "left")), QString("already"));
        QCOMPARE(outcome(im.keyCapture(kbd, ccBack, "up")), QString("cancel"));

        // Its own: set, then kept as its Select / OK or Up; moved; taken away.
        QCOMPARE(outcome(im.bindControllerInput(kbd, key(Qt::Key_O), "select")), QString("set"));
        QCOMPARE(outcome(im.bindControllerInput(kbd, key(Qt::Key_O), "up")), QString("already"));
        QCOMPARE(outcome(im.bindControllerInput(kbd, key(Qt::Key_O), "none")), QString("already"));
        QCOMPARE(outcome(im.bindControllerInput(kbd, key(Qt::Key_Q), "up")), QString("set"));
        QCOMPARE(outcome(im.bindControllerInput(kbd, key(Qt::Key_Q), "down")), QString("already"));
        QCOMPARE(outcome(im.bindControllerInput(kbd, key(Qt::Key_P), "left")), QString("set"));
        QCOMPARE(outcome(im.bindControllerInput(kbd, key(Qt::Key_P), "right")), QString("set"));
        QCOMPARE(outcome(im.bindControllerInput(kbd, key(Qt::Key_F6), "none")), QString("other"));
        QCOMPARE(outcome(im.bindControllerInput(kbd, key(Qt::Key_Return), "up")), QString("already"));
        QCOMPARE(outcome(im.bindControllerInput(kbd, key(Qt::Key_Escape), "none")), QString("cancel"));
        QCOMPARE(savedControllers().value(kbd).toObject().value("bindings").toObject(),
                 QJsonObject({ { key(Qt::Key_O), "select" }, { key(Qt::Key_Q), "up" }, { key(Qt::Key_P), "right" } }));
        QCOMPARE(outcome(im.bindControllerInput(kbd, key(Qt::Key_P), "none")), QString("set"));
        QCOMPARE(savedControllers().value(kbd).toObject().value("bindings").toObject(),
                 QJsonObject({ { key(Qt::Key_O), "select" }, { key(Qt::Key_Q), "up" } }));
    }

    // A key a wait for a press took stays swallowed until it is let go:
    // held on, its repeats don't act as what it has just been given.
    void keyHeldThroughAWait() {
        AppCore core(m_app.path(), m_data.path());
        InputManager im(m_data.path(), &core);
        QSignalSpy keys(&im, &InputManager::mpvKeyRequested);
        QSignalSpy captured(&im, &InputManager::auxButtonPressed);
        QWindow window;
        const auto send = [&window](QEvent::Type type, bool repeat) {
            QKeyEvent event(type, Qt::Key_P, Qt::NoModifier, QStringLiteral("p"), repeat);
            QCoreApplication::sendEvent(&window, &event);
        };

        // All Keyboards & Remotes waits; P is pressed and saved for Down.
        im.setRemapCapture(true);
        send(QEvent::KeyPress, false);
        QCOMPARE(captured.count(), 1);
        QCOMPARE(captured.at(0).at(0).toInt(), int(Qt::Key_P));
        im.setRemapCapture(false);
        core.save_setting(QString(), "remote_keymap.down", int(Qt::Key_P));
        send(QEvent::KeyPress, true);
        send(QEvent::KeyPress, true);
        QCOMPARE(keysOf(keys), QStringList());
        send(QEvent::KeyRelease, false);
        QCOMPARE(keysOf(keys), QStringList());
        // Pressed anew, it is Down.
        send(QEvent::KeyPress, false);
        send(QEvent::KeyRelease, false);
        QCOMPARE(keysOf(keys), QStringList({ "DOWN" }));
    }

    // A held volume key, which the keyboard repeats every 80 ms or so, goes
    // to mpv every 150 ms (the whole range in three seconds); a press goes at
    // once, and any other media key once per press, however long it is held.
    void heldVolumeKey() {
        AppCore core(m_app.path(), m_data.path());
        InputManager im(m_data.path(), &core);
        QSignalSpy keys(&im, &InputManager::mpvKeyRequested);
        QWindow window;
        const auto press = [&window](int qtKey, bool repeat) {
            QKeyEvent event(QEvent::KeyPress, qtKey, Qt::NoModifier, QString(), repeat);
            QCoreApplication::sendEvent(&window, &event);
        };
        press(Qt::Key_VolumeUp, false);
        for (int i = 0; i < 5; ++i)
            press(Qt::Key_VolumeUp, true);
        QCOMPARE(keysOf(keys), QStringList({ "VOLUME_UP" }));
        QTest::qWait(170);
        press(Qt::Key_VolumeUp, true);
        press(Qt::Key_VolumeUp, true);
        QCOMPARE(keysOf(keys), QStringList({ "VOLUME_UP", "VOLUME_UP" }));
        // Let go and pressed again, at once.
        press(Qt::Key_VolumeDown, false);
        QCOMPARE(keysOf(keys), QStringList({ "VOLUME_UP", "VOLUME_UP", "VOLUME_DOWN" }));
        press(Qt::Key_MediaNext, false);
        press(Qt::Key_MediaNext, true);
        QTest::qWait(170);
        press(Qt::Key_MediaNext, true);
        QCOMPARE(keysOf(keys).count("NEXT"), 1);
    }

    // A remote's OK, however it comes, is Select / OK: keypad Enter, which
    // most views don't take, and the Select and OK keys (Qt::Key_Select from
    // Qt's own evdev keymap; from libinput's, which has no symbol for them, a
    // key of 0 known by its scan code), once per press. A mouse's left and
    // right buttons are Select / OK and Back, as an air-mouse remote's OK is a
    // left click while its pointer is on, and its wheel Up and Down a notch at
    // a time; once a click is bound to something else, it does what it is
    // bound to.
    void remoteOk() {
        AppCore core(m_app.path(), m_data.path());
        InputManager im(m_data.path(), &core);
        QSignalSpy keys(&im, &InputManager::mpvKeyRequested);
        QWindow window;
        // Pressed, held on as Qt repeats it, and let go.
        const auto key = [&window](int qtKey, quint32 scanCode = 0, bool held = false) {
            QKeyEvent press(QEvent::KeyPress, qtKey, Qt::NoModifier, scanCode, 0, 0);
            QCoreApplication::sendEvent(&window, &press);
            if (held) {
                QKeyEvent repeat(QEvent::KeyPress, qtKey, Qt::NoModifier, scanCode, 0, 0, QString(), true);
                QCoreApplication::sendEvent(&window, &repeat);
            }
            QKeyEvent release(QEvent::KeyRelease, qtKey, Qt::NoModifier, scanCode, 0, 0);
            QCoreApplication::sendEvent(&window, &release);
        };
        const auto click = [&window](Qt::MouseButton button) {
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(1, 1), QPointF(1, 1), button, button, Qt::NoModifier);
            QCoreApplication::sendEvent(&window, &press);
            QMouseEvent release(QEvent::MouseButtonRelease, QPointF(1, 1), QPointF(1, 1), button, Qt::NoButton,
                                Qt::NoModifier);
            QCoreApplication::sendEvent(&window, &release);
        };
        const auto wheel = [&window](int turn) {
            QWheelEvent notch(QPointF(1, 1), QPointF(1, 1), QPoint(), QPoint(0, turn), Qt::NoButton, Qt::NoModifier,
                              Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(&window, &notch);
        };
        const int leftClick = 0x03000000 + int(Qt::LeftButton);
        const int rightClick = 0x03000000 + int(Qt::RightButton);

        QStringList oks;
        key(Qt::Key_Enter, 0, true);
        key(Qt::Key_Select, 0, true);
        oks << "ENTER" << "ENTER";
#ifdef Q_OS_LINUX
        key(0, KEY_OK + 8, true);
        key(0, KEY_SELECT + 8);
        oks << "ENTER" << "ENTER";
        // Another key without a symbol is nothing.
        key(0, KEY_VENDOR + 8);
#endif
        QCOMPARE(keysOf(keys), oks);
        // Return goes on to the views as it is.
        keys.clear();
        key(Qt::Key_Return);
        QCOMPARE(keysOf(keys), QStringList());
        // The Select key keeps what it does, as Enter does.
        QCOMPARE(outcome(im.keyCapture(QString(), Qt::Key_Select, "back")), QString("already"));
#ifdef Q_OS_LINUX
        // A wait for a press takes libinput's OK as a remote's OK button, which
        // keeps what it does too.
        QSignalSpy captured(&im, &InputManager::auxButtonPressed);
        im.setRemapCapture(true);
        key(0, KEY_OK + 8);
        im.setRemapCapture(false);
        QCOMPARE(captured.count(), 1);
        const int ok = captured.at(0).at(0).toInt();
        QCOMPARE(ok, 0x02000000 + KEY_OK);
        QCOMPARE(im.keyDisplayName(ok), QString("OK"));
        QCOMPARE(outcome(im.keyCapture(QString(), ok, "up")), QString("already"));
        QCOMPARE(keysOf(keys), QStringList());
#endif

        // A mouse: left and right are Select / OK and Back, the middle button
        // nothing.
        click(Qt::LeftButton);
        click(Qt::RightButton);
        click(Qt::MiddleButton);
        QCOMPARE(keysOf(keys), QStringList({ "ENTER", "ESC" }));
        // A wait for a press keeps them: the right one ends it, as Back does.
        QCOMPARE(outcome(im.keyCapture(QString(), leftClick, "back")), QString("already"));
        QCOMPARE(outcome(im.keyCapture(QString(), rightClick, "up")), QString("cancel"));
        // The wheel: a notch down is Down, a notch up Up, however it comes in
        // (a wheel that turns in half notches makes one of every two); never
        // while a press is waited for.
        keys.clear();
        wheel(-120);
        wheel(60);
        wheel(60);
        im.setRemapCapture(true);
        wheel(120);
        im.setRemapCapture(false);
        QCOMPARE(keysOf(keys), QStringList({ "DOWN", "UP" }));
        // Bound to something else, the click does that.
        core.save_setting(QString(), "remote_keymap.down", leftClick);
        keys.clear();
        click(Qt::LeftButton);
        QCOMPARE(keysOf(keys), QStringList({ "DOWN" }));
    }

#ifdef Q_OS_LINUX
    // A remote's Consumer Control button that no action takes goes to the
    // views as the key it is where they act on one (Channel + / −, Page Up
    // and Down), once per press however long it is held, with its release;
    // Qt's own copy of the press, where its keymap has one (a key of 0 known
    // by its scan code), goes no further. A button the views don't know does
    // nothing, and one bound to an action acts as before.
    void remoteChannelKeys() {
        AppCore core(m_app.path(), m_data.path());
        InputManager im(m_data.path(), &core);
        QSignalSpy keys(&im, &InputManager::mpvKeyRequested);
        QQuickWindow window;
        im.setTargetWindow(&window);
        struct KeyLog : QObject {
            QList<QPair<int, bool>> keys;   // the key, pressed or let go
            bool eventFilter(QObject *, QEvent *event) override {
                if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)
                    keys << qMakePair(static_cast<QKeyEvent *>(event)->key(), event->type() == QEvent::KeyPress);
                return false;
            }
        } log;
        window.installEventFilter(&log);
        // The node: a pipe written as the kernel writes a remote's node.
        int fds[2];
        QVERIFY(::pipe2(fds, O_NONBLOCK | O_CLOEXEC) == 0);
        im.adoptNodeForTest(fds[0], "Remote Consumer Control");
        const auto button = [&fds](int code, int value) {
            struct input_event ev = {};
            ev.type = EV_KEY;
            ev.code = code;
            ev.value = value;
            return ::write(fds[1], &ev, sizeof(ev)) == ssize_t(sizeof(ev));
        };

        QVERIFY(button(KEY_CHANNELUP, 1));
        QTRY_COMPARE(log.keys.size(), 1);
        QCOMPARE(log.keys.at(0), qMakePair(int(Qt::Key_ChannelUp), true));
        // Qt's copy of the press, by its scan code (the code plus 8).
        QKeyEvent copy(QEvent::KeyPress, 0, Qt::NoModifier, KEY_CHANNELUP + 8, 0, 0);
        QCoreApplication::sendEvent(&window, &copy);
        QCOMPARE(log.keys.size(), 1);
        QVERIFY(button(KEY_CHANNELUP, 0));
        QTRY_COMPARE(log.keys.size(), 2);
        QCOMPARE(log.keys.at(1), qMakePair(int(Qt::Key_ChannelUp), false));

        // Held: the kernel's repeats are no presses.
        QVERIFY(button(KEY_PAGEDOWN, 1));
        QVERIFY(button(KEY_PAGEDOWN, 2));
        QVERIFY(button(KEY_PAGEDOWN, 2));
        QVERIFY(button(KEY_PAGEDOWN, 0));
        QTRY_COMPARE(log.keys.size(), 4);
        QCOMPARE(log.keys.at(2), qMakePair(int(Qt::Key_PageDown), true));
        QCOMPARE(log.keys.at(3), qMakePair(int(Qt::Key_PageDown), false));

        // A button the views don't know, then one bound to Down: the first
        // does nothing, the second is Down (to mpv, as the window isn't
        // active here), not Channel −.
        QVERIFY(button(KEY_RED, 1));
        QVERIFY(button(KEY_RED, 0));
        core.save_setting(QString(), "remote_keymap.down", 0x02000000 + KEY_CHANNELDOWN);
        QVERIFY(button(KEY_CHANNELDOWN, 1));
        QTRY_COMPARE(keysOf(keys), QStringList({ "DOWN" }));
        QVERIFY(button(KEY_CHANNELDOWN, 0));
        QTest::qWait(50);
        QCOMPARE(log.keys.size(), 4);

        // The remote goes (its node's end) with a button held: let go.
        QVERIFY(button(KEY_PAGEUP, 1));
        QTRY_COMPARE(log.keys.size(), 5);
        ::close(fds[1]);
        QTRY_COMPARE(log.keys.size(), 6);
        QCOMPARE(log.keys.at(5), qMakePair(int(Qt::Key_PageUp), false));
        QTRY_VERIFY(entry(im, "Remote Consumer Control").value("present").toBool() == false);
    }

    // The same keys from a remote's keyboard node reach Qt itself, as a key
    // of 0 known by its scan code (libinput's keymap has no symbol for the
    // channel keys): delivered as the keys they are, held as held, and let
    // go; any other unknown key goes on as it is.
    void remoteChannelKeysFromAKeyboard() {
        AppCore core(m_app.path(), m_data.path());
        InputManager im(m_data.path(), &core);
        QQuickWindow window;
        im.setTargetWindow(&window);
        struct KeyLog : QObject {
            QList<QPair<int, bool>> keys;
            bool eventFilter(QObject *, QEvent *event) override {
                if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)
                    keys << qMakePair(static_cast<QKeyEvent *>(event)->key(), event->type() == QEvent::KeyPress);
                return false;
            }
        } log;
        window.installEventFilter(&log);
        const auto key = [&window](QEvent::Type type, int code, bool repeat = false) {
            QKeyEvent event(type, 0, Qt::NoModifier, code + 8, 0, 0, QString(), repeat);
            QCoreApplication::sendEvent(&window, &event);
        };
        key(QEvent::KeyPress, KEY_CHANNELUP);
        key(QEvent::KeyPress, KEY_CHANNELUP, true);
        key(QEvent::KeyRelease, KEY_CHANNELUP);
        key(QEvent::KeyPress, KEY_CHANNELDOWN);
        key(QEvent::KeyRelease, KEY_CHANNELDOWN);
        QTRY_COMPARE(log.keys.size(), 5);
        QCOMPARE(log.keys.at(0), qMakePair(int(Qt::Key_ChannelUp), true));
        QCOMPARE(log.keys.at(1), qMakePair(int(Qt::Key_ChannelUp), true));
        QCOMPARE(log.keys.at(2), qMakePair(int(Qt::Key_ChannelUp), false));
        QCOMPARE(log.keys.at(3), qMakePair(int(Qt::Key_ChannelDown), true));
        QCOMPARE(log.keys.at(4), qMakePair(int(Qt::Key_ChannelDown), false));
        // A key of 0 the views don't know goes on as it is.
        key(QEvent::KeyPress, KEY_RED);
        QTest::qWait(20);
        QCOMPARE(log.keys.size(), 6);
        QCOMPARE(log.keys.at(5), qMakePair(0, true));
    }
#endif

    // A key that stands for an action is let go as the action's key too, as
    // the Arcade screen plays its game once select is let go: keypad Enter as
    // Return, a key bound to Down as Down, Right Shift as Escape, and on the
    // console a left click as Return.
    void standInLetGo() {
        AppCore core(m_app.path(), m_data.path());
        InputManager im(m_data.path(), &core);
        QQuickWindow window;
        window.resize(64, 48);
        window.show();
        window.requestActivate();
        QTRY_VERIFY(window.isActive());
        im.setTargetWindow(&window);
        core.save_setting(QString(), "remote_keymap.down", int(Qt::Key_F7));
        // The keys as the window gets them, pressed and let go.
        struct Catcher : QObject {
            QList<int> pressed, released;
            bool eventFilter(QObject *, QEvent *event) override {
                if (event->type() == QEvent::KeyPress)
                    pressed << static_cast<QKeyEvent *>(event)->key();
                else if (event->type() == QEvent::KeyRelease)
                    released << static_cast<QKeyEvent *>(event)->key();
                return false;
            }
        } catcher;
        window.installEventFilter(&catcher);
        // What came of an input pressed and let go: the keys pressed, then
        // those let go.
        const auto came = [&catcher](const std::function<void()> &input) {
            catcher.pressed.clear();
            catcher.released.clear();
            input();
            // Without a release, the comparison says so.
            (void)QTest::qWaitFor([&catcher] { return !catcher.released.isEmpty(); }, 1000);
            return QList<QList<int>>{ catcher.pressed, catcher.released };
        };
        const auto key = [&window](int qtKey, quint32 scanCode = 0) {
            for (const QEvent::Type type : { QEvent::KeyPress, QEvent::KeyRelease }) {
                QKeyEvent event(type, qtKey, Qt::NoModifier, scanCode, 0, 0);
                QCoreApplication::sendEvent(&window, &event);
            }
        };
        const QList<QList<int>> enter{ { Qt::Key_Return }, { Qt::Key_Return } };

        QCOMPARE(came([&] { key(Qt::Key_Enter); }), enter);
        QCOMPARE(came([&] { key(Qt::Key_F7); }), (QList<QList<int>>{ { Qt::Key_Down }, { Qt::Key_Down } }));
#ifdef Q_OS_LINUX
        QCOMPARE(came([&] { key(Qt::Key_Shift, KEY_RIGHTSHIFT + 8); }),
                 (QList<QList<int>>{ { Qt::Key_Escape }, { Qt::Key_Escape } }));
#endif
        QCOMPARE(came([&] {
                     QMouseEvent press(QEvent::MouseButtonPress, QPointF(1, 1), QPointF(1, 1), Qt::LeftButton,
                                       Qt::LeftButton, Qt::NoModifier);
                     QCoreApplication::sendEvent(&window, &press);
                     QMouseEvent release(QEvent::MouseButtonRelease, QPointF(1, 1), QPointF(1, 1),
                                         Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                     QCoreApplication::sendEvent(&window, &release);
                 }),
                 enter);
        window.removeEventFilter(&catcher);
        im.setTargetWindow(nullptr);
    }

    // The same model twice is one line, and one set of settings for both.
    void oneModelTwice() {
        AppCore core(m_app.path(), m_data.path());
        InputManager im(m_data.path(), &core);
        QSignalSpy keys(&im, &InputManager::mpvKeyRequested);
        VirtualPad first, second, other;
        QVERIFY(first.attach("OSD/OS Twin Pad", 0x1209, 0x0002));
        QVERIFY(second.attach("OSD/OS Twin Pad", 0x1209, 0x0002));
        QVERIFY(other.attach("OSD/OS Other Pad", 0x1209, 0x0003));
        QTRY_COMPARE(entry(im, "OSD/OS Twin Pad").value("count").toInt(), 2);
        QTRY_VERIFY(entry(im, "OSD/OS Other Pad").value("present").toBool());
        const QString key = entry(im, "OSD/OS Twin Pad").value("key").toString();
        QVERIFY(key != entry(im, "OSD/OS Other Pad").value("key").toString());

        im.bindControllerInput(key, "y", "play_pause");
        QCOMPARE(tap(first, SDL_CONTROLLER_BUTTON_Y, keys), QString("SPACE"));
        QCOMPARE(tap(second, SDL_CONTROLLER_BUTTON_Y, keys), QString("SPACE"));
        QCOMPARE(tap(other, SDL_CONTROLLER_BUTTON_Y, keys), QString());

        // Its page waiting: only its presses are taken; the other pad's act.
        QSignalSpy captured(&im, &InputManager::controllerInputCaptured);
        im.captureController(key);
        QCOMPARE(tap(other, SDL_CONTROLLER_BUTTON_B, keys), QString("ESC"));
        QCOMPARE(tap(second, SDL_CONTROLLER_BUTTON_A, keys), QString());
        QCOMPARE(captured.count(), 1);
        im.captureController(QString());
    }

    // Settings → Controls → Players: which pad each player has.
    void players() {
        AppCore core(m_app.path(), m_data.path());
        InputManager im(m_data.path(), &core);
        VirtualPad stick2, padA, stick1, padB;
        QVERIFY(stick2.attach("OSD/OS JAMMA P2", 0, 0));
        QVERIFY(padA.attach("OSD/OS Pad A", 0x1209, 0x0010));
        QVERIFY(stick1.attach("OSD/OS JAMMA P1", 0, 0));
        QVERIFY(padB.attach("OSD/OS Pad B", 0x1209, 0x0011));
        for (const char *name : { "OSD/OS JAMMA P2", "OSD/OS Pad A", "OSD/OS JAMMA P1", "OSD/OS Pad B" })
            QTRY_VERIFY(entry(im, name).value("present").toBool());
        const QString keyA = entry(im, "OSD/OS Pad A").value("key").toString();
        const QString keyB = entry(im, "OSD/OS Pad B").value("key").toString();

        // Auto: the JAMMA board's sticks by their number, whichever came
        // first. SDL's player index: the players', then the rest in order.
        QCOMPARE(player(im, 1).value("key").toString(), QString());
        QCOMPARE(player(im, 1).value("name").toString(), QString("OSD/OS JAMMA P1"));
        QCOMPARE(player(im, 2).value("name").toString(), QString("OSD/OS JAMMA P2"));
        QCOMPARE(SDL_JoystickGetPlayerIndex(stick1.joystick), 0);
        QCOMPARE(SDL_JoystickGetPlayerIndex(stick2.joystick), 1);
        QCOMPARE(SDL_JoystickGetPlayerIndex(padA.joystick), 2);
        QCOMPARE(SDL_JoystickGetPlayerIndex(padB.joystick), 3);

        // Chosen: Pad A for player 1. Auto's player 2 keeps its own stick.
        QSignalSpy changed(&im, &InputManager::playersChanged);
        im.setPlayer(1, keyA);
        QVERIFY(changed.count() > 0);
        QCOMPARE(player(im, 1).value("key").toString(), keyA);
        QCOMPARE(player(im, 1).value("name").toString(), QString("OSD/OS Pad A"));
        QVERIFY(player(im, 1).value("connected").toBool());
        QCOMPARE(player(im, 2).value("name").toString(), QString("OSD/OS JAMMA P2"));
        QCOMPARE(SDL_JoystickGetPlayerIndex(padA.joystick), 0);
        QCOMPARE(SDL_JoystickGetPlayerIndex(stick2.joystick), 1);
        QCOMPARE(SDL_JoystickGetPlayerIndex(stick1.joystick), 2);

        // One pad, one player: Pad A for player 2 takes it from player 1,
        // which goes back to Auto.
        im.setPlayer(2, keyA);
        QCOMPARE(player(im, 2).value("key").toString(), keyA);
        QCOMPARE(player(im, 1).value("key").toString(), QString());
        QCOMPARE(player(im, 1).value("name").toString(), QString("OSD/OS JAMMA P1"));

        // A program OSD/OS starts has them, and nothing inherited.
        QProcessEnvironment env;
        env.insert("OSDOS_PLAYER1_NAME", "inherited");
        env.insert("OSDOS_PLAYER2_DEVICE", "inherited");
        controllers::applyPlayersTo(env);
        QCOMPARE(env.value("OSDOS_PLAYER1_NAME"), QString("OSD/OS JAMMA P1"));
        QCOMPARE(env.value("OSDOS_PLAYER2_NAME"), QString("OSD/OS Pad A"));
        QCOMPARE(controllers::padKey(env.value("OSDOS_PLAYER2_GUID")), keyA);
        // Every pad's mapping as SDL has it, its GUID's CRC zeroed, for the
        // emulators' SDL.
        const QStringList mappings = controllers::padMappings();
        QCOMPARE(mappings.size(), 4);
        for (const QString &mapping : mappings) {
            QCOMPARE(mapping.mid(4, 4), QString("0000"));
            QVERIFY2(mapping.mid(32).startsWith(QLatin1String(",OSD/OS ")), qPrintable(mapping));
        }
        QCOMPARE(mappings.filter(QStringLiteral(",OSD/OS Pad A,")).size(), 1);
        QCOMPARE(controllers::mappingWithoutCrc(QStringLiteral("0300a1b25e040000e002000003090000,Xbox,a:b0")),
                 QStringLiteral("030000005e040000e002000003090000,Xbox,a:b0"));
        QCOMPARE(controllers::mappingWithoutCrc(QStringLiteral("not a mapping")), QStringLiteral("not a mapping"));
        QVERIFY(env.value("OSDOS_PLAYER2_DEVICE") != QString("inherited"));

        // A pad chosen and away: its player has none until it is back.
        im.setPlayer(1, keyB);
        padB.detach();
        QTRY_VERIFY(!player(im, 1).value("connected").toBool());
        QCOMPARE(player(im, 1).value("key").toString(), keyB);
        QCOMPARE(player(im, 1).value("name").toString(), QString("OSD/OS Pad B"));
        QCOMPARE(player(im, 2).value("name").toString(), QString("OSD/OS Pad A"));
        env = QProcessEnvironment();
        controllers::applyPlayersTo(env);
        QVERIFY(!env.contains("OSDOS_PLAYER1_NAME"));
        QCOMPARE(env.value("OSDOS_PLAYER2_NAME"), QString("OSD/OS Pad A"));

        // Only a pad that is there can be chosen.
        im.setPlayer(2, "pad-0000000000000000000000000000");
        QCOMPARE(player(im, 2).value("key").toString(), keyA);

        // Kept in config.json.
        const QJsonObject saved = savedApp("players");
        QCOMPARE(saved.value("1").toObject().value("key").toString(), keyB);
        QCOMPARE(saved.value("1").toObject().value("name").toString(), QString("OSD/OS Pad B"));
        QCOMPARE(saved.value("2").toObject().value("key").toString(), keyA);

        // Two of a model: each player has one of them.
        VirtualPad twin1, twin2;
        QVERIFY(twin1.attach("OSD/OS Twin Pad", 0x1209, 0x0012));
        QVERIFY(twin2.attach("OSD/OS Twin Pad", 0x1209, 0x0012));
        QTRY_COMPARE(entry(im, "OSD/OS Twin Pad").value("count").toInt(), 2);
        const QString twins = entry(im, "OSD/OS Twin Pad").value("key").toString();
        im.setPlayer(1, twins);
        im.setPlayer(2, twins);
        QCOMPARE(player(im, 1).value("key").toString(), twins);
        QCOMPARE(player(im, 2).value("key").toString(), twins);
        QCOMPARE(SDL_JoystickGetPlayerIndex(twin1.joystick), 0);
        QCOMPARE(SDL_JoystickGetPlayerIndex(twin2.joystick), 1);
    }

    // A display hand-off closes every SDL pad so RetroArch/MAME can open
    // them, without wiping the player env or mappings the child is given;
    // reclaim opens the pads again for the menus.
    void handoffReleasesJoysticks() {
        AppCore core(m_app.path(), m_data.path());
        InputManager im(m_data.path(), &core);
        VirtualPad pad;
        QVERIFY(pad.attach("OSD/OS Handoff Pad", 0x1209, 0x00aa));
        QTRY_VERIFY(entry(im, "OSD/OS Handoff Pad").value("present").toBool());

        QProcessEnvironment before;
        controllers::applyPlayersTo(before);
        QCOMPARE(before.value("OSDOS_PLAYER1_NAME"), QString("OSD/OS Handoff Pad"));
        const QStringList mappingsBefore = controllers::padMappings();
        QVERIFY(!mappingsBefore.isEmpty());

        im.releaseJoysticksForHandoff();
        QCOMPARE(entry(im, "OSD/OS Handoff Pad").value("present").toBool(), false);

        // Frozen for ScriptLauncher / Arcade while the child has the pads.
        QProcessEnvironment during;
        controllers::applyPlayersTo(during);
        QCOMPARE(during.value("OSDOS_PLAYER1_NAME"), QString("OSD/OS Handoff Pad"));
        QCOMPARE(controllers::padMappings(), mappingsBefore);

        im.reclaimJoysticksAfterHandoff();
        // Reclaim is deferred so Coin+Start still held from quitting a game
        // is not delivered to the menus.
        QTRY_VERIFY(entry(im, "OSD/OS Handoff Pad").value("present").toBool());
        QProcessEnvironment after;
        controllers::applyPlayersTo(after);
        QCOMPARE(after.value("OSDOS_PLAYER1_NAME"), QString("OSD/OS Handoff Pad"));

        // A second reclaim with nothing released is a no-op.
        im.reclaimJoysticksAfterHandoff();
        QVERIFY(entry(im, "OSD/OS Handoff Pad").value("present").toBool());
    }

    // Six players: the JAMMA board's two, then the other pads in the order
    // they came; a seventh plays none. Three of a model are three players.
    void sixPlayers() {
        AppCore core(m_app.path(), m_data.path());
        InputManager im(m_data.path(), &core);
        VirtualPad stick1, stick2, pads[5];
        QVERIFY(stick1.attach("OSD/OS JAMMA P1", 0, 0));
        QVERIFY(stick2.attach("OSD/OS JAMMA P2", 0, 0));
        QList<QByteArray> names;
        for (int i = 0; i < 5; ++i) {
            names << "OSD/OS Pad " + QByteArray::number(i + 1);
            QVERIFY(pads[i].attach(names[i].constData(), 0x1209, 0x0020 + i));
        }
        for (const QByteArray &name : std::as_const(names))
            QTRY_VERIFY(entry(im, QString::fromLatin1(name)).value("present").toBool());
        QTRY_VERIFY(entry(im, "OSD/OS JAMMA P2").value("present").toBool());

        QCOMPARE(im.players().size(), 6);
        QCOMPARE(player(im, 1).value("name").toString(), QString("OSD/OS JAMMA P1"));
        QCOMPARE(player(im, 2).value("name").toString(), QString("OSD/OS JAMMA P2"));
        for (int p = 3; p <= 6; ++p)
            QCOMPARE(player(im, p).value("name").toString(), QString::fromLatin1(names[p - 3]));
        QCOMPARE(SDL_JoystickGetPlayerIndex(pads[3].joystick), 5);
        QCOMPARE(SDL_JoystickGetPlayerIndex(pads[4].joystick), 6);
        QProcessEnvironment env;
        controllers::applyPlayersTo(env);
        QCOMPARE(env.value("OSDOS_PLAYER6_NAME"), QString("OSD/OS Pad 4"));
        QVERIFY(!env.contains("OSDOS_PLAYER7_NAME"));

        // Pad 5 for player 3: Auto's players move up a pad.
        const QString key5 = entry(im, "OSD/OS Pad 5").value("key").toString();
        im.setPlayer(3, key5);
        QCOMPARE(player(im, 3).value("name").toString(), QString("OSD/OS Pad 5"));
        QCOMPARE(player(im, 4).value("name").toString(), QString("OSD/OS Pad 1"));
        QCOMPARE(player(im, 6).value("name").toString(), QString("OSD/OS Pad 3"));
        im.setPlayer(3, QString());

        // Three of a model are three players; a fourth chosen takes it from
        // the highest-numbered of the others.
        VirtualPad trio[2];
        QVERIFY(trio[0].attach("OSD/OS Pad 1", 0x1209, 0x0020));
        QVERIFY(trio[1].attach("OSD/OS Pad 1", 0x1209, 0x0020));
        QTRY_COMPARE(entry(im, "OSD/OS Pad 1").value("count").toInt(), 3);
        const QString key1 = entry(im, "OSD/OS Pad 1").value("key").toString();
        for (int p : { 1, 2, 5 })
            im.setPlayer(p, key1);
        QCOMPARE(player(im, 1).value("key").toString(), key1);
        QCOMPARE(player(im, 2).value("key").toString(), key1);
        QCOMPARE(player(im, 5).value("key").toString(), key1);
        im.setPlayer(6, key1);
        QCOMPARE(player(im, 6).value("key").toString(), key1);
        QCOMPARE(player(im, 1).value("key").toString(), key1);
        QCOMPARE(player(im, 2).value("key").toString(), key1);
        QCOMPARE(player(im, 5).value("key").toString(), QString());
        QCOMPARE(savedApp("players").size(), 6);
    }

    // The JAMMA board's sticks (osdos-jamma) are an arcade's, numbered.
    void jammaSticks() {
        AppCore core(m_app.path(), m_data.path());
        InputManager im(m_data.path(), &core);
        QSignalSpy keys(&im, &InputManager::mpvKeyRequested);
        VirtualPad p1, p2;
        QVERIFY(p1.attach("OSD/OS JAMMA P1", 0, 0));
        QVERIFY(p2.attach("OSD/OS JAMMA P2", 0, 0));
        QTRY_VERIFY(entry(im, "OSD/OS JAMMA P1").value("present").toBool());
        // Each by its own name and key: SDL gives P2, which has no vendor or
        // product, the mapping it made for P1, and that mapping's name.
        QTRY_VERIFY(entry(im, "OSD/OS JAMMA P2").value("present").toBool());
        const QVariantMap listed = entry(im, "OSD/OS JAMMA P1");
        QCOMPARE(listed.value("kind").toString(), QString("arcade"));
        QCOMPARE(listed.value("count").toInt(), 1);
        QVERIFY(listed.value("key") != entry(im, "OSD/OS JAMMA P2").value("key"));
        QCOMPARE(entry(im, "OSD/OS JAMMA P2").value("kind").toString(), QString("arcade"));
        QCOMPARE(tap(p1, SDL_CONTROLLER_BUTTON_A, keys), QString("ENTER"));
        QCOMPARE(im.hints().value("select").toString(), QString("[1]"));
        QCOMPARE(im.hints().value("back").toString(), QString("[2]"));
        const QVariantMap page = im.controllerPage(listed.value("key").toString());
        QCOMPARE(page.value("labelsInUse").toString(), QString("Arcade"));
        QCOMPARE(rowValue(page, "back"), QString("2, COIN"));
        QCOMPARE(rowValue(page, "left"), QString("5, D-PAD ◄, JOYSTICK ◄"));

        // Away with no settings of its own, it is still the arcade stick it
        // was: its page names and labels it so, and what is set there keeps
        // its name and kind, its labels back to Auto and set again too.
        const QString key2 = entry(im, "OSD/OS JAMMA P2").value("key").toString();
        p2.detach();
        QTRY_VERIFY(entry(im, "OSD/OS JAMMA P2").isEmpty());
        QCOMPARE(im.controllerPage(key2).value("name").toString(), QString("OSD/OS JAMMA P2"));
        QCOMPARE(im.controllerPage(key2).value("labelsInUse").toString(), QString("Arcade"));
        im.setControllerLabels(key2, "nintendo");
        QCOMPARE(entry(im, "OSD/OS JAMMA P2").value("kind").toString(), QString("arcade"));
        im.setControllerLabels(key2, "auto");
        QVERIFY(entry(im, "OSD/OS JAMMA P2").isEmpty());
        im.setControllerLabels(key2, "xbox");
        QCOMPARE(entry(im, "OSD/OS JAMMA P2").value("kind").toString(), QString("arcade"));
        QCOMPARE(savedControllers().value(key2).toObject().value("name").toString(), QString("OSD/OS JAMMA P2"));
    }

    // The JAMMA board's sticks as osdos-jamma makes them now, with a vendor
    // and a product: SDL tells them apart, each its own mapping and name. The
    // settings one had under the key it had without them (legacyJammaKey()),
    // its page's and a player's choice of it, follow it to its key.
    void jammaStickKeys() {
        QCOMPARE(controllers::legacyJammaKey("06002868010000000100000001000000", "OSD/OS JAMMA P1"),
                 QString("pad-060028684f53442f4f53204a414d4d00"));
        QCOMPARE(controllers::legacyJammaKey("0600", "OSD/OS JAMMA P1"), QString());
        QVERIFY(controllers::isJammaStick("OSD/OS JAMMA P2"));
        QVERIFY(!controllers::isJammaStick("Xbox 360 Controller"));

        AppCore core(m_app.path(), m_data.path());
        InputManager im(m_data.path(), &core);
        VirtualPad p1, p2;
        QVERIFY(p1.attach("OSD/OS JAMMA P1", 0x0001, 0x0001));
        QVERIFY(p2.attach("OSD/OS JAMMA P2", 0x0001, 0x0002));
        QTRY_VERIFY(entry(im, "OSD/OS JAMMA P1").value("present").toBool());
        QTRY_VERIFY(entry(im, "OSD/OS JAMMA P2").value("present").toBool());
        for (int i = 0; i < SDL_NumJoysticks(); ++i) {
            if (SDL_JoystickGetDeviceInstanceID(i) == p2.id)
                QCOMPARE(QString(SDL_GameControllerNameForIndex(i)), QString("OSD/OS JAMMA P2"));
        }
        const QString key1 = entry(im, "OSD/OS JAMMA P1").value("key").toString();
        QVERIFY(key1 != entry(im, "OSD/OS JAMMA P2").value("key").toString());

        // P1's settings from before: Xbox labels, and player 1's choice.
        const QString legacy = controllers::legacyJammaKey(key1.mid(4), "OSD/OS JAMMA P1");
        QVERIFY(!legacy.isEmpty() && legacy != key1);
        p1.detach();
        QTRY_VERIFY(entry(im, "OSD/OS JAMMA P1").isEmpty());
        core.save_setting(QString(), "controllers", QVariantMap{ { legacy, QVariantMap{
            { "name", "OSD/OS JAMMA P1" }, { "kind", "arcade" }, { "labels", "xbox" } } } });
        core.save_setting(QString(), "players", QVariantMap{ { "1", QVariantMap{
            { "key", legacy }, { "name", "OSD/OS JAMMA P1" } } } });
        QVERIFY(p1.attach("OSD/OS JAMMA P1", 0x0001, 0x0001));
        QTRY_COMPARE(player(im, 1).value("playing").toString(), key1);
        QVERIFY(savedControllers().contains(key1));
        QVERIFY(!savedControllers().contains(legacy));
        QCOMPARE(savedApp("players").value("1").toObject().value("key").toString(), key1);
        QCOMPARE(im.controllerPage(key1).value("labelsInUse").toString(), QString("Xbox"));
        // Nothing of it left under the old key: listed once, connected.
        int listed = 0;
        for (const QVariant &v : im.controllers())
            listed += v.toMap().value("name").toString() == "OSD/OS JAMMA P1";
        QCOMPARE(listed, 1);
        QCOMPARE(entry(im, "OSD/OS JAMMA P1").value("key").toString(), key1);
    }

    // TEST BUTTONS on a JAMMA stick's page takes both sticks at once, their
    // presses reported and acting on nothing, and the board's pins as
    // osdos-jamma --test prints them, pressed and let go, both players' and
    // another expander's.
    void jammaTest() {
        using Change = JammaPins::Change;
        const auto parsed = [](const char *line) { return JammaPins::parse(line).value_or(Change{ -1, {}, {}, false, {} }); };
        QCOMPARE(parsed("P1 0x20 B1 pressed  tl2").player, 1);
        QCOMPARE(parsed("P1 0x20 B1 pressed  tl2").address, QString("0x20"));
        QCOMPARE(parsed("P1 0x20 B1 pressed  tl2").pin, QString("B1"));
        QVERIFY(parsed("P1 0x20 B1 pressed  tl2").pressed);
        QCOMPARE(parsed("P1 0x20 B1 pressed  tl2").input, QString("tl2"));
        QCOMPARE(parsed("P2 0x21 A7 released none\n").player, 2);
        QVERIFY(!parsed("P2 0x21 A7 released none").pressed);
        QCOMPARE(parsed("P2 0x21 A7 released none").input, QString("none"));
        QCOMPARE(parsed("P0 0x24 B3 pressed  none").player, 0);
        QCOMPARE(parsed("P0 0x24 B3 pressed  none").address, QString("0x24"));
        for (const char *other : { "osdos-jamma: player 1 at 0x20", "Press each control; Ctrl+C ends.",
                                   "P3 0x20 B1 pressed  a", "P1 0x20 C1 pressed  a", "P1 0x20 B8 pressed  a",
                                   "P1 0x20 B1 held     a", "P1 0x20 B1 pressed  <b>", "" })
            QVERIFY2(!JammaPins::parse(other), other);

        AppCore core(m_app.path(), m_data.path());
        InputManager im(m_data.path(), &core);
        QSignalSpy keys(&im, &InputManager::mpvKeyRequested);
        VirtualPad p1, p2, other;
        QVERIFY(p1.attach("OSD/OS JAMMA P1", 0, 0));
        QVERIFY(p2.attach("OSD/OS JAMMA P2", 0, 0));
        QVERIFY(other.attach("OSD/OS Test Pad"));
        QTRY_VERIFY(entry(im, "OSD/OS JAMMA P2").value("present").toBool());
        QTRY_VERIFY(entry(im, "OSD/OS Test Pad").value("present").toBool());
        QCOMPARE(entry(im, "OSD/OS JAMMA P1").value("jamma").toInt(), 1);
        QCOMPARE(entry(im, "OSD/OS JAMMA P2").value("jamma").toInt(), 2);
        QCOMPARE(entry(im, "OSD/OS Test Pad").value("jamma").toInt(), 0);
        const QString key1 = entry(im, "OSD/OS JAMMA P1").value("key").toString();
        const QString key2 = entry(im, "OSD/OS JAMMA P2").value("key").toString();
        QCOMPARE(im.controllerPage(key2).value("jamma").toInt(), 2);
        QCOMPARE(im.controllerPage(entry(im, "OSD/OS Test Pad").value("key").toString()).value("jamma").toInt(), 0);

        // Both sticks waited on: each press reported with its stick's key,
        // and none acts; another pad's do.
        QSignalSpy captured(&im, &InputManager::controllerInputCaptured);
        im.captureControllers({ key1, key2 });
        QCOMPARE(tap(p1, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, keys), QString());
        QCOMPARE(tap(p2, SDL_CONTROLLER_BUTTON_BACK, keys), QString());
        QCOMPARE(tap(other, SDL_CONTROLLER_BUTTON_A, keys), QString("ENTER"));
        QCOMPARE(captured.count(), 2);
        QCOMPARE(captured.at(0).at(0).toString(), key1);
        QCOMPARE(captured.at(0).at(2).toString(), QString("5"));
        QCOMPARE(captured.at(1).at(0).toString(), key2);
        QCOMPARE(captured.at(1).at(2).toString(), QString("COIN"));
        im.captureController(QString());
        QCOMPARE(tap(p1, SDL_CONTROLLER_BUTTON_A, keys), QString("ENTER"));

        // The pins: both players' and another expander's, pressed and let go,
        // in their order; the rest of what it prints is no pin. Stopped, it is
        // no failure.
        QSignalSpy pins(&im, &InputManager::jammaPinChanged);
        QSignalSpy failed(&im, &InputManager::jammaPinsFailed);
        im.setJammaProgram(script("osdos-jamma",
            "[ \"$1\" = --test ] || exit 3\n"
            "echo 'osdos-jamma: player 1 at 0x20' >&2\n"
            "echo 'Press each control; Ctrl+C ends.'\n"
            "echo 'P1 0x20 B1 pressed  tl2'\n"
            "echo 'P1 0x20 B1 released tl2'\n"
            "printf 'P2 0x21 A7 pre'\n"
            "sleep 0.2\n"
            "printf 'ssed  z\\nP1 0x20 A4 pressed  mode\\nP0 0x24 B3 pressed  none\\n'\n"
            "exec sleep 30\n"));
        im.watchJammaPins(true);
        QTRY_COMPARE(pins.count(), 5);
        QCOMPARE(pins.at(0), QVariantList({ 1, QString("0x20"), QString("B1"), true, QString("tl2") }));
        QCOMPARE(pins.at(1), QVariantList({ 1, QString("0x20"), QString("B1"), false, QString("tl2") }));
        QCOMPARE(pins.at(2), QVariantList({ 2, QString("0x21"), QString("A7"), true, QString("z") }));
        QCOMPARE(pins.at(3), QVariantList({ 1, QString("0x20"), QString("A4"), true, QString("mode") }));
        QCOMPARE(pins.at(4), QVariantList({ 0, QString("0x24"), QString("B3"), true, QString("none") }));
        im.watchJammaPins(false);
        QTest::qWait(300);
        QCOMPARE(failed.count(), 0);

        // It ends: what it said last is why.
        im.setJammaProgram(script("osdos-jamma-none",
            "echo 'osdos-jamma: /dev/i2c-0 opened' >&2\n"
            "echo 'osdos-jamma: no MCP23017 at 0x20 or 0x21 on /dev/i2c-0: nothing to read' >&2\n"));
        im.watchJammaPins(true);
        QVERIFY(failed.wait(3000));
        QCOMPARE(failed.at(0).at(0).toString(), QString("no MCP23017 at 0x20 or 0x21 on /dev/i2c-0: nothing to read"));
        // None there at all.
        im.setJammaProgram(m_app.path() + "/osdos-jamma-missing");
        im.watchJammaPins(true);
        QCOMPARE(failed.count(), 2);
        QCOMPARE(failed.at(1).at(0).toString(), QString("osdos-jamma isn't installed"));
    }

    // A pad Steam's virtual pad comes to stand for lets go of what it held,
    // as its releases are no longer listened to.
    void suppressedWhileHeld() {
        AppCore core(m_app.path(), m_data.path());
        InputManager im(m_data.path(), &core);
        QSignalSpy keys(&im, &InputManager::mpvKeyRequested);
        VirtualPad pad, steam;
        QVERIFY(pad.attach("OSD/OS Test Pad"));
        QTRY_VERIFY(entry(im, "OSD/OS Test Pad").value("present").toBool());
        keys.clear();
        pad.button(SDL_CONTROLLER_BUTTON_DPAD_DOWN, true);
        QVERIFY(keys.wait(1000));
        QVERIFY(steam.attach("Steam Virtual Gamepad", 0x1209, 0x00ff));
        QTRY_VERIFY(entry(im, "Steam Virtual Gamepad").value("present").toBool());
        QVERIFY(entry(im, "OSD/OS Test Pad").isEmpty());
        keys.clear();
        QTest::qWait(700);   // past the repeat's first 400 ms
        QCOMPARE(keysOf(keys), QStringList());
        release(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        steam.detach();
        QTRY_VERIFY(entry(im, "OSD/OS Test Pad").value("present").toBool());
        QCOMPARE(tap(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN, keys), QString("DOWN"));
    }

    // A pad's Back button is MAME's coin key, 5, where the item with the
    // focus takes a coin (takesCoin: the nearest item round it that says),
    // and Back anywhere else; its B is Back either way. The hints name the
    // pad's Back button for the coin; its Arcade Buttons' Coin takes Back's
    // place.
    void coinButton() {
        AppCore core(m_app.path(), m_data.path());
        InputManager im(m_data.path(), &core);
        QQuickWindow window;
        window.resize(64, 48);
        window.show();
        window.requestActivate();
        QTRY_VERIFY(window.isActive());
        im.setTargetWindow(&window);
        // A screen that takes a coin, and a line of it with the focus.
        QQuickItem screen(window.contentItem());
        screen.setProperty("takesCoin", true);
        QQuickItem line(&screen);
        line.forceActiveFocus();
        QTRY_COMPARE(window.activeFocusItem(), &line);
        // The keys as the window gets them, pressed and let go.
        struct Catcher : QObject {
            QList<int> pressed, released;
            bool eventFilter(QObject *, QEvent *event) override {
                if (event->type() == QEvent::KeyPress)
                    pressed << static_cast<QKeyEvent *>(event)->key();
                else if (event->type() == QEvent::KeyRelease)
                    released << static_cast<QKeyEvent *>(event)->key();
                return false;
            }
        } catcher;
        window.installEventFilter(&catcher);

        VirtualPad pad;
        QVERIFY(pad.attach("OSD/OS JAMMA P1", 0, 0));
        QTRY_VERIFY(entry(im, "OSD/OS JAMMA P1").value("present").toBool());
        // The key a button presses, once it has been let go too.
        const auto press = [&](SDL_GameControllerButton b) -> int {
            catcher.pressed.clear();
            catcher.released.clear();
            pad.button(b, true);
            const bool got = QTest::qWaitFor([&] { return !catcher.pressed.isEmpty(); }, 1000);
            release(pad, b);
            if (!got || !QTest::qWaitFor([&] { return !catcher.released.isEmpty(); }, 1000))
                return 0;
            return catcher.pressed.first() == catcher.released.first() ? catcher.pressed.first() : -1;
        };
        QCOMPARE(press(SDL_CONTROLLER_BUTTON_BACK), int(Qt::Key_5));
        QCOMPARE(press(SDL_CONTROLLER_BUTTON_B), int(Qt::Key_Escape));
        QCOMPARE(press(SDL_CONTROLLER_BUTTON_START), int(Qt::Key_Space));
        // The JAMMA board's Coin, by the stick's labels.
        QCOMPARE(im.hints().value("coin").toString(), QString("[COIN]"));
        // The line says it takes none: Back.
        line.setProperty("takesCoin", false);
        QCOMPARE(press(SDL_CONTROLLER_BUTTON_BACK), int(Qt::Key_Escape));
        line.setProperty("takesCoin", QVariant());
        QCOMPARE(press(SDL_CONTROLLER_BUTTON_BACK), int(Qt::Key_5));
        // The screen stops taking one: Back.
        screen.setProperty("takesCoin", false);
        QCOMPARE(press(SDL_CONTROLLER_BUTTON_BACK), int(Qt::Key_Escape));

        // Its Arcade Buttons' Coin is the coin there instead, a button or a
        // trigger pressed, and the hints name it; Back is Back again.
        screen.setProperty("takesCoin", true);
        const QString key = entry(im, "OSD/OS JAMMA P1").value("key").toString();
        QCOMPARE(outcome(im.setArcadeInput(key, "coin", "rightshoulder")), QString("swapped"));
        QCOMPARE(press(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER), int(Qt::Key_5));
        QCOMPARE(press(SDL_CONTROLLER_BUTTON_BACK), int(Qt::Key_Escape));
        QCOMPARE(im.hints().value("coin").toString(), QString("[6]"));
        QCOMPARE(outcome(im.setArcadeInput(key, "coin", "lefttrigger+")), QString("swapped"));
        catcher.pressed.clear();
        catcher.released.clear();
        // A trigger's travel is the whole of its axis: let go, its least.
        pad.axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 30000);
        QVERIFY(QTest::qWaitFor([&] { return !catcher.pressed.isEmpty(); }, 1000));
        QCOMPARE(catcher.pressed, QList<int>({ int(Qt::Key_5) }));
        pad.axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, -32768);
        QVERIFY(QTest::qWaitFor([&] { return !catcher.released.isEmpty(); }, 1000));
        QCOMPARE(catcher.released, QList<int>({ int(Qt::Key_5) }));
        QCOMPARE(im.hints().value("coin").toString(), QString("[7]"));
        window.removeEventFilter(&catcher);
        im.setTargetWindow(nullptr);
    }
};

QTEST_MAIN(ControllersTest)
#include "controllers_test.moc"
