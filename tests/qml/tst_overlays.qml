import QtQuick
import QtTest
import Components

// What comes over a view, each on its own: the on-screen keyboard
// (views/Components/OnScreenKeyboard.qml), whose keys stay the keys they are
// as SHIFT and SHOW change what they say, whose line keeps its end in view
// however long it is, and which Backspace leaves once nothing is typed; and
// the choice window (views/Components/ChoiceOverlay.qml). Neither draws
// anything before it first shows. And the screen saver
// (views/Components/ScreenSaver.qml) over a view and a notice, laid out as
// Main.qml lays them: the keys its own while it is up, and back to whoever
// has them under it as it goes.
Rectangle {
    id: root
    color: surfaceColor
    width: 640
    height: 480
    // Main.qml's, as the views and their components read them.
    property real sw: width
    property real sh: height
    property real px: 2
    property string globalFont: "monospace"
    property color primaryColor: "white"
    property color surfaceColor: "black"
    property var skin: ({})
    property bool hintBar: true
    property bool helpLine: true
    property var statusIcons: []
    property bool framed: false
    readonly property real paneX: sw * 0.115625
    readonly property real paneWidth: sw * 0.76875
    readonly property real hintRoom: 0
    readonly property real helpRoom: 0
    property rect contentBox: Qt.rect(sw * 0.115625, sh * 0.11875, sw * 0.76875, sh * 0.7770833)
    property rect osdWindow: contentBox
    property string osdBackground: "Full"
    property string osdFrame: "On"
    property var backgroundEffect: ({})
    property string backgroundShader: ""
    property bool effectsRest: true
    property real fxTime: 0
    property var selector: null
    property var hints: ({ back: "[ESC]", navigate: "[▲▼]", arrows: "[▲▼◄►]", change: "[◄►]",
                           browse: "[►]", select: "[ENTER]", play_pause: "[SPACE]" })
    function selectorShown(item, shown) {}
    function coverShown(item, shown) {}
    function skinIcon(url) { return url }

    Loader {
        id: loader
        anchors.fill: parent
        focus: true
    }

    // The screen saver as Main.qml has it: over the views' loader, whose
    // view has a question it asks by itself (the update's, as its download
    // ends), and over a notice that takes the keys as it comes (OSD/OS
    // Setup's, at startup).
    property bool saverUp: false
    property int viewKeys: 0
    property int noticeKeys: 0
    property int quits: 0
    Loader {
        id: viewLoader
        anchors.fill: parent
        active: false
        // As Main.qml's moduleLoader gives a view the keys.
        onLoaded: item.forceActiveFocus()
        sourceComponent: FocusScope {
            property alias content: content
            property alias question: question
            Item {
                id: content
                anchors.fill: parent
                focus: true
                Keys.onPressed: function(event) { root.viewKeys++; event.accepted = true }
            }
            ChoiceOverlay {
                id: question
                anchors.fill: parent
                promptText: "Install update?"
                choices: [{ label: "Apply", action: "apply" }, { label: "Discard", action: "discard" }]
                onClosed: content.forceActiveFocus()
            }
        }
    }
    Loader {
        id: noticeLoader
        anchors.fill: parent
        z: 9400
        active: false
        sourceComponent: FocusScope {
            Keys.onPressed: function(event) { root.noticeKeys++; event.accepted = true }
        }
        onLoaded: item.forceActiveFocus()
    }
    ScreenSaver {
        id: saver
        anchors.fill: parent
        z: 9999
        shown: root.saverUp
        keysTo: [noticeLoader.item, viewLoader]
        onWoken: root.saverUp = false
        onQuitAsked: root.quits++
    }

    SignalSpy {
        id: canceledSpy
        signalName: "canceled"
    }
    SignalSpy {
        id: activatedSpy
        signalName: "activated"
    }
    SignalSpy {
        id: closedSpy
        signalName: "closed"
    }

    // How many items an item holds, all the way down.
    function itemsIn(item) {
        var count = 0
        for (var i = 0; i < item.children.length; i++)
            count += 1 + itemsIn(item.children[i])
        return count
    }
    // The PromptScreen an item holds, or null.
    function promptIn(item) {
        for (var i = 0; i < item.children.length; i++) {
            var kid = item.children[i]
            if (kid.maxChoices !== undefined && typeof kid.hoverRow === "function")
                return kid
            var deeper = promptIn(kid)
            if (deeper)
                return deeper
        }
        return null
    }

    TestCase {
        name: "OnScreenKeyboard"
        when: windowShown

        function cleanup() {
            loader.source = ""
        }

        // The keyboard on its own, as a view carries it.
        function keyboardWith(properties) {
            loader.setSource(Qt.resolvedUrl("../../views/Components/OnScreenKeyboard.qml"), properties)
            verify(loader.item !== null, "loaded OnScreenKeyboard.qml")
            return loader.item
        }
        // Every key drawn, row by row.
        function keysOf(keyboard) {
            var keys = []
            for (var r = 0; r < keyboard.rows.length; r++) {
                keys.push([])
                for (var c = 0; c < keyboard.rows[r].length; c++)
                    keys[r].push(keyboard.keyItem(r, c))
            }
            return keys
        }
        // What the key at a row and column says.
        function saidBy(keyboard, r, c) {
            var key = keyboard.keyItem(r, c)
            for (var i = 0; key && i < key.children.length; i++) {
                if (key.children[i].text !== undefined)
                    return key.children[i].text
            }
            return null
        }
        function sameKeys(keyboard, keys) {
            for (var r = 0; r < keys.length; r++) {
                for (var c = 0; c < keys[r].length; c++) {
                    if (keys[r][c] === null || keyboard.keyItem(r, c) !== keys[r][c])
                        return false
                }
            }
            return true
        }

        // SHIFT changes what the keys say, the letters and the second row of
        // signs, and SHOW its own key: every key stays the item it was, none
        // made again, and the box where it was.
        function test_shiftKeepsTheKeys() {
            var keyboard = keyboardWith({ caseSensitive: true, masked: true })
            keyboard.open("")
            var last = keyboard.rows.length - 1
            var keys = keysOf(keyboard)
            compare(saidBy(keyboard, 0, 0), "a")
            compare(saidBy(keyboard, 5, 2), ",")
            keyboard.moveTo(last, 0)
            compare(saidBy(keyboard, last, 0), "SHIFT")
            keyClick(Qt.Key_Return)
            verify(keyboard.shifted)
            compare(saidBy(keyboard, 0, 0), "A")
            compare(saidBy(keyboard, 5, 2), "^")
            verify(sameKeys(keyboard, keys), "the same keys after SHIFT")
            compare(keyboard.row, last)
            compare(keyboard.col, 0)
            keyClick(Qt.Key_Return)
            verify(!keyboard.shifted)
            compare(saidBy(keyboard, 0, 0), "a")
            verify(sameKeys(keyboard, keys), "the same keys after SHIFT again")
            // SHOW becomes HIDE on its own key.
            keyboard.moveTo(last, 3)
            compare(saidBy(keyboard, last, 3), "SHOW")
            keyClick(Qt.Key_Return)
            verify(keyboard.revealed)
            compare(saidBy(keyboard, last, 3), "HIDE")
            verify(sameKeys(keyboard, keys), "the same keys after SHOW")
            compare(keyboard.row, last)
            compare(keyboard.col, 3)
            // A key still types what it says.
            keyboard.moveTo(1, 0)
            keyClick(Qt.Key_Return)
            compare(keyboard.text, "k")
        }

        // Nothing of it is made with its view: the line and the keys come
        // as it first shows, are kept once it is closed, and are the same
        // ones when it opens again. One a view shows from the start (the
        // Arcade screen's Search, as its first filter) has them at once.
        function test_madeAsItFirstShows() {
            var keyboard = keyboardWith({ caseSensitive: true })
            verify(!keyboard.visible)
            // The loader that will hold them, and no more.
            compare(root.itemsIn(keyboard), 1)
            compare(keyboard.keyItem(0, 0), null)
            keyboard.open("")
            verify(root.itemsIn(keyboard) > 100)
            var keys = keysOf(keyboard)
            compare(saidBy(keyboard, 0, 0), "a")
            keyClick(Qt.Key_Escape)
            verify(!keyboard.visible)
            verify(sameKeys(keyboard, keys), "kept once closed")
            keyboard.open("abc")
            verify(sameKeys(keyboard, keys), "the same keys opened again")
            compare(lineOf(keyboard, "abc").text.text, "abc")

            var shown = keyboardWith({ inline: true, visible: true })
            verify(shown.keyItem(0, 0) !== null)
        }

        // The line typed as it is drawn (the stars of a hidden one): its
        // Text, in a row with the block after it, in the box that clips it.
        function lineOf(keyboard, shown) {
            var found = null
            var walk = function(item) {
                if (item.text === shown && item.elide !== undefined)
                    found = item
                for (var i = 0; i < item.children.length && !found; i++)
                    walk(item.children[i])
            }
            walk(keyboard)
            verify(found !== null, "the line typed, " + shown)
            var box = found.parent.parent
            verify(box.clip)
            return { text: found, block: found.parent.children[1], box: box }
        }
        function type(text) {
            for (var i = 0; i < text.length; i++)
                keyClick(text.charAt(i))
        }

        // A short line starts at the box's left edge. One longer than the
        // box (a password, 64 characters; an address) keeps its end in view,
        // at the box's right edge with room for the block after it, its
        // start running out at the left; deleted back to a short one, it
        // starts at the left edge again. A hidden line's stars the same.
        function test_longLineKeepsItsEnd_data() {
            return [{ tag: "as typed", masked: false }, { tag: "hidden", masked: true }]
        }
        function test_longLineKeepsItsEnd(data) {
            var keyboard = keyboardWith({ caseSensitive: true, masked: data.masked, maxLength: 64 })
            keyboard.open("")
            var shown = function() { return data.masked ? "*".repeat(keyboard.text.length) : keyboard.text }
            var margin = root.sw * 0.0125
            type("ab")
            var line = lineOf(keyboard, shown())
            compare(line.text.mapToItem(line.box, 0, 0).x, margin)
            type("cdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz01")
            compare(keyboard.text.length, 64)
            line = lineOf(keyboard, shown())
            verify(line.text.width > line.box.width, "a line longer than its box")
            // Its end a block's width short of the box's right edge, for the
            // block that comes and goes after it.
            verify(line.block.width > 0)
            fuzzyCompare(line.text.mapToItem(line.box, line.text.width, 0).x,
                         line.box.width - margin - line.block.width, 0.01)
            verify(line.text.mapToItem(line.box, 0, 0).x < 0)
            for (var i = 0; i < 60; i++)
                keyClick(Qt.Key_Backspace)
            compare(keyboard.text, "abcd")
            line = lineOf(keyboard, shown())
            compare(line.text.mapToItem(line.box, 0, 0).x, margin)
        }

        // Backspace deletes a letter while there is one, and leaves the
        // keyboard up; with none typed it is back: the keyboard closes,
        // canceled. Held, it deletes and no more, so a line deleted by
        // holding it stays up. Delete only ever deletes.
        function test_backspaceIsBackOnceNothingIsTyped() {
            var keyboard = keyboardWith({})
            canceledSpy.target = keyboard
            canceledSpy.clear()
            keyboard.open("AB")
            keyClick(Qt.Key_Backspace)
            compare(keyboard.text, "A")
            keyClick(Qt.Key_Backspace)
            compare(keyboard.text, "")
            verify(keyboard.visible)
            keyClick(Qt.Key_Delete)
            verify(keyboard.visible)
            // A held key's repeat, as Keys.onPressed hands it on.
            keyboard.backspace(true)
            verify(keyboard.visible)
            compare(canceledSpy.count, 0)
            keyClick(Qt.Key_Backspace)
            verify(!keyboard.visible)
            verify(!keyboard.activeFocus)
            compare(canceledSpy.count, 1)
            // Esc, as the hint bar says, with something typed too.
            keyboard.open("AB")
            keyClick(Qt.Key_Escape)
            verify(!keyboard.visible)
            compare(canceledSpy.count, 2)
        }

        // In another view's window (the Arcade screen's Search), back is the
        // view's to act on: Backspace with nothing typed says so and leaves
        // the keys up, as Esc does.
        function test_backspaceInAnotherViewsWindow() {
            var keyboard = keyboardWith({ inline: true, allowEmpty: true, visible: true })
            canceledSpy.target = keyboard
            canceledSpy.clear()
            keyboard.forceActiveFocus()
            keyClick(Qt.Key_A)
            keyClick(Qt.Key_Backspace)
            compare(keyboard.text, "")
            compare(canceledSpy.count, 0)
            keyClick(Qt.Key_Backspace)
            compare(canceledSpy.count, 1)
            verify(keyboard.visible)
        }
    }

    TestCase {
        name: "ChoiceOverlay"
        when: windowShown

        function cleanup() {
            loader.source = ""
        }

        // The list of answers a PromptScreen draws.
        function answersOf(prompt) {
            var answers = null
            var find = function(item) {
                if (typeof item.keep === "function" && typeof item.itemAtIndex === "function")
                    answers = item
                for (var i = 0; i < item.children.length && !answers; i++)
                    find(item.children[i])
            }
            find(prompt)
            return answers
        }

        // Its window (PromptScreen) is not made with its view: it comes as
        // the question is first asked, with all its answers there at once,
        // takes the keys as ever, stays once closed, and is the same window
        // when the question is asked again.
        function test_madeAsItFirstShows() {
            loader.setSource(Qt.resolvedUrl("../../views/Components/ChoiceOverlay.qml"),
                             { promptText: "Install update?", subtitleText: "v2.0",
                               choices: [{ label: "Apply", action: "apply" }, { label: "Discard", action: "discard" }] })
            var overlay = loader.item
            verify(overlay !== null, "loaded ChoiceOverlay.qml")
            activatedSpy.target = overlay
            activatedSpy.clear()
            closedSpy.target = overlay
            closedSpy.clear()
            verify(!overlay.visible)
            compare(root.promptIn(overlay), null)
            // The loader that will hold it, and no more.
            compare(root.itemsIn(overlay), 1)

            overlay.open()
            var prompt = root.promptIn(overlay)
            verify(prompt !== null)
            verify(prompt.visible)
            compare(prompt.title, "Install update?")
            compare(prompt.message, "v2.0")
            compare(prompt.width, root.width)
            compare(prompt.height, root.height)
            // Both answers drawn already, the cursor's the first.
            var answers = answersOf(prompt)
            verify(answers !== null)
            verify(answers.itemAtIndex(0) !== null && answers.itemAtIndex(1) !== null)
            verify(answers.itemAtIndex(0).current)
            keyClick(Qt.Key_Down)
            compare(prompt.currentIndex, 1)
            verify(answers.itemAtIndex(1).current)
            keyClick(Qt.Key_Return)
            compare(activatedSpy.count, 1)
            compare(activatedSpy.signalArguments[0][0], "discard")
            compare(closedSpy.count, 1)
            verify(!overlay.visible)
            compare(root.promptIn(overlay), prompt)

            // Asked again, with other answers: the same window, on the first.
            overlay.choices = [{ label: "Yes", action: "yes" }, { label: "No", action: "no" },
                               { label: "Later", action: "later" }]
            overlay.open()
            compare(root.promptIn(overlay), prompt)
            compare(prompt.choices.length, 3)
            compare(prompt.currentIndex, 0)
            keyClick(Qt.Key_Escape)
            compare(closedSpy.count, 2)
            compare(activatedSpy.count, 1)
        }
    }

    TestCase {
        name: "ScreenSaver"
        when: windowShown

        property var view: null

        function init() {
            root.saverUp = false
            noticeLoader.active = false
            viewLoader.active = true
            view = viewLoader.item
            verify(view !== null)
            verify(view.content.activeFocus)
            activatedSpy.target = view.question
            activatedSpy.clear()
            root.viewKeys = 0
            root.noticeKeys = 0
            root.quits = 0
        }
        function cleanup() {
            root.saverUp = false
            noticeLoader.active = false
            viewLoader.active = false
        }

        // Over a view: the first key wakes it and goes no further, the next
        // is the view's.
        function test_aKeyOnlyWakesIt() {
            verify(!saver.visible)
            root.saverUp = true
            verify(saver.visible)
            verify(saver.activeFocus)
            keyClick(Qt.Key_Return)
            verify(!root.saverUp)
            verify(!saver.visible)
            compare(root.viewKeys, 0)
            verify(view.content.activeFocus)
            keyClick(Qt.Key_Return)
            compare(root.viewKeys, 1)
        }

        // A question the view asks while the saver is up (the update's, as
        // its download ends) opens under it without the keys: the saver
        // keeps them, the key that wakes it answers nothing, and the
        // question has them once the saver has gone.
        function test_aQuestionAskedUnderIt() {
            root.saverUp = true
            view.question.open()
            verify(view.question.visible)
            verify(saver.activeFocus, "the keys still the saver's")
            verify(!view.question.activeFocus)
            keyClick(Qt.Key_Return)
            verify(!root.saverUp, "a key still wakes it")
            compare(activatedSpy.count, 0)
            verify(view.question.visible)
            verify(view.question.activeFocus, "the keys the question's")
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(activatedSpy.count, 1)
            compare(activatedSpy.signalArguments[0][0], "discard")
            compare(root.viewKeys, 0)
            // Answered, the keys are the view's again.
            keyClick(Qt.Key_Left)
            compare(root.viewKeys, 1)
        }

        // A notice shown at startup, over the views, the saver come and
        // gone: the notice has the keys again, not the view under it. Once
        // it has gone, they go back to the view.
        function test_aNoticeUnderIt() {
            noticeLoader.active = true
            verify(noticeLoader.item.activeFocus)
            root.saverUp = true
            verify(saver.activeFocus)
            keyClick(Qt.Key_Return)
            verify(!root.saverUp)
            compare(root.noticeKeys, 0)
            verify(noticeLoader.item.activeFocus, "the keys the notice's")
            keyClick(Qt.Key_Return)
            compare(root.noticeKeys, 1)
            compare(root.viewKeys, 0)
            noticeLoader.active = false
            viewLoader.forceActiveFocus()
            root.saverUp = true
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Return)
            compare(root.viewKeys, 1)
        }

        // A notice that comes while the saver is up takes the keys as it is
        // made: the saver has them back at once, and the notice after it.
        function test_aNoticeComesUnderIt() {
            root.saverUp = true
            noticeLoader.active = true
            verify(noticeLoader.item !== null)
            verify(saver.activeFocus, "the keys still the saver's")
            keyClick(Qt.Key_Escape)
            verify(!root.saverUp)
            compare(root.noticeKeys, 0)
            keyClick(Qt.Key_Return)
            compare(root.noticeKeys, 1)
            compare(root.viewKeys, 0)
        }

        // Put away without a key (the mouse moved, a video started): the
        // keys go back the same way.
        function test_putAwayWithoutAKey() {
            root.saverUp = true
            view.question.open()
            root.saverUp = false
            verify(view.question.activeFocus)
            keyClick(Qt.Key_Return)
            compare(activatedSpy.count, 1)
            compare(activatedSpy.signalArguments[0][0], "apply")
        }

        // Ctrl+Q as a keyboard sends it, Ctrl and then Q: Ctrl is a key like
        // any other and wakes the saver, and the Q after it is the view's
        // (Main.qml's loader quits on it). The saver asks its host to quit
        // (quitAsked) only for a Ctrl+Q that comes as one key.
        function test_ctrlQ() {
            root.saverUp = true
            keyClick(Qt.Key_Q, Qt.ControlModifier)
            verify(!root.saverUp)
            compare(root.quits, 0)
            compare(root.viewKeys, 1)
        }
    }
}
