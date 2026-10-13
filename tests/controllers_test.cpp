#include "AppCore.h"
#include "input/ControllerProfile.h"
#include "input/InputManager.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <QWindow>

#include <SDL.h>
#include <memory>
#ifdef Q_OS_LINUX
#include <linux/input-event-codes.h>
#endif

// Settings → Controls, each controller apart. First what needs no device
// (ControllerProfile): what a pad's buttons are called in each label style,
// a profile as config.json keeps it, which /dev/input nodes are keyboards and
// remotes, and whose a key is. Then InputManager with pads SDL makes up (its
// virtual joysticks): each pad's own map over the shared one (the defaults
// and input.cfg), its presses reported instead of acting while its page waits
// for one, its labels in the hints, a model of pad plugged in twice, the JAMMA
// board's sticks, the players' pads, and all of it kept in config.json for the
// next start.
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
};

QTEST_MAIN(ControllersTest)
#include "controllers_test.moc"
