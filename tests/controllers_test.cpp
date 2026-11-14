#include "AppCore.h"
#include "input/ControllerProfile.h"
#include "input/InputManager.h"
#include "input/JammaPins.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QProcessEnvironment>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSaveFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
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

        // buttons: the ones it has, a bit each (SDL_GameControllerButton);
        // all of them unless said.
        bool attach(const char *name, Uint16 vendor = 0x1209, Uint16 product = 0x0001,
                    Uint32 buttons = (1u << SDL_CONTROLLER_BUTTON_MAX) - 1) {
            SDL_VirtualJoystickDesc desc;
            SDL_zero(desc);
            desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
            desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
            desc.naxes = SDL_CONTROLLER_AXIS_MAX;
            desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
            desc.vendor_id = vendor;
            desc.product_id = product;
            desc.button_mask = buttons;
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
    // Pressed and held, once InputManager's poll has read it.
    static void hold(VirtualPad &pad, SDL_GameControllerButton b) {
        pad.button(b, true);
        QVERIFY2(QTest::qWaitFor([&] { return SDL_JoystickGetButton(pad.joystick, b) == 1; }, 1000),
                 "SDL never saw the press");
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
        // The face buttons by position, as each style names them.
        QCOMPARE(inputLabel(S::ABelow, "a"), QString("A"));
        QCOMPARE(inputLabel(S::Shapes, "a"), QString("X"));
        QCOMPARE(inputLabel(S::ARight, "a"), QString("B"));
        QCOMPARE(inputLabel(S::Numbers, "a"), QString("1"));
        QCOMPARE(inputLabel(S::ABelow, "b"), QString("B"));
        QCOMPARE(inputLabel(S::Shapes, "b"), QString("O"));
        QCOMPARE(inputLabel(S::ARight, "b"), QString("A"));
        QCOMPARE(inputLabel(S::Numbers, "y"), QString("4"));
        QCOMPARE(inputLabel(S::Shapes, "x"), QString("SQ"));
        QCOMPARE(inputLabel(S::ARight, "y"), QString("X"));
        // The JAMMA board's eight buttons: B7 down to B0 are a, b, x, y, the
        // shoulders and the triggers (osdos-jamma).
        QCOMPARE(inputLabel(S::Numbers, "rightshoulder"), QString("6"));
        QCOMPARE(inputLabel(S::Numbers, "righttrigger+"), QString("8"));
        QCOMPARE(inputLabel(S::Numbers, "back"), QString("COIN"));
        QCOMPARE(inputLabel(S::Numbers, "start"), QString("START"));
        QCOMPARE(inputLabel(S::Shapes, "lefttrigger+"), QString("L2"));
        QCOMPARE(inputLabel(S::ARight, "start"), QString("+"));
        QCOMPARE(inputLabel(S::ABelow, "leftshoulder"), QString("LB"));
        // The d-pad and the sticks with their direction.
        QCOMPARE(inputLabel(S::ABelow, "dpup"), QString("D-PAD ▲"));
        QCOMPARE(inputLabel(S::ARight, "dpright"), QString("D-PAD ►"));
        QCOMPARE(inputLabel(S::ABelow, "leftx-"), QString("L STICK ◄"));
        QCOMPARE(inputLabel(S::ABelow, "lefty+"), QString("L STICK ▼"));
        QCOMPARE(inputLabel(S::Shapes, "righty-"), QString("R STICK ▲"));
        QCOMPARE(inputLabel(S::Numbers, "leftx+"), QString("JOYSTICK ►"));
        // What has no name of its own: as input.cfg calls it.
        QCOMPARE(inputLabel(S::ABelow, "paddle1"), QString("PADDLE1"));

        QCOMPARE(controllers::labelStyleFromName("Shapes"), S::Shapes);
        // The names an older version wrote for the same styles are still read.
        QCOMPARE(controllers::labelStyleFromName("xbox"), S::ABelow);
        QCOMPARE(controllers::labelStyleFromName("PlayStation"), S::Shapes);
        QCOMPARE(controllers::labelStyleFromName("nintendo"), S::ARight);
        QCOMPARE(controllers::labelStyleFromName("arcade"), S::Numbers);
        QCOMPARE(controllers::labelStyleTitle(S::ABelow), QString("A below"));
        QCOMPARE(inputLabel(S::Shapes, "guide"), QString("HOME"));
        QCOMPARE(controllers::labelStyleFromName("bogus"), S::Auto);
        QCOMPARE(controllers::labelStyleNames(),
                 QStringList({ "auto", "a_below", "a_right", "shapes", "numbers" }));
    }

    void profileAsKept() {
        const QVariantMap stored{
            { "name", "8BitDo Pro 2" }, { "kind", "gamepad" }, { "labels", "A_Right" },
            { "bindings", QVariantMap{ { "A", "Select" }, { "leftx-", "back" }, { "x", "bogus" },
                                       { "", "up" }, { "b", "none" } } } };
        const controllers::Profile p = controllers::Profile::fromVariant(stored);
        QCOMPARE(p.name, QString("8BitDo Pro 2"));
        QCOMPARE(p.kind, QString("gamepad"));
        QCOMPARE(p.labels, controllers::LabelStyle::ARight);
        // An action it doesn't know, or no input, isn't one.
        QCOMPARE(p.bindings.size(), 3);
        QCOMPARE(p.bindings.value("a"), QString("select"));
        QCOMPARE(p.bindings.value("leftx-"), QString("back"));
        QCOMPARE(p.bindings.value("b"), QString("none"));

        const QVariantMap kept = p.toVariant();
        QCOMPARE(kept.value("labels").toString(), QString("a_right"));
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
        QCOMPARE(arcadeLayouts(), QStringList({ "default", "fighting6", "neogeo4", "nes", "megadrive", "snes", "saturn", "n64",
                                                "custom" }));
        QCOMPARE(arcadeLayoutTitle("fighting6"), QString("Fighting 6"));
        QCOMPARE(arcadeLayoutTitle(""), QString("Engine's"));
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
        // On a JAMMA board's stick, wired by number, Fighting 6 is its
        // buttons in order, 1 2 3 over 4 5 6; on a pad, its face buttons'
        // rows.
        QCOMPARE(arcadeLayoutInputs("fighting6", true), arcadeDefaultInputs());
        QCOMPARE(arcadeLayoutInputs("fighting6").mid(0, 6),
                 QStringList({ "x", "y", "rightshoulder", "a", "b", "righttrigger+" }));
        QCOMPARE(arcadeLayoutInputs("neogeo4", true), arcadeLayoutInputs("neogeo4"));
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
        PadShape full;
        for (const char *input : { "a", "b", "x", "y", "back", "guide", "start", "leftstick", "rightstick",
                                   "leftshoulder", "rightshoulder", "leftx", "lefty", "rightx", "righty",
                                   "lefttrigger", "righttrigger" })
            full.inputs.insert(input);
        QCOMPARE(mameInput(full, "a"), QString("BUTTON1"));
        QCOMPARE(mameInput(full, "rightshoulder"), QString("BUTTON6"));
        QCOMPARE(mameInput(full, "leftstick"), QString("BUTTON7"));
        QCOMPARE(mameInput(full, "guide"), QString("BUTTON9"));
        QCOMPARE(mameInput(full, "back"), QString("SELECT"));
        QCOMPARE(mameInput(full, "start"), QString("START"));
        QCOMPARE(mameInput(full, "lefttrigger+"), QString("SLIDER1_NEG_SWITCH"));
        QCOMPARE(mameInput(full, "righttrigger+"), QString("SLIDER2_NEG_SWITCH"));
        QCOMPARE(mameInput(full, "rightx-"), QString("ZAXIS_NEG_SWITCH"));
        QCOMPARE(mameInput(full, "righty+"), QString("RZAXIS_POS_SWITCH"));
        QCOMPARE(mameInput(full, "paddle1"), QString());
        QCOMPARE(mameInput(full, ""), QString());
        // Triggers that are buttons (a JAMMA stick's, a Switch Pro's).
        PadShape stick = full;
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
        QCOMPARE(arcadeActing(away, full), arcadeActing(away, small));
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
        QCOMPARE(page.value("labelsInUse").toString(), QString("A below"));
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
        im->setControllerLabels(key, "shapes");
        QCOMPARE(im->controllerPage(key).value("labelsInUse").toString(), QString("Shapes"));
        QCOMPARE(im->hints().value("select").toString(), QString("[SQ]"));
        QCOMPARE(im->hints().value("back").toString(), QString("[O]"));
        QCOMPARE(rowValue(im->controllerPage(key), "left"), QString("L1, D-PAD ◄"));
        im->setControllerLabels(key, "a_right");
        QCOMPARE(im->hints().value("select").toString(), QString("[Y]"));
        QCOMPARE(im->hints().value("play_pause").toString(), QString("[+]"));

        // Kept in config.json, by its key.
        const QJsonObject saved = savedControllers().value(key).toObject();
        QCOMPARE(saved.value("name").toString(), QString("OSD/OS Test Pad"));
        QCOMPARE(saved.value("kind").toString(), QString("gamepad"));
        QCOMPARE(saved.value("labels").toString(), QString("a_right"));
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
        QCOMPARE(rowValue(page, "select"), QString("Y"));   // A right's name for X
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
        QCOMPARE(page.value("layoutTitle").toString(), QString("Engine's"));
        QVERIFY(page.value("present").toBool());
        QCOMPARE(im->controllerPage(key).value("arcade").toString(), QString("Engine's"));
        QCOMPARE(values(), QStringList({ "Engine's", "Engine's", "Engine's", "Engine's", "Engine's", "Engine's", "Engine's", "Engine's", "BACK", "START" }));
        QCOMPARE(listed(page.value("layouts").toList(), "title"),
                 QStringList({ "Engine's", "Fighting 6", "Neo Geo 4", "NES", "Mega Drive", "SNES", "Saturn", "N64", "Custom" }));
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
        // What was set by hand is kept beside it, and beside a preset, so
        // that Custom brings it back.
        QJsonObject keeps = savedControllers().value(key).toObject().value("arcade").toObject();
        QCOMPARE(keeps.value("layout").toString(), QString("default"));
        QVERIFY(!keeps.contains("inputs"));
        QCOMPARE(keeps.value("custom").toObject().value("5").toString(), QString("leftshoulder"));
        im->setArcadeLayout(key, "fighting6");
        QCOMPARE(values().at(0), QString("X"));
        keeps = savedControllers().value(key).toObject().value("arcade").toObject();
        QCOMPARE(keeps.value("inputs").toObject().value("1").toString(), QString("x"));
        QCOMPARE(keeps.value("custom").toObject().value("1").toString(), QString("a"));
        im->setArcadeLayout(key, "custom");
        QCOMPARE(values(), QStringList({ "A", "B", "X", "Y", "LB", "RB", "None", "None", "BACK", "START" }));
        QVERIFY(!savedControllers().value(key).toObject().value("arcade").toObject().contains("custom"));
        im->setArcadeLayout(key, "default");
        im->setArcadeLayout(key, "bogus");
        QCOMPARE(im->arcadePage(key).value("layout").toString(), QString("default"));
        im->setArcadeLayout(key, "neogeo4");
        QCOMPARE(values().mid(0, 4), QStringList({ "A", "B", "X", "Y" }));
        // A console pad's own, by the inputs SDL gives its buttons: none
        // where its pad has fewer; a Saturn's Coin on L and an N64's on Z,
        // which have no Select.
        im->setArcadeLayout(key, "nes");
        QCOMPARE(values(), QStringList({ "B", "A", "None", "None", "None", "None", "None", "None", "BACK", "START" }));
        im->setArcadeLayout(key, "megadrive");
        QCOMPARE(values(), QStringList({ "A", "B", "RT", "X", "Y", "RB", "None", "None", "BACK", "START" }));
        im->setArcadeLayout(key, "snes");
        QCOMPARE(values(), QStringList({ "X", "Y", "LB", "A", "B", "RB", "None", "None", "BACK", "START" }));
        im->setArcadeLayout(key, "saturn");
        QCOMPARE(values(), QStringList({ "X", "Y", "RB", "A", "B", "RT", "LT", "None", "LB", "START" }));
        im->setArcadeLayout(key, "n64");
        QCOMPARE(values(), QStringList({ "B", "R STICK ◄", "R STICK ▲", "A", "R STICK ▼", "R STICK ►", "LB", "RB",
                                         "LT", "START" }));
        QCOMPARE(im->controllerPage(key).value("arcade").toString(), QString("N64"));
        QCOMPARE(controllers::playerArcade().value(0).mame,
                 QStringList({ "BUTTON2", "ZAXIS_NEG_SWITCH", "RZAXIS_NEG_SWITCH", "BUTTON1", "RZAXIS_POS_SWITCH",
                               "ZAXIS_POS_SWITCH", "BUTTON5", "BUTTON6", "SLIDER1_NEG_SWITCH", "START" }));
        QCOMPARE(controllers::playerArcade().value(0).retroarch,
                 QStringList({ "1", "-2", "-3", "0", "+3", "+2", "9", "10", "+4", "6" }));
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
    }

    // A keyboard's or a remote's keys on a page: the menus' own keys keep what
    // they do, and an extra key that is the Select / OK, Up or Down in use
    // stays it until the menus have been worked with something else.
    void extraKeysStay() {
        AppCore core(m_app.path(), m_data.path());
        // A remote, known as one: a keyboard has the menus' own keys beside
        // its extra ones, which can always go (savedByTheVersionBefore()).
        const QString kbd = QStringLiteral("kbd-0005-046d-b342");
        core.save_setting(QString(), "controllers", QVariantMap{ { kbd, QVariantMap{
            { "name", "Air Remote" }, { "kind", "remote" }, { "labels", "a_right" } } } });
        InputManager im(m_data.path(), &core);
        QVariantMap result;

        // Back ends the wait; Select / OK's and the menus' keys keep what
        // they do; any other is set.
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
        // The extra Down stays while nothing else has been seen to go down: it
        // may be the only Down that remote has.
        result = im.keyCapture(QString(), Qt::Key_F7, "up");
        QCOMPARE(outcome(result), QString("only"));
        QCOMPARE(result.value("label").toString(), QString("Down"));
        QCOMPARE(outcome(im.keyCapture(InputManager::allKeys(), Qt::Key_F7, "up")), QString("only"));
        QCOMPARE(outcome(im.keyCapture(QString(), Qt::Key_F5, "right")), QString("set"));
        QCOMPARE(outcome(im.keyCapture(QString(), Qt::Key_F5, "left")), QString("already"));
        QCOMPARE(outcome(im.keyCapture(kbd, ccBack, "up")), QString("cancel"));

        // Its own: set, then kept as its Select / OK or Up; moved; taken away.
        QCOMPARE(outcome(im.bindControllerInput(kbd, key(Qt::Key_O), "select")), QString("set"));
        result = im.bindControllerInput(kbd, key(Qt::Key_O), "up");
        QCOMPARE(outcome(result), QString("only"));
        QCOMPARE(result.value("label").toString(), QString("Select / OK"));
        QCOMPARE(outcome(im.bindControllerInput(kbd, key(Qt::Key_O), "none")), QString("only"));
        QCOMPARE(outcome(im.removeControllerInput(kbd, key(Qt::Key_O))), QString("only"));
        QCOMPARE(outcome(im.bindControllerInput(kbd, key(Qt::Key_Q), "up")), QString("set"));
        QCOMPARE(outcome(im.bindControllerInput(kbd, key(Qt::Key_Q), "down")), QString("only"));
        QCOMPARE(outcome(im.bindControllerInput(kbd, key(Qt::Key_P), "left")), QString("set"));
        QCOMPARE(outcome(im.bindControllerInput(kbd, key(Qt::Key_P), "right")), QString("set"));
        QCOMPARE(outcome(im.bindControllerInput(kbd, key(Qt::Key_F6), "none")), QString("other"));
        QCOMPARE(outcome(im.bindControllerInput(kbd, key(Qt::Key_Return), "up")), QString("already"));
        QCOMPARE(outcome(im.bindControllerInput(kbd, key(Qt::Key_Escape), "none")), QString("cancel"));
        QCOMPARE(savedControllers().value(kbd).toObject().value("bindings").toObject(),
                 QJsonObject({ { key(Qt::Key_O), "select" }, { key(Qt::Key_Q), "up" }, { key(Qt::Key_P), "right" } }));
        QCOMPARE(outcome(im.removeControllerInput(kbd, key(Qt::Key_P))), QString("set"));
        QCOMPARE(outcome(im.removeControllerInput(kbd, key(Qt::Key_P))), QString("other"));
        QCOMPARE(savedControllers().value(kbd).toObject().value("bindings").toObject(),
                 QJsonObject({ { key(Qt::Key_O), "select" }, { key(Qt::Key_Q), "up" } }));

        // Once the menus have been worked with another key, the extra one can
        // go: Enter selected, so O is no longer the Select / OK in use.
        QWindow window;
        const auto press = [&window](int qtKey) {
            QKeyEvent down(QEvent::KeyPress, qtKey, Qt::NoModifier);
            QCoreApplication::sendEvent(&window, &down);
            QKeyEvent up(QEvent::KeyRelease, qtKey, Qt::NoModifier);
            QCoreApplication::sendEvent(&window, &up);
        };
        press(Qt::Key_Return);
        QCOMPARE(outcome(im.bindControllerInput(kbd, key(Qt::Key_O), "left")), QString("set"));
        // Q went up last: it stays, until an arrow has.
        press(Qt::Key_Q);
        QCOMPARE(outcome(im.removeControllerInput(kbd, key(Qt::Key_Q))), QString("only"));
        press(Qt::Key_Up);
        QCOMPARE(outcome(im.removeControllerInput(kbd, key(Qt::Key_Q))), QString("set"));
        // The same for every keyboard's: F7 can move once an arrow went down.
        press(Qt::Key_Down);
        QCOMPARE(outcome(im.keyCapture(QString(), Qt::Key_F7, "up")), QString("set"));
    }

    // A wait for a press takes a tap for the answer and a hold for its way
    // out: a press is told at once (to show), told again as the answer when
    // it is let go, and held a second and a half it stops the wait, answers
    // nothing and acts on nothing. A pad's buttons and stick, and with All
    // Keyboards & Remotes a key and a mouse button.
    void tapAndHold() {
        AppCore core(m_app.path(), m_data.path());
        InputManager im(m_data.path(), &core);
        QSignalSpy keys(&im, &InputManager::mpvKeyRequested);
        QSignalSpy down(&im, &InputManager::controllerInputDown);
        QSignalSpy captured(&im, &InputManager::controllerInputCaptured);
        QSignalSpy held(&im, &InputManager::captureHeld);
        VirtualPad pad;
        QVERIFY(pad.attach("OSD/OS Test Pad"));
        QTRY_VERIFY(entry(im, "OSD/OS Test Pad").value("present").toBool());
        const QString key = entry(im, "OSD/OS Test Pad").value("key").toString();

        // A tap: down as it is pressed, the answer as it is let go.
        im.captureController(key);
        pad.button(SDL_CONTROLLER_BUTTON_X, true);
        QVERIFY(down.wait(1000));
        QCOMPARE(down.at(0), QList<QVariant>({ key, QString("x"), QString("X") }));
        QCOMPARE(captured.count(), 0);
        release(pad, SDL_CONTROLLER_BUTTON_X);
        QTRY_COMPARE(captured.count(), 1);
        QCOMPARE(captured.at(0), QList<QVariant>({ key, QString("x"), QString("X") }));
        QCOMPARE(held.count(), 0);

        // Held: the wait's way out, with the pad's own Back as with any
        // other; let go, it is no answer, and nothing acted.
        QElapsedTimer since;
        since.start();
        pad.button(SDL_CONTROLLER_BUTTON_B, true);
        QVERIFY(held.wait(3000));
        QVERIFY2(since.elapsed() >= 1400, qPrintable(QString::number(since.elapsed())));
        QCOMPARE(held.at(0).at(0).toString(), key);
        QCOMPARE(down.count(), 2);
        im.captureController(QString());   // as the page does
        release(pad, SDL_CONTROLLER_BUTTON_B);
        QTest::qWait(100);
        QCOMPARE(captured.count(), 1);
        QCOMPARE(keysOf(keys), QStringList());
        QCOMPARE(tap(pad, SDL_CONTROLLER_BUTTON_B, keys), QString("ESC"));

        // Two held together: the older one's time counts; a stick pushed is
        // a press like any.
        captured.clear();
        held.clear();
        im.captureController(key);
        pad.axis(SDL_CONTROLLER_AXIS_LEFTY, -30000);
        QTest::qWait(700);
        pad.button(SDL_CONTROLLER_BUTTON_Y, true);
        QVERIFY(held.wait(1500));
        centre(pad, SDL_CONTROLLER_AXIS_LEFTY);
        release(pad, SDL_CONTROLLER_BUTTON_Y);
        QTest::qWait(100);
        QCOMPARE(captured.count(), 0);
        // The wait still on (a test goes on to its next press): a tap answers.
        pad.axis(SDL_CONTROLLER_AXIS_LEFTY, -30000);
        QTest::qWait(100);
        centre(pad, SDL_CONTROLLER_AXIS_LEFTY);
        QTRY_COMPARE(captured.count(), 1);
        QCOMPARE(captured.at(0), QList<QVariant>({ key, QString("lefty-"), QString("L STICK ▲") }));
        im.captureController(QString());

        // The wait ended while a button is down: its release answers nothing.
        captured.clear();
        im.captureController(key);
        pad.button(SDL_CONTROLLER_BUTTON_X, true);
        QTest::qWait(100);
        im.captureController(QString());
        release(pad, SDL_CONTROLLER_BUTTON_X);
        QTest::qWait(100);
        QCOMPARE(captured.count(), 0);
        QCOMPARE(held.count(), 1);

        // All Keyboards & Remotes: a key and a mouse button the same way,
        // under its own key; a pad's button still acts (its Back cancels
        // the page's wait).
        QWindow window;
        const auto send = [&window](QEvent::Type type, bool repeat) {
            QKeyEvent event(type, Qt::Key_P, Qt::NoModifier, QStringLiteral("p"), repeat);
            QCoreApplication::sendEvent(&window, &event);
        };
        const auto mouse = [&window](QEvent::Type type) {
            QMouseEvent event(type, QPointF(1, 1), QPointF(1, 1), Qt::MiddleButton,
                              type == QEvent::MouseButtonPress ? Qt::MiddleButton : Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(&window, &event);
        };
        const QString all = InputManager::allKeys();
        down.clear();
        captured.clear();
        held.clear();
        keys.clear();
        im.captureController(all);
        send(QEvent::KeyPress, false);
        QCOMPARE(down.count(), 1);
        QCOMPARE(down.at(0), QList<QVariant>({ all, QString::number(int(Qt::Key_P)), QString("P") }));
        QCOMPARE(captured.count(), 0);
        send(QEvent::KeyPress, true);
        send(QEvent::KeyRelease, false);
        QCOMPARE(captured.count(), 1);
        QCOMPARE(captured.at(0), down.at(0));
        mouse(QEvent::MouseButtonPress);
        mouse(QEvent::MouseButtonRelease);
        QCOMPARE(captured.count(), 2);
        QCOMPARE(captured.at(1).at(1).toString(), QString::number(0x03000000 + int(Qt::MiddleButton)));
        QCOMPARE(captured.at(1).at(2).toString(), QString("Mouse: Middle Click"));
        QCOMPARE(tap(pad, SDL_CONTROLLER_BUTTON_B, keys), QString("ESC"));
        QCOMPARE(captured.count(), 2);

        // P held: the way out. Saved for Down meanwhile, its repeats and its
        // release act on nothing; pressed anew, it is Down.
        keys.clear();
        send(QEvent::KeyPress, false);
        QVERIFY(held.wait(3000));
        QCOMPARE(held.at(0).at(0).toString(), all);
        im.captureController(QString());
        core.save_setting(QString(), "remote_keymap.down", int(Qt::Key_P));
        send(QEvent::KeyPress, true);
        send(QEvent::KeyPress, true);
        send(QEvent::KeyRelease, false);
        QCOMPARE(captured.count(), 2);
        QCOMPARE(keysOf(keys), QStringList());
        send(QEvent::KeyPress, false);
        send(QEvent::KeyRelease, false);
        QCOMPARE(keysOf(keys), QStringList({ "DOWN" }));
    }

    // Menu Buttons on a pad's page. Replace makes the button pressed the
    // action's only one, the ones that did it doing nothing after; where the
    // button was the pad's only Select / OK, Back, Up or Down, the first of
    // those takes that in its place, so A and B swap in one step from either
    // row. Remove one takes a button's action away, but for the last of
    // those four. The page lists only the buttons a pad connected has, each
    // with its name, and says what an input is in the menus and the games.
    void menuButtons() {
        AppCore core(m_app.path(), m_data.path());
        InputManager im(m_data.path(), &core);
        QSignalSpy keys(&im, &InputManager::mpvKeyRequested);
        VirtualPad pad, small;
        QVERIFY(pad.attach("OSD/OS Test Pad"));
        // One with no Back button and no shoulders.
        const Uint32 fewer = ((1u << SDL_CONTROLLER_BUTTON_MAX) - 1)
                             & ~((1u << SDL_CONTROLLER_BUTTON_BACK) | (1u << SDL_CONTROLLER_BUTTON_LEFTSHOULDER)
                                 | (1u << SDL_CONTROLLER_BUTTON_RIGHTSHOULDER));
        QVERIFY(small.attach("OSD/OS Small Pad", 0x1209, 0x0005, fewer));
        QTRY_VERIFY(entry(im, "OSD/OS Test Pad").value("present").toBool());
        QTRY_VERIFY(entry(im, "OSD/OS Small Pad").value("present").toBool());
        const QString key = entry(im, "OSD/OS Test Pad").value("key").toString();
        const QString skey = entry(im, "OSD/OS Small Pad").value("key").toString();
        const auto row = [&im](const QString &k, const QString &action) {
            const QVariantList rows = im.controllerPage(k).value("rows").toList();
            for (const QVariant &v : rows) {
                if (v.toMap().value("action").toString() == action)
                    return v.toMap();
            }
            return QVariantMap();
        };
        const auto inputsOf = [&row](const QString &k, const QString &action) {
            QStringList tokens;
            const QVariantList inputs = row(k, action).value("inputs").toList();
            for (const QVariant &v : inputs)
                tokens << v.toMap().value("input").toString();
            return tokens;
        };

        // The rows: what does each action, the first of them, and each by its
        // input; on the small pad only what it has.
        QCOMPARE(row(key, "back").value("value").toString(), QString("B, BACK"));
        QCOMPARE(row(key, "back").value("first").toString(), QString("B"));
        QCOMPARE(inputsOf(key, "back"), QStringList({ "b", "back" }));
        QCOMPARE(row(key, "back").value("inputs").toList().at(1).toMap().value("label").toString(), QString("BACK"));
        QCOMPARE(row(skey, "back").value("value").toString(), QString("B"));
        QCOMPARE(row(skey, "left").value("value").toString(), QString("D-PAD ◄, L STICK ◄"));
        QCOMPARE(inputsOf(skey, "left"), QStringList({ "dpleft", "leftx-" }));

        // Back's row, Replace, A pressed: A was the only Select / OK, so B
        // takes it, and BACK, which did Back too, does nothing now.
        QVariantMap result = im.replaceControllerInput(key, "a", "back");
        QCOMPARE(outcome(result), QString("swapped"));
        QCOMPARE(result.value("action").toString(), QString("back"));
        QCOMPARE(result.value("other").toString(), QString("B"));
        QCOMPARE(result.value("otherAction").toString(), QString("Select / OK"));
        QCOMPARE(result.value("cleared").toStringList(), QStringList({ "BACK" }));
        QCOMPARE(tap(pad, SDL_CONTROLLER_BUTTON_A, keys), QString("ESC"));
        QCOMPARE(tap(pad, SDL_CONTROLLER_BUTTON_B, keys), QString("ENTER"));
        QCOMPARE(tap(pad, SDL_CONTROLLER_BUTTON_BACK, keys), QString());
        QCOMPARE(row(key, "select").value("value").toString(), QString("B"));
        QCOMPARE(row(key, "back").value("value").toString(), QString("A"));
        // The same again is nothing new.
        QCOMPARE(outcome(im.replaceControllerInput(key, "a", "back")), QString("already"));
        im.resetController(key);

        // Select / OK's row, Replace, B pressed: Back has BACK too, so
        // nothing is swapped: B selects, A does nothing.
        result = im.replaceControllerInput(key, "b", "select");
        QCOMPARE(outcome(result), QString("set"));
        QCOMPARE(result.value("cleared").toStringList(), QStringList({ "A" }));
        QCOMPARE(row(key, "select").value("value").toString(), QString("B"));
        QCOMPARE(row(key, "back").value("value").toString(), QString("BACK"));
        QCOMPARE(tap(pad, SDL_CONTROLLER_BUTTON_A, keys), QString());
        im.resetController(key);

        // A button that did nothing takes an action's place: its buttons go.
        result = im.replaceControllerInput(key, "x", "left");
        QCOMPARE(outcome(result), QString("set"));
        QCOMPARE(result.value("cleared").toStringList(), QStringList({ "LB", "D-PAD ◄", "L STICK ◄" }));
        QCOMPARE(row(key, "left").value("value").toString(), QString("X"));
        // One of the action's own kept alone.
        result = im.replaceControllerInput(key, "start", "play_pause");
        QCOMPARE(outcome(result), QString("already"));
        im.resetController(key);

        // On the small pad B is the only Back and A the only Select / OK: the
        // two swap from either row, where a second button was refused before.
        result = im.replaceControllerInput(skey, "b", "select");
        QCOMPARE(outcome(result), QString("swapped"));
        QCOMPARE(result.value("other").toString(), QString("A"));
        QCOMPARE(result.value("otherAction").toString(), QString("Back"));
        QCOMPARE(tap(small, SDL_CONTROLLER_BUTTON_B, keys), QString("ENTER"));
        QCOMPARE(tap(small, SDL_CONTROLLER_BUTTON_A, keys), QString("ESC"));
        // An action with no button has none to give: the only Back stays.
        QCOMPARE(outcome(im.bindControllerInput(skey, "start", "none")), QString("set"));
        result = im.replaceControllerInput(skey, "a", "play_pause");
        QCOMPARE(outcome(result), QString("only"));
        QCOMPARE(result.value("label").toString(), QString("Back"));
        // Add another refuses it too, and Remove one.
        QCOMPARE(outcome(im.bindControllerInput(skey, "a", "play_pause")), QString("only"));
        QCOMPARE(outcome(im.removeControllerInput(skey, "a")), QString("only"));
        // Remove one of two: the other then stays.
        QCOMPARE(outcome(im.removeControllerInput(key, "back")), QString("set"));
        QCOMPARE(outcome(im.removeControllerInput(key, "b")), QString("only"));
        QCOMPARE(outcome(im.removeControllerInput(key, "start")), QString("set"));
        QCOMPARE(row(key, "play_pause").value("value").toString(), QString("None"));
        QCOMPARE(row(key, "play_pause").value("first").toString(), QString("None"));

        // What an input is, for the pad's test: its action in the menus, and
        // in the games the joystick, MAME's menu, a Game Button, or under
        // Default the emulators' to number (Coin and Start always OSD/OS's).
        im.resetController(key);
        QCOMPARE(im.inputRoles(key, "a"), QVariantMap({ { "action", "Select / OK" }, { "game", "Engine's" } }));
        QCOMPARE(im.inputRoles(key, "back"), QVariantMap({ { "action", "Back" }, { "game", "Coin" } }));
        QCOMPARE(im.inputRoles(key, "dpup"), QVariantMap({ { "action", "Up" }, { "game", "Joystick" } }));
        QCOMPARE(im.inputRoles(key, "guide"), QVariantMap({ { "action", "" }, { "game", "MAME's menu" } }));
        im.setArcadeLayout(key, "fighting6");
        QCOMPARE(im.inputRoles(key, "a"), QVariantMap({ { "action", "Select / OK" }, { "game", "Button 4" } }));
        QCOMPARE(im.inputRoles(key, "x"), QVariantMap({ { "action", "" }, { "game", "Button 1" } }));
        QCOMPARE(im.inputRoles(key, "leftstick"), QVariantMap({ { "action", "" }, { "game", "" } }));
    }

    // All Keyboards & Remotes' page and a keyboard's own: each action's own
    // key by name, then the extra keys; several to an action; Replace puts a
    // new key in the place of the action's others at once; kept in
    // config.json as a number for one key, as every version wrote it, and a
    // list for more.
    void extraKeys() {
        AppCore core(m_app.path(), m_data.path());
        InputManager im(m_data.path(), &core);
        const QString all = InputManager::allKeys();
        const auto id = [](int qtKey) { return QString::number(qtKey); };
        const auto row = [&im](const QString &k, const QString &action) {
            const QVariantList rows = im.controllerPage(k).value("rows").toList();
            for (const QVariant &v : rows) {
                if (v.toMap().value("action").toString() == action)
                    return v.toMap();
            }
            return QVariantMap();
        };
        const auto stored = [&core](const char *action) {
            return core.get_setting(QString(), QStringLiteral("remote_keymap.") + action);
        };

        QVariantMap page = im.controllerPage(all);
        QCOMPARE(page.value("name").toString(), QString("All Keyboards & Remotes"));
        QCOMPARE(page.value("kind").toString(), QString("keys"));
        QVERIFY(page.value("present").toBool());
        QCOMPARE(page.value("rows").toList().size(), 7);
        QCOMPARE(row(all, "up").value("value").toString(), QString("Arrow key"));
        QCOMPARE(row(all, "select").value("value").toString(), QString("Enter"));
        QCOMPARE(row(all, "back").value("value").toString(), QString("Esc"));
        QCOMPARE(row(all, "play_pause").value("value").toString(), QString("Space"));
        QVERIFY(row(all, "select").value("inputs").toList().isEmpty());

        // One extra key, then another beside it, for an action the menus can
        // do without; Play / Pause has a row here too.
        QSignalSpy changed(&im, &InputManager::controllersChanged);
        QCOMPARE(outcome(im.bindControllerInput(all, id(Qt::Key_F5), "left")), QString("set"));
        QCOMPARE(stored("left").toInt(), int(Qt::Key_F5));
        // The page is told, to show it.
        QVERIFY(changed.count() > 0);
        QCOMPARE(outcome(im.bindControllerInput(all, id(Qt::Key_F6), "left")), QString("set"));
        QCOMPARE(row(all, "left").value("value").toString(), QString("Arrow key + F5 + F6"));
        QCOMPARE(stored("left").toList().size(), 2);
        QCOMPARE(outcome(im.bindControllerInput(all, id(Qt::Key_F9), "play_pause")), QString("set"));
        QCOMPARE(row(all, "play_pause").value("value").toString(), QString("Space + F9"));
        // One key, one action: given to another, it moves.
        QCOMPARE(outcome(im.bindControllerInput(all, id(Qt::Key_F6), "right")), QString("set"));
        QCOMPARE(row(all, "left").value("value").toString(), QString("Arrow key + F5"));
        QCOMPARE(row(all, "right").value("value").toString(), QString("Arrow key + F6"));
        QCOMPARE(stored("left").toInt(), int(Qt::Key_F5));
        // Replace: F8 in, F5 out, at once.
        QVariantMap result = im.replaceControllerInput(all, id(Qt::Key_F8), "left");
        QCOMPARE(outcome(result), QString("set"));
        QCOMPARE(result.value("cleared").toStringList(), QStringList({ "F5" }));
        QCOMPARE(row(all, "left").value("value").toString(), QString("Arrow key + F8"));
        QCOMPARE(row(all, "left").value("inputs").toList().at(0).toMap().value("input").toString(), id(Qt::Key_F8));
        // The menus' own keys keep what they do, Back ends a wait.
        QCOMPARE(outcome(im.replaceControllerInput(all, id(Qt::Key_Return), "left")), QString("already"));
        QCOMPARE(outcome(im.replaceControllerInput(all, id(Qt::Key_Escape), "left")), QString("cancel"));
        // Remove one.
        QCOMPARE(outcome(im.removeControllerInput(all, id(Qt::Key_F8))), QString("set"));
        QCOMPARE(outcome(im.removeControllerInput(all, id(Qt::Key_F8))), QString("other"));
        QCOMPARE(stored("left").toInt(), 0);
        QCOMPARE(row(all, "left").value("value").toString(), QString("Arrow key"));

        // A keyboard's own page names every keyboard's extra keys before
        // its own; a key of its own for another action comes first on it.
        const QString kbd = QStringLiteral("kbd-0005-046d-b342");
        QCOMPARE(row(kbd, "right").value("value").toString(), QString("Arrow key + F6 (all)"));
        QCOMPARE(outcome(im.bindControllerInput(kbd, id(Qt::Key_F7), "right")), QString("set"));
        QCOMPARE(row(kbd, "right").value("value").toString(), QString("Arrow key + F6 (all) + F7"));
        QCOMPARE(row(kbd, "right").value("inputs").toList().size(), 1);
        QCOMPARE(outcome(im.bindControllerInput(kbd, id(Qt::Key_F6), "left")), QString("set"));
        QCOMPARE(row(kbd, "right").value("value").toString(), QString("Arrow key + F7"));
        QCOMPARE(row(kbd, "left").value("value").toString(), QString("Arrow key + F6"));
        QCOMPARE(im.inputRoles(kbd, id(Qt::Key_F6)), QVariantMap({ { "action", "Left" }, { "game", "" } }));
        QCOMPARE(im.inputRoles(kbd, id(Qt::Key_Return)).value("action").toString(), QString("Select / OK"));

        // At the next start, the same.
        InputManager again(m_data.path(), &core);
        QCOMPARE(again.controllerPage(all).value("rows").toList().at(3).toMap().value("value").toString(),
                 QString("Arrow key + F6"));
        QCOMPARE(again.keyCapture(QString(), Qt::Key_F9, "play_pause").value("outcome").toString(), QString("already"));
    }

    // A pad's player from its own page: Auto, or a number. A number another
    // pad was chosen for swaps the two; one a pad had by Auto leaves that pad
    // to Auto, and the answer says where it went. Every player back to Auto
    // in one step.
    void padPlayer() {
        AppCore core(m_app.path(), m_data.path());
        InputManager im(m_data.path(), &core);
        VirtualPad first, second, third;
        QVERIFY(first.attach("OSD/OS First Pad", 0x1209, 0x0011));
        QVERIFY(second.attach("OSD/OS Second Pad", 0x1209, 0x0012));
        QVERIFY(third.attach("OSD/OS Third Pad", 0x1209, 0x0013));
        QTRY_VERIFY(entry(im, "OSD/OS Third Pad").value("present").toBool());
        const QString k1 = entry(im, "OSD/OS First Pad").value("key").toString();
        const QString k2 = entry(im, "OSD/OS Second Pad").value("key").toString();
        const QString k3 = entry(im, "OSD/OS Third Pad").value("key").toString();
        const auto plays = [&im](const QString &key) {
            QList<int> numbers;
            const QVariantList list = im.controllerPage(key).value("players").toList();
            for (const QVariant &v : list)
                numbers << v.toInt();
            return numbers;
        };

        // By Auto, in the order they came; the list says so too.
        QCOMPARE(plays(k1), QList<int>({ 1 }));
        QCOMPARE(plays(k3), QList<int>({ 3 }));
        QCOMPARE(entry(im, "OSD/OS Second Pad").value("players").toList(), QVariantList{ 2 });
        QCOMPARE(im.controllerPage(k3).value("player").toInt(), 0);

        // The third made player 1: the first, there by Auto, goes on to the
        // next number free.
        QVariantMap result = im.setPadPlayer(k3, 1);
        QCOMPARE(outcome(result), QString("set"));
        QCOMPARE(result.value("other").toString(), QString("OSD/OS First Pad"));
        QCOMPARE(result.value("otherPlayer").toInt(), 2);
        QCOMPARE(plays(k3), QList<int>({ 1 }));
        QCOMPARE(im.controllerPage(k3).value("player").toInt(), 1);
        QCOMPARE(plays(k2), QList<int>({ 3 }));
        QCOMPARE(outcome(im.setPadPlayer(k3, 1)), QString("already"));

        // The second chosen for 4, then the third for 4: the two swap.
        QCOMPARE(outcome(im.setPadPlayer(k2, 4)), QString("set"));
        result = im.setPadPlayer(k3, 4);
        QCOMPARE(result.value("other").toString(), QString("OSD/OS Second Pad"));
        QCOMPARE(result.value("otherPlayer").toInt(), 1);
        QCOMPARE(plays(k3), QList<int>({ 4 }));
        QCOMPARE(plays(k2), QList<int>({ 1 }));
        QCOMPARE(im.controllerPage(k2).value("player").toInt(), 1);
        QCOMPARE(savedApp("players").value("4").toObject().value("key").toString(), k3);
        QCOMPARE(savedApp("players").value("1").toObject().value("key").toString(), k2);

        // Back to Auto, one pad, then all of them.
        QCOMPARE(outcome(im.setPadPlayer(k3, 0)), QString("set"));
        QCOMPARE(outcome(im.setPadPlayer(k3, 0)), QString("already"));
        QCOMPARE(im.controllerPage(k3).value("player").toInt(), 0);
        im.resetPlayers();
        QCOMPARE(plays(k1), QList<int>({ 1 }));
        QCOMPARE(plays(k2), QList<int>({ 2 }));
        QCOMPARE(savedApp("players").value("1").toObject().value("key").toString(), QString());
        // A pad away can't be given a number.
        third.detach();
        QTRY_VERIFY(!entry(im, "OSD/OS Third Pad").value("present").toBool());
        QCOMPARE(outcome(im.setPadPlayer(k3, 2)), QString("away"));
    }

    // A config.json as the version before these pages wrote it is read as it
    // is: the players' pads, a pad's own buttons and its button names (by
    // the name that version gave the style), its Arcade Buttons (a preset's,
    // and inputs set by hand), a keyboard's and a remote's extra keys, and
    // every keyboard's, a number an action. Nothing of it is lost by the
    // first change made after.
    void savedByTheVersionBefore() {
        // The pad's key, as SDL makes it here.
        QString key;
        {
            AppCore core(m_app.path(), m_data.path());
            InputManager im(m_data.path(), &core);
            VirtualPad pad;
            QVERIFY(pad.attach("OSD/OS Test Pad"));
            QTRY_VERIFY(entry(im, "OSD/OS Test Pad").value("present").toBool());
            key = entry(im, "OSD/OS Test Pad").value("key").toString();
        }
        const QString other = QStringLiteral("pad-0300aaaa5e040000ea02000000007801");
        const QString kbd = QStringLiteral("kbd-0005-046d-b342");
        const QString remote = QStringLiteral("kbd-0003-1d57-ad02");
        QByteArray config = R"({
    "app": {
        "color_scheme": "Video 1",
        "controllers": {
            "PAD": {
                "arcade": {
                    "inputs": { "1": "x", "2": "y", "3": "rightshoulder", "4": "a", "5": "b", "6": "righttrigger+",
                                "7": "leftshoulder", "8": "lefttrigger+", "coin": "back", "start": "start" },
                    "layout": "custom"
                },
                "bindings": { "a": "back", "b": "select", "x": "none" },
                "kind": "gamepad",
                "labels": "nintendo",
                "name": "OSD/OS Test Pad"
            },
            "OTHER": {
                "arcade": {
                    "inputs": { "1": "x", "2": "y", "3": "rightshoulder", "4": "a", "5": "b", "6": "righttrigger+",
                                "7": "leftshoulder", "8": "lefttrigger+", "coin": "back", "start": "start" },
                    "layout": "fighting6"
                },
                "bindings": {},
                "kind": "gamepad",
                "labels": "playstation",
                "name": "Another Pad"
            },
            "KBD": {
                "bindings": { "16777268": "select", "16777269": "left" },
                "kind": "keyboard",
                "labels": "auto",
                "name": "Logitech K380"
            },
            "REMOTE": {
                "bindings": { "16777301": "select" },
                "kind": "remote",
                "labels": "auto",
                "name": "Air Mouse Remote"
            }
        },
        "players": {
            "1": { "key": "", "name": "" },
            "2": { "key": "PAD", "name": "OSD/OS Test Pad" },
            "3": { "key": "OTHER", "name": "Another Pad" },
            "4": { "key": "", "name": "" },
            "5": { "key": "", "name": "" },
            "6": { "key": "", "name": "" }
        },
        "remote_keymap": { "up": 0, "down": 0, "left": 0, "right": 0, "select": 16777301, "back": 33554604 }
    },
    "modules": {
    }
}
)";
        config.replace("\"PAD\"", '"' + key.toLatin1() + '"').replace("\"OTHER\"", '"' + other.toLatin1() + '"');
        config.replace("\"KBD\"", '"' + kbd.toLatin1() + '"').replace("\"REMOTE\"", '"' + remote.toLatin1() + '"');
        QVERIFY(writeData("config.json", config));

        AppCore core(m_app.path(), m_data.path());
        InputManager im(m_data.path(), &core);
        QSignalSpy keys(&im, &InputManager::mpvKeyRequested);
        VirtualPad pad;
        QVERIFY(pad.attach("OSD/OS Test Pad"));
        QTRY_VERIFY(entry(im, "OSD/OS Test Pad").value("present").toBool());
        QCOMPARE(entry(im, "OSD/OS Test Pad").value("key").toString(), key);

        // The pad: its buttons, its names, its player, its Arcade Buttons.
        QCOMPARE(tap(pad, SDL_CONTROLLER_BUTTON_B, keys), QString("ENTER"));
        QCOMPARE(tap(pad, SDL_CONTROLLER_BUTTON_A, keys), QString("ESC"));
        QCOMPARE(tap(pad, SDL_CONTROLLER_BUTTON_X, keys), QString());
        QVariantMap page = im.controllerPage(key);
        QCOMPARE(page.value("labels").toString(), QString("a_right"));
        QCOMPARE(page.value("labelsInUse").toString(), QString("A right"));
        QCOMPARE(rowValue(page, "select"), QString("A"));      // the east button, by that style's name
        QCOMPARE(page.value("player").toInt(), 2);
        QCOMPARE(page.value("players").toList(), QVariantList{ 2 });
        QCOMPARE(page.value("arcade").toString(), QString("Custom"));
        QCOMPARE(im.arcadeLabel(key, "1"), QString("Y"));      // x, as that style names it
        QCOMPARE(im.arcadeLabel(key, "6"), QString("ZR"));
        QTRY_COMPARE(controllers::playerArcade().value(1).mame.value(0), QString("BUTTON3"));
        // The pad away: its player's choice, its names, its preset.
        QCOMPARE(player(im, 3).value("key").toString(), other);
        QCOMPARE(player(im, 3).value("name").toString(), QString("Another Pad"));
        QVERIFY(!player(im, 3).value("connected").toBool());
        QCOMPARE(im.controllerPage(other).value("labelsInUse").toString(), QString("Shapes"));
        QCOMPARE(im.controllerPage(other).value("arcade").toString(), QString("Fighting 6"));
        // The keyboard's and the remote's extra keys, and every keyboard's.
        QCOMPARE(rowValue(im.controllerPage(kbd), "select"), QString("Enter + Menu (all) + F5"));
        QCOMPARE(rowValue(im.controllerPage(kbd), "left"), QString("Arrow key + F6"));
        QCOMPARE(rowValue(im.controllerPage(remote), "select"), QString("Enter + Menu"));
        QCOMPARE(rowValue(im.controllerPage(InputManager::allKeys()), "select"), QString("Enter + Menu"));
        // A remote's Consumer Control button has its name on Linux, where
        // such buttons are read (InputManager::evdevKeyName()); elsewhere a
        // code from a Linux's config.json is shown by its number.
#ifdef Q_OS_LINUX
        const QString home = QStringLiteral("Home");
#else
        const QString home = QStringLiteral("Button 172");
#endif
        QCOMPARE(rowValue(im.controllerPage(InputManager::allKeys()), "back"), QStringLiteral("Esc + ") + home);
        QCOMPARE(im.keyCapture(QString(), Qt::Key_Menu, "select").value("outcome").toString(), QString("already"));

        // A layout chosen, then Custom again: the inputs set by hand are back.
        im.setArcadeLayout(key, "neogeo4");
        QCOMPARE(im.arcadeLabel(key, "1"), QString("B"));
        im.setArcadeLayout(key, "custom");
        QCOMPARE(im.arcadeLabel(key, "1"), QString("Y"));
        QCOMPARE(im.arcadeLabel(key, "8"), QString("ZL"));
        // A change to one controller leaves the others' as they were; the
        // names are written the new way, the rest as it was.
        im.setControllerLabels(key, "auto");
        const QJsonObject saved = savedControllers();
        QCOMPARE(saved.value(key).toObject().value("bindings").toObject(),
                 QJsonObject({ { "a", "back" }, { "b", "select" }, { "x", "none" } }));
        QCOMPARE(saved.value(key).toObject().value("arcade").toObject().value("inputs").toObject().value("6").toString(),
                 QString("righttrigger+"));
        QCOMPARE(saved.value(other).toObject().value("labels").toString(), QString("shapes"));
        QCOMPARE(saved.value(other).toObject().value("arcade").toObject().value("layout").toString(), QString("fighting6"));
        QCOMPARE(saved.value(kbd).toObject().value("bindings").toObject(),
                 QJsonObject({ { "16777268", "select" }, { "16777269", "left" } }));
        QCOMPARE(saved.value(remote).toObject().value("bindings").toObject(), QJsonObject({ { "16777301", "select" } }));
        QCOMPARE(savedApp("players").value("2").toObject().value("key").toString(), key);
        QCOMPARE(savedApp("players").value("3").toObject().value("key").toString(), other);
        QCOMPARE(savedApp("remote_keymap").value("select").toInt(), 16777301);
        QCOMPARE(savedApp("remote_keymap").value("back").toInt(), 33554604);
        // A keyboard has the menus' own keys: its extra Select / OK can go
        // (a remote's stays while nothing else has selected: extraKeysStay()).
        QCOMPARE(outcome(im.removeControllerInput(kbd, "16777268")), QString("set"));
        QCOMPARE(rowValue(im.controllerPage(kbd), "select"), QString("Enter + Menu (all)"));
    }

    // A held volume key, which the keyboard repeats every 80 ms or so, goes
    // to mpv as a press and then as repeats, each said to be one, so that
    // mpv holds the key down and turns the volume at its own pace
    // (MpvController::sendKey); letting go is said too. Any other media key
    // goes once per press, however long it is held.
    void heldVolumeKey() {
        AppCore core(m_app.path(), m_data.path());
        InputManager im(m_data.path(), &core);
        QSignalSpy keys(&im, &InputManager::mpvKeyRequested);
        QSignalSpy released(&im, &InputManager::mpvKeysReleased);
        QWindow window;
        const auto key = [&window](QEvent::Type type, int qtKey, bool repeat) {
            QKeyEvent event(type, qtKey, Qt::NoModifier, QString(), repeat);
            QCoreApplication::sendEvent(&window, &event);
        };
        const auto repeats = [&keys]() {
            QList<bool> flags;
            for (const QList<QVariant> &args : keys)
                flags << args.at(1).toBool();
            return flags;
        };
        key(QEvent::KeyPress, Qt::Key_VolumeUp, false);
        key(QEvent::KeyPress, Qt::Key_VolumeUp, true);
        key(QEvent::KeyPress, Qt::Key_VolumeUp, true);
        QCOMPARE(keysOf(keys), QStringList({ "VOLUME_UP", "VOLUME_UP", "VOLUME_UP" }));
        QCOMPARE(repeats(), QList<bool>({ false, true, true }));
        QCOMPARE(released.count(), 0);
        key(QEvent::KeyRelease, Qt::Key_VolumeUp, false);
        QCOMPARE(released.count(), 1);
        // Pressed again, at once.
        key(QEvent::KeyPress, Qt::Key_VolumeDown, false);
        QCOMPARE(keysOf(keys).last(), QStringLiteral("VOLUME_DOWN"));
        QCOMPARE(repeats().last(), false);
        // The other media keys once per press, and their release is nobody's.
        key(QEvent::KeyPress, Qt::Key_MediaNext, false);
        key(QEvent::KeyPress, Qt::Key_MediaNext, true);
        key(QEvent::KeyPress, Qt::Key_MediaNext, true);
        key(QEvent::KeyRelease, Qt::Key_MediaNext, false);
        QCOMPARE(keysOf(keys).count("NEXT"), 1);
        QCOMPARE(released.count(), 1);
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
        QSignalSpy captured(&im, &InputManager::controllerInputCaptured);
        im.captureController(InputManager::allKeys());
        key(0, KEY_OK + 8);
        im.captureController(QString());
        QCOMPARE(captured.count(), 1);
        const int ok = captured.at(0).at(1).toString().toInt();
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
        im.captureController(InputManager::allKeys());
        wheel(120);
        im.captureController(QString());
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

        // The next game started before the pads were opened again: they
        // stay released for it, the reclaim that was waiting dropped.
        im.releaseJoysticksForHandoff();
        im.reclaimJoysticksAfterHandoff();
        im.releaseJoysticksForHandoff();
        QTest::qWait(400);
        QCOMPARE(entry(im, "OSD/OS Handoff Pad").value("present").toBool(), false);
        im.reclaimJoysticksAfterHandoff();
        QTRY_VERIFY(entry(im, "OSD/OS Handoff Pad").value("present").toBool());
    }

    // Coin and Start held as a game ends (they quit it) reach the menus
    // neither as presses nor as releases, Start would play the game again:
    // whether the pad says they are held as it opens (an evdev pad's state)
    // or only with its next report (a Bluetooth one's, here the virtual
    // pad's buttons set after the pads are open again). A press after that
    // is one.
    void heldThroughHandoff() {
        AppCore core(m_app.path(), m_data.path());
        InputManager im(m_data.path(), &core);
        QSignalSpy keys(&im, &InputManager::mpvKeyRequested);
        VirtualPad pad;
        QVERIFY(pad.attach("OSD/OS Handoff Pad", 0x1209, 0x00ab));
        QTRY_VERIFY(entry(im, "OSD/OS Handoff Pad").value("present").toBool());

        // Held before the pads open again.
        im.releaseJoysticksForHandoff();
        pad.button(SDL_CONTROLLER_BUTTON_BACK, true);
        pad.button(SDL_CONTROLLER_BUTTON_START, true);
        im.reclaimJoysticksAfterHandoff();
        QTRY_VERIFY(entry(im, "OSD/OS Handoff Pad").value("present").toBool());
        QTest::qWait(100);
        pad.button(SDL_CONTROLLER_BUTTON_BACK, false);
        pad.button(SDL_CONTROLLER_BUTTON_START, false);
        QTest::qWait(100);
        QCOMPARE(keysOf(keys), QStringList());

        // Said to be held only once the pads are open again.
        im.releaseJoysticksForHandoff();
        im.reclaimJoysticksAfterHandoff();
        QTRY_VERIFY(entry(im, "OSD/OS Handoff Pad").value("present").toBool());
        pad.button(SDL_CONTROLLER_BUTTON_BACK, true);
        pad.button(SDL_CONTROLLER_BUTTON_START, true);
        QTest::qWait(100);
        QCOMPARE(keysOf(keys), QStringList());
        pad.button(SDL_CONTROLLER_BUTTON_BACK, false);
        pad.button(SDL_CONTROLLER_BUTTON_START, false);
        QTest::qWait(100);
        QCOMPARE(keysOf(keys), QStringList());

        // Pressed afresh once the pads have settled: a press.
        QTest::qWait(500);
        pad.button(SDL_CONTROLLER_BUTTON_START, true);
        QTRY_COMPARE(keysOf(keys), QStringList({ "SPACE" }));
        pad.button(SDL_CONTROLLER_BUTTON_START, false);
    }

    // A pad SDL hears only through its reports (its HIDAPI driver's, as a
    // Retro-Bit's) that reports only what changes says the Coin and Start
    // that quit a game are held once another of its inputs changes, however
    // long after the pads opened again: until it is heard holding nothing,
    // nothing of it but a direction counts, for three seconds at most. A pad
    // that says what is held as it opens has no such wait.
    void heldThroughHandoffReportedLate() {
        AppCore core(m_app.path(), m_data.path());
        InputManager im(m_data.path(), &core);
        QSignalSpy keys(&im, &InputManager::mpvKeyRequested);
        VirtualPad pad;
        QVERIFY(pad.attach("OSD/OS Handoff Pad", 0x1209, 0x00ac));
        QTRY_VERIFY(entry(im, "OSD/OS Handoff Pad").value("present").toBool());
        const auto handOff = [&] {
            im.releaseJoysticksForHandoff();
            im.reclaimJoysticksAfterHandoff();
            QTRY_VERIFY(entry(im, "OSD/OS Handoff Pad").value("present").toBool());
        };

        // A pad that says what is held as it opens: Start is Start once the
        // first moment is over.
        handOff();
        QTest::qWait(700);
        QCOMPARE(tap(pad, SDL_CONTROLLER_BUTTON_START, keys), QString("SPACE"));

        im.padsReportLateForTest();
        // Back and Start said to be held long after the pads opened again,
        // then let go of one after the other: neither reaches the menus, and
        // the pad, heard holding nothing, presses afresh.
        handOff();
        QTest::qWait(900);
        keys.clear();
        pad.button(SDL_CONTROLLER_BUTTON_BACK, true);
        pad.button(SDL_CONTROLLER_BUTTON_START, true);
        QTest::qWait(100);
        pad.button(SDL_CONTROLLER_BUTTON_BACK, false);
        QTest::qWait(100);
        release(pad, SDL_CONTROLLER_BUTTON_START);
        QTest::qWait(100);
        QCOMPARE(keysOf(keys), QStringList());
        QCOMPARE(tap(pad, SDL_CONTROLLER_BUTTON_START, keys), QString("SPACE"));

        // A direction counts at once, and the pad heard holding nothing
        // after it presses Start afresh.
        handOff();
        QTest::qWait(700);
        QCOMPARE(tap(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN, keys), QString("DOWN"));
        QCOMPARE(tap(pad, SDL_CONTROLLER_BUTTON_START, keys), QString("SPACE"));

        // Not heard from at all: Start is Start once the wait is over.
        handOff();
        QTest::qWait(3200);
        QCOMPARE(tap(pad, SDL_CONTROLLER_BUTTON_START, keys), QString("SPACE"));
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
        QCOMPARE(page.value("labelsInUse").toString(), QString("Numbers"));
        QCOMPARE(rowValue(page, "back"), QString("2, COIN"));
        // A panel's buttons 5 and 6 (a pad's shoulders) are buttons like the
        // rest: the joystick goes left and right.
        QCOMPARE(rowValue(page, "left"), QString("D-PAD ◄, JOYSTICK ◄"));
        QCOMPARE(tap(p1, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, keys), QString());

        // Away with no settings of its own, it is still the arcade stick it
        // was: its page names and labels it so, and what is set there keeps
        // its name and kind, its labels back to Auto and set again too.
        const QString key2 = entry(im, "OSD/OS JAMMA P2").value("key").toString();
        p2.detach();
        QTRY_VERIFY(entry(im, "OSD/OS JAMMA P2").isEmpty());
        QCOMPARE(im.controllerPage(key2).value("name").toString(), QString("OSD/OS JAMMA P2"));
        QCOMPARE(im.controllerPage(key2).value("labelsInUse").toString(), QString("Numbers"));
        im.setControllerLabels(key2, "a_right");
        QCOMPARE(entry(im, "OSD/OS JAMMA P2").value("kind").toString(), QString("arcade"));
        im.setControllerLabels(key2, "auto");
        QVERIFY(entry(im, "OSD/OS JAMMA P2").isEmpty());
        im.setControllerLabels(key2, "a_below");
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

        // P1's settings from before: its labels (as that version named them), and player 1's choice.
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
        QCOMPARE(im.controllerPage(key1).value("labelsInUse").toString(), QString("A below"));
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

    // Cabinet Lock's unlock buttons: chosen by holding them for two seconds,
    // four at most, nothing acting meanwhile, and named by the players their
    // pads play. With the lock on, a press of one that isn't a direction
    // waits a moment for the rest before it acts, and another input lets
    // what waited go first; all of them held for three seconds ask to
    // unlock, and none acts, nor does a release. Off, nothing waits. Keys
    // the same, the ones held back given the window later, in order.
    void cabinetLock() {
        using B = SDL_GameControllerButton;
        AppCore core(m_app.path(), m_data.path());
        InputManager im(m_data.path(), &core);
        QSignalSpy keys(&im, &InputManager::mpvKeyRequested);
        QSignalSpy held(&im, &InputManager::unlockButtonsHeld);
        QSignalSpy ended(&im, &InputManager::unlockCaptureEnded);
        QSignalSpy unlock(&im, &InputManager::unlockRequested);
        VirtualPad p1, p2;
        QVERIFY(p1.attach("OSD/OS JAMMA P1", 0x0001, 0x0001));
        QVERIFY(p2.attach("OSD/OS JAMMA P2", 0x0001, 0x0002));
        QTRY_VERIFY(entry(im, "OSD/OS JAMMA P1").value("present").toBool());
        QTRY_VERIFY(entry(im, "OSD/OS JAMMA P2").value("present").toBool());
        QTRY_COMPARE(player(im, 2).value("name").toString(), QString("OSD/OS JAMMA P2"));
        QCOMPARE(im.unlockButtons(), QString());

        // Five held are too many; one let go, the four left are kept two
        // seconds on.
        im.captureUnlockButtons(true);
        for (const B b : { SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_BACK })
            hold(p1, b);
        hold(p2, SDL_CONTROLLER_BUTTON_START);
        hold(p2, SDL_CONTROLLER_BUTTON_A);
        QCOMPARE(held.last().at(0).toString(), QString("P1 START + P1 D-PAD ▲ + P1 COIN + P2 START + P2 1"));
        QVERIFY(held.last().at(1).toBool());
        QTest::qWait(2300);
        QCOMPARE(ended.count(), 0);
        release(p2, SDL_CONTROLLER_BUTTON_A);
        QVERIFY(!held.last().at(1).toBool());
        QVERIFY(ended.wait(3000));
        QCOMPARE(ended.at(0).at(0).toBool(), true);
        QCOMPARE(im.unlockButtons(), QString("P1 START + P1 D-PAD ▲ + P1 COIN + P2 START"));
        const QJsonArray kept = [this] {
            QFile f(m_data.path() + "/config.json");
            return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object().value("app")
                                                     .toObject().value("unlock_buttons").toArray()
                                               : QJsonArray();
        }();
        QCOMPARE(kept.size(), 4);
        QCOMPARE(kept.at(1).toObject().value("input").toString(), QString("p1/dpup"));
        QCOMPARE(kept.at(3).toObject().value("input").toString(), QString("p2/start"));
        for (const B b : { SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_BACK })
            release(p1, b);
        release(p2, SDL_CONTROLLER_BUTTON_START);
        QCOMPARE(keysOf(keys), QStringList());

        // Locked: START waits for the rest, then acts; a direction acts at
        // once. How long a press takes to act, held on.
        const auto actsAfter = [&keys](VirtualPad &pad, B b) -> qint64 {
            keys.clear();
            QElapsedTimer since;
            since.start();
            hold(pad, b);
            if (keys.isEmpty() && !keys.wait(1000))
                return -1;
            return since.elapsed();
        };
        QVERIFY(core.save_setting(QString(), "cabinet_lock", "On"));
        QVERIFY(actsAfter(p1, SDL_CONTROLLER_BUTTON_START) >= 250);
        QCOMPARE(keysOf(keys), QStringList({ "SPACE" }));
        release(p1, SDL_CONTROLLER_BUTTON_START);
        const qint64 up = actsAfter(p1, SDL_CONTROLLER_BUTTON_DPAD_UP);
        QVERIFY2(up >= 0 && up < 250, qPrintable(QString::number(up)));
        release(p1, SDL_CONTROLLER_BUTTON_DPAD_UP);
        // Another button lets START go first.
        keys.clear();
        hold(p1, SDL_CONTROLLER_BUTTON_START);
        hold(p1, SDL_CONTROLLER_BUTTON_A);
        QTRY_COMPARE(keysOf(keys), QStringList({ "SPACE", "ENTER" }));
        release(p1, SDL_CONTROLLER_BUTTON_A);
        release(p1, SDL_CONTROLLER_BUTTON_START);

        // All four held for three seconds: unlock, and nothing else.
        const auto holdAll = [&] {
            for (const B b : { SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_BACK })
                p1.button(b, true);
            p2.button(SDL_CONTROLLER_BUTTON_START, true);
            QVERIFY(QTest::qWaitFor([&] {
                return SDL_JoystickGetButton(p1.joystick, SDL_CONTROLLER_BUTTON_DPAD_UP)
                    && SDL_JoystickGetButton(p2.joystick, SDL_CONTROLLER_BUTTON_START);
            }, 1000));
        };
        keys.clear();
        holdAll();
        QTest::qWait(2000);
        QCOMPARE(unlock.count(), 0);
        QVERIFY(unlock.wait(2500));
        // One let go before then: no unlock, until it is held again.
        release(p2, SDL_CONTROLLER_BUTTON_START);
        hold(p2, SDL_CONTROLLER_BUTTON_START);
        QTest::qWait(1000);
        release(p2, SDL_CONTROLLER_BUTTON_START);
        QTest::qWait(2500);
        QCOMPARE(unlock.count(), 1);
        hold(p2, SDL_CONTROLLER_BUTTON_START);
        QVERIFY(unlock.wait(4000));
        for (const B b : { SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_BACK })
            release(p1, b);
        release(p2, SDL_CONTROLLER_BUTTON_START);
        QTest::qWait(400);
        QCOMPARE(keysOf(keys), QStringList());
        QCOMPARE(unlock.count(), 2);

        // Off: nothing waits.
        QVERIFY(core.save_setting(QString(), "cabinet_lock", "Off"));
        const qint64 start = actsAfter(p1, SDL_CONTROLLER_BUTTON_START);
        QVERIFY2(start >= 0 && start < 250, qPrintable(QString::number(start)));
        release(p1, SDL_CONTROLLER_BUTTON_START);

        // Keys: 1 and 2 chosen, their releases kept from the window too.
        QQuickWindow window;
        im.setTargetWindow(&window);
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
        const auto send = [&window](QEvent::Type type, int key, bool repeat = false) {
            QKeyEvent event(type, key, Qt::NoModifier, QString(), repeat);
            QCoreApplication::sendEvent(&window, &event);
        };
        ended.clear();
        im.captureUnlockButtons(true);
        send(QEvent::KeyPress, Qt::Key_1);
        send(QEvent::KeyPress, Qt::Key_2);
        send(QEvent::KeyPress, Qt::Key_2, true);
        QVERIFY(ended.wait(3000));
        QCOMPARE(im.unlockButtons(), QString("1 + 2"));
        send(QEvent::KeyRelease, Qt::Key_1);
        send(QEvent::KeyRelease, Qt::Key_2);
        QCOMPARE(catcher.pressed, QList<int>());
        QCOMPARE(catcher.released, QList<int>());

        // Locked: 1 waits, then the window has it, and its release after it;
        // 3 coming meanwhile goes after it.
        QVERIFY(core.save_setting(QString(), "cabinet_lock", "On"));
        send(QEvent::KeyPress, Qt::Key_1);
        QCOMPARE(catcher.pressed, QList<int>());
        QTRY_COMPARE(catcher.pressed, QList<int>({ Qt::Key_1 }));
        send(QEvent::KeyRelease, Qt::Key_1);
        QCOMPARE(catcher.released, QList<int>({ Qt::Key_1 }));
        catcher.pressed.clear();
        catcher.released.clear();
        send(QEvent::KeyPress, Qt::Key_1);
        send(QEvent::KeyPress, Qt::Key_3);
        send(QEvent::KeyRelease, Qt::Key_1);
        QCOMPARE(catcher.pressed, QList<int>());
        QTRY_COMPARE(catcher.released, QList<int>({ Qt::Key_1 }));
        QCOMPARE(catcher.pressed, QList<int>({ Qt::Key_1, Qt::Key_3 }));
        send(QEvent::KeyRelease, Qt::Key_3);
        // Both held: unlock, and the window has neither.
        catcher.pressed.clear();
        catcher.released.clear();
        unlock.clear();
        send(QEvent::KeyPress, Qt::Key_1);
        send(QEvent::KeyPress, Qt::Key_2);
        send(QEvent::KeyPress, Qt::Key_1, true);
        QVERIFY(unlock.wait(4000));
        send(QEvent::KeyRelease, Qt::Key_2);
        send(QEvent::KeyRelease, Qt::Key_1);
        QTest::qWait(400);
        QCOMPARE(catcher.pressed, QList<int>());
        QCOMPARE(catcher.released, QList<int>());
        QCOMPARE(unlock.count(), 1);

        // Held three seconds while the app was too busy to see the time go
        // by, and let go before it could: they count all the same, once.
        unlock.clear();
        send(QEvent::KeyPress, Qt::Key_1);
        send(QEvent::KeyPress, Qt::Key_2);
        QThread::msleep(3100);
        send(QEvent::KeyRelease, Qt::Key_2);
        QCOMPARE(unlock.count(), 1);
        send(QEvent::KeyRelease, Qt::Key_1);
        QTest::qWait(400);
        QCOMPARE(unlock.count(), 1);
        QCOMPARE(catcher.pressed, QList<int>());
        QCOMPARE(catcher.released, QList<int>());

        // A wait ended from its page says nothing was kept.
        ended.clear();
        im.captureUnlockButtons(true);
        im.captureUnlockButtons(false);
        QCOMPARE(ended.count(), 1);
        QVERIFY(!ended.at(0).at(0).toBool());
        window.removeEventFilter(&catcher);
        im.setTargetWindow(nullptr);
    }
};

QTEST_MAIN(ControllersTest)
#include "controllers_test.moc"
