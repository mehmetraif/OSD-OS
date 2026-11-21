import QtQuick
import QtTest
import Components
import OSDOS.Testing
import "../../views/Components/Hover.js" as Hover

// What comes over a view, each on its own: the on-screen keyboard
// (views/Components/OnScreenKeyboard.qml), whose keys stay the keys they are
// as SHIFT and SHOW change what they say, whose line keeps its end in view
// however long it is, and which Backspace leaves once nothing is typed; and
// the choice window (views/Components/ChoiceOverlay.qml). Neither draws
// anything before it first shows. And the screen saver
// (views/Components/ScreenSaver.qml) over a view and a notice, laid out as
// Main.qml lays them: the keys its own while it is up, and back to whoever
// has them under it as it goes. And the loading screen's letters, bleeding
// in the colours' second ink.
StandInRoot {
    id: root

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

    // A question on its own, as a host that keeps its keys draws it.
    Component {
        id: question
        PromptScreen {
            title: "Keep this display output?"
            message: "HDMI 720p"
            choices: ["Keep", "Switch Back"]
        }
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
            root.framed = false
            root.setFrame([], 0, false, false)
            root.menuLayout = "Standard"
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
            canceledSpy.target = overlay
            canceledSpy.clear()
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
            compare(canceledSpy.count, 0)
            verify(!overlay.visible)
            compare(root.promptIn(overlay), prompt)

            // Asked again, with other answers: the same window, on the first,
            // ▲ ▼ round both ends; back closes it, canceled.
            overlay.choices = [{ label: "Yes", action: "yes" }, { label: "No", action: "no" },
                               { label: "Later", action: "later" }]
            overlay.open()
            compare(root.promptIn(overlay), prompt)
            compare(prompt.choices.length, 3)
            compare(prompt.currentIndex, 0)
            keyClick(Qt.Key_Up)
            compare(prompt.currentIndex, 2)
            keyClick(Qt.Key_Down)
            compare(prompt.currentIndex, 0)
            keyClick(Qt.Key_Escape)
            compare(closedSpy.count, 2)
            compare(canceledSpy.count, 1)
            compare(activatedSpy.count, 1)
        }

        // The frame's column of icons a PromptScreen draws again in
        // Camcorder's frame (an IconColumn), or null.
        function columnOf(prompt) {
            var column = null
            var find = function(item) {
                if (typeof item.active === "boolean" && item.count !== undefined && item.model !== undefined)
                    column = item
                for (var i = 0; i < item.children.length && !column; i++)
                    find(item.children[i])
            }
            find(prompt)
            return column
        }
        // The mouse on an item, at its middle unless told where, as
        // Main.qml's followPointer() has it.
        function hover(item, x, y) {
            var p = item.mapToItem(root, x === undefined ? item.width / 2 : x, y === undefined ? item.height / 2 : y)
            Hover.follow(root, root.Window.activeFocusItem, p.x, p.y)
        }

        // The mouse moves the cursor to the answer it is over, and nowhere
        // from anything else: in Camcorder's frame the window draws the
        // frame's column again beside its answers, and the mouse over an
        // icon there leaves the cursor on its answer (the first may be Power
        // Off, and a click is select). In Standard there is no column.
        function test_mouseOverTheFramesColumn_data() {
            return [{ tag: "Standard", layout: "Standard", framed: false },
                    { tag: "Camcorder", layout: "Camcorder", framed: true }]
        }
        function test_mouseOverTheFramesColumn(data) {
            root.menuLayout = data.layout
            root.setFrame([{ icon: "tv" }, { icon: "gear" }, { icon: "eject" }], 2, false, false)
            root.framed = data.framed
            loader.setSource(Qt.resolvedUrl("../../views/Components/ChoiceOverlay.qml"),
                             { promptText: "Quit OSD/OS?",
                               choices: [{ label: "Power Off", action: "poweroff" }, { label: "Restart", action: "restart" },
                                         { label: "Cancel", action: "cancel" }] })
            var overlay = loader.item
            verify(overlay !== null, "loaded ChoiceOverlay.qml")
            overlay.open()
            var prompt = root.promptIn(overlay)
            var answers = answersOf(prompt)
            keyClick(Qt.Key_Up)
            compare(overlay.choiceIndex, 2)
            var column = columnOf(prompt)
            compare(column !== null, data.framed)
            if (column) {
                compare(column.count, 3)
                // Each icon in turn, the first first.
                for (var i = 0; i < 3; i++) {
                    hover(column, column.width / 2, (i + 0.5) * root.menuRowHeight)
                    compare(overlay.choiceIndex, 2, "the cursor left on its answer, the mouse on icon " + i)
                }
                // The answers beside the column, in the pane.
                verify(answers.mapToItem(root, 0, 0).x >= column.mapToItem(root, column.width, 0).x)
            }
            // An answer: the cursor's.
            hover(answers.itemAtIndex(0))
            compare(overlay.choiceIndex, 0)
            hover(answers.itemAtIndex(1))
            compare(overlay.choiceIndex, 1)
            // The title bar, and the room under the answers: nowhere.
            hover(prompt, prompt.width / 2, root.sh * 0.15)
            hover(prompt, prompt.width / 2, root.sh * 0.8)
            compare(overlay.choiceIndex, 1)
        }

        // A question asked over the whole screen whatever the layout
        // (`inLayout` off: a new display output's, before the menus): in
        // Camcorder's frame it draws no column and lies across the window,
        // not in the pane, as in Standard and in a monitor layout. One
        // asked in the layout has Camcorder's column beside it, and lies in
        // the pane.
        function test_askedOverTheWholeScreen_data() {
            return [{ tag: "Standard", layout: "Standard", framed: false },
                    { tag: "Camcorder", layout: "Camcorder", framed: true },
                    { tag: "Broadcast", layout: "Broadcast", framed: false }]
        }
        function test_askedOverTheWholeScreen(data) {
            root.menuLayout = data.layout
            root.setFrame([{ icon: "tv" }, { icon: "gear" }], 0, false, false)
            root.framed = data.framed
            var whole = createTemporaryObject(question, root, { inLayout: false })
            verify(whole !== null)
            verify(!whole.paned)
            compare(columnOf(whole), null)
            var answers = answersOf(whole)
            compare(answers.mapToItem(root, 0, 0).x, root.contentBox.x)
            compare(answers.width, root.contentBox.width)
            whole.visible = false

            var inIt = createTemporaryObject(question, root)
            verify(inIt !== null)
            compare(inIt.paned, data.layout !== "Standard")
            compare(columnOf(inIt) !== null, data.framed)
            answers = answersOf(inIt)
            compare(answers.mapToItem(root, 0, 0).x, root.paneX)
            compare(answers.width, root.paneWidth)
            if (data.layout !== "Standard")
                verify(root.paneX > root.contentBox.x)
        }

        // The card writer (views/Components/NfcCardWriter.qml) asks in the
        // window every question has, in the layout: each phase's words in
        // its title bar, the title the card is for under it, the answers
        // while there is one to choose, the keys in its hint bar; beside
        // Camcorder's column, in the pane. The mouse moves its cursor.
        function test_cardWriterWindow_data() {
            return [{ tag: "Standard", layout: "Standard", framed: false },
                    { tag: "Camcorder", layout: "Camcorder", framed: true }]
        }
        function test_cardWriterWindow(data) {
            root.menuLayout = data.layout
            root.setFrame([{ icon: "tv" }, { icon: "gear" }], 0, false, false)
            root.framed = data.framed
            var held = createTemporaryObject(cardWriter, root)
            verify(held !== null)
            var writer = held.writer
            writer.offerShuffle = true
            writer.open()
            var prompt = root.promptIn(writer)
            verify(prompt !== null)
            compare(prompt.title, "Tap a card for...")
            compare(prompt.kind, "notice")
            compare(prompt.message, "<b>The Film</b>")
            compare(prompt.choices.length, 0)
            compare(prompt.hint, "[ESC]:CANCEL")
            compare(columnOf(prompt) !== null, data.framed)
            compare(prompt.paned, data.framed)
            // Nothing to choose until a card comes.
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(writer.phase, "armed")

            held.reader.cardCaptured("04AABBCC", "An Older Film")
            compare(writer.phase, "choose")
            compare(prompt.title, "Write card for...")
            compare(prompt.kind, "question")
            compare(prompt.choices.map(function(c) { return c.label }),
                    ["Write card (Sequential Episodes)", "Write card (Shuffle Episodes)", "Cancel"])
            compare(prompt.hint, "[ESC]:CANCEL [▲▼]:NAVIGATE [ENTER]:SELECT")
            var answers = answersOf(prompt)
            compare(answers.mapToItem(root, 0, 0).x, data.framed ? root.paneX : root.contentBox.x)
            keyClick(Qt.Key_Up)
            compare(prompt.currentIndex, 2)
            hover(answers.itemAtIndex(1))
            compare(writer.choiceIndex, 1)
            keyClick(Qt.Key_Return)
            compare(writer.phase, "replace")
            compare(prompt.title, "This card plays \"An Older Film\". Change to...")
            compare(prompt.choices.map(function(c) { return c.label }), ["Cancel", "Change Card"])
            compare(prompt.currentIndex, 0)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(writer.phase, "done")
            compare(prompt.title, "Card written for...")
            compare(prompt.kind, "notice")
            compare(prompt.choices.length, 0)
            compare(prompt.hint, "[ENTER]:DONE")
            keyClick(Qt.Key_Return)
            verify(!writer.visible)
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

        // The logo is a tenth of the screen's width, the size of the one
        // over a paused video.
        function test_theLogoIsATenthAcross() {
            root.saverUp = true
            var logo = findChild(saver, "bounceLogo")
            verify(logo !== null)
            verify(Math.abs(logo.sourceSize.width - root.sw * 0.1) <= 1,
                   "the logo's width: " + logo.sourceSize.width + " of " + root.sw)
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

    // The card writer and a web player's launch page, each with what it reads
    // of the app and of its module beside it.
    Component {
        id: cardWriter
        Item {
            property alias writer: writer
            property alias reader: nfcReaderBackend
            anchors.fill: parent
            QtObject {
                id: nfcReaderBackend
                property bool available: true
                property bool readerConnected: true
                signal cardCaptured(string uid, string existing)
                function setCardCapture(on) {}
                function writeCardFile(uid, title, ref, mode) { return true }
            }
            QtObject {
                id: appCore
                function is_module_enabled(id) { return true }
            }
            NfcCardWriter {
                id: writer
                anchors.fill: parent
                cardRef: "plex://movie/1"
                cardTitle: "<b>The Film</b>"
            }
        }
    }
    Component {
        id: launchPage
        Item {
            property alias page: page
            anchors.fill: parent
            QtObject {
                id: moduleRoot
                property string moduleIcon: ""
                property string moduleName: "Netflix"
            }
            QtObject {
                id: idleTracker
                function resetActivity() {}
            }
            QtObject {
                id: appCore
                function installHint(what) { return "" }
            }
            // A browser that can't be started, and says why.
            QtObject {
                id: browser
                property bool running: false
                property bool opened: true
                property var catalog: null
                signal finished(int exitCode, string reason)
                function launch(url) { return false }
                function lastError() { return "<b>/opt/osdos/web-player.sh</b> could not be started" }
            }
            WebPlayerLaunch {
                id: page
                anchors.fill: parent
                backend: browser
                serviceName: "Netflix"
                navParams: ({ name: "<b>The Film</b>" })
            }
        }
    }

    // What a server, a catalogue, a card or a file's name says is shown as
    // it is written: a title that looks like markup is its letters, never
    // read as markup (a picture's tag in one would fetch its address). On the
    // info screen (views/Components/InfoPanel.qml), the card writer
    // (views/Components/NfcCardWriter.qml) and a web player's launch page
    // (views/Components/WebPlayerLaunch.qml).
    TestCase {
        name: "WordsAsWritten"
        when: windowShown

        function cleanup() {
            loader.source = ""
        }

        // Every Text an item holds whose words have "<b>" in them, all the
        // way down.
        function marked(item) {
            var found = []
            if (item.text !== undefined && item.textFormat !== undefined && String(item.text).indexOf("<b>") >= 0)
                found.push(item)
            for (var i = 0; i < item.children.length; i++)
                found = found.concat(marked(item.children[i]))
            return found
        }
        // At least so many of them, each as it is written.
        function asWritten(item, atLeast) {
            var texts = marked(item)
            verify(texts.length >= atLeast, texts.length + " of " + atLeast + " lines found")
            for (var i = 0; i < texts.length; i++)
                compare(texts[i].textFormat, Text.PlainText, String(texts[i].text))
        }

        // The entry's own name, then its title, its facts, its story and a
        // line of its details as they come in.
        function test_infoScreen() {
            loader.setSource(Qt.resolvedUrl("../../views/Components/InfoPanel.qml"))
            var info = loader.item
            verify(info !== null, "loaded InfoPanel.qml")
            info.show({ title: "<b>The Film</b>" })
            asWritten(info, 1)
            info.details = { title: "<b>The Film</b>", facts: "<b>1999</b> • 2H 10M", summary: "<b>A story.</b>",
                             rows: [{ label: "Genre", value: "<b>Drama</b>" }] }
            asWritten(info, 4)
        }

        // The title a card is written for, and the one a card already plays.
        function test_cardWriter() {
            var held = createTemporaryObject(cardWriter, root)
            verify(held !== null)
            held.writer.open()
            compare(held.writer.phase, "armed")
            asWritten(held.writer, 1)
            held.reader.cardCaptured("04AABBCC", "<b>An Older Film</b>")
            compare(held.writer.phase, "choose")
            keyClick(Qt.Key_Return)
            compare(held.writer.phase, "replace")
            asWritten(held.writer, 2)
        }

        // The title being opened, then why it didn't open.
        function test_webPlayerLaunch() {
            var held = createTemporaryObject(launchPage, root)
            verify(held !== null)
            compare(held.page.phase, "opening")
            asWritten(held.page, 1)
            tryCompare(held.page, "phase", "error", 1000)
            asWritten(held.page, 1)
        }
    }

    // MpvController, as the loading screen hears it: a video inside the
    // window has drawn a picture.
    QtObject {
        id: mpvController
        signal videoFrameReady()
    }
    // The loading screen a player shows while its video starts: its letters
    // in the loading ink, each bleeding to the right in the colours' second
    // ink (root.loadingBleed), Video 1's with Default; no bleed with the
    // Loading Effect off. And when it moves.
    Component {
        id: tapeLayer
        LoadingScreen {
            anchors.fill: parent
            source: "Local Files"
        }
    }
    TestCase {
        name: "LoadingScreen"
        when: windowShown

        function bleeds(item) {
            var found = []
            for (var i = 0; i < item.children.length; i++) {
                var child = item.children[i]
                if (child.text === "TAPE A" && child.opacity < 1)
                    found.push(child)
                found = found.concat(bleeds(child))
            }
            return found
        }
        function test_bleedInTheSecondInk() {
            root.loadingThemed = true
            var tape = createTemporaryObject(tapeLayer, root)
            verify(tape !== null)
            var bleed = bleeds(tape)
            compare(bleed.length, 1)
            verify(bleed[0].visible)
            verify(Qt.colorEqual(bleed[0].color, root.loadingBleed))
            root.loadingThemed = false
            compare(root.loadingBleed, "#C2BFE4")
            verify(Qt.colorEqual(bleed[0].color, "#C2BFE4"))
            tape.effect = false
            verify(!bleed[0].visible)
            root.loadingThemed = true
        }

        // With Transparent Background it goes on moving while the window's
        // video has no picture yet (nothing is drawn over it), and rests
        // once one is over it; its session gone, and shown again, it moves.
        // While another process has the screen it rests.
        function test_restsUnderAPicture() {
            var tape = createTemporaryObject(tapeLayer, root)
            verify(tape.live)
            root.videoActive = true
            verify(tape.live)
            mpvController.videoFrameReady()
            verify(!tape.live)
            root.videoActive = false
            verify(tape.live)
            root.videoActive = true
            verify(tape.live)
            mpvController.videoFrameReady()
            verify(!tape.live)
            tape.visible = false
            tape.visible = true
            verify(tape.live)
            root.videoActive = false
            root.screenHandedOff = true
            verify(!tape.live)
            root.screenHandedOff = false
        }
    }
}
