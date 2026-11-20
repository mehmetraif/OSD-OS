import QtQuick
import QtTest
import Components

// A line too long for its room (views/Components/ScrolledText.qml) on its
// own: on, it is whole and scrolls after its pause exactly as far as it
// overflows, rests there, and starts over to the new distance when its room
// is made another width, as a layout settling makes it; hidden (itself or
// what holds it) or resting, it stops at its start, and shown again starts
// over after its pause; one that fits stands still; not on, it is cut short
// with "…", or at its room's edge with `elides` off; what is laid under its
// line moves with it; its font is the app's in capitals unless the host says
// otherwise; and a name that looks like markup is shown as it is. And lines
// too tall for their room (views/Components/ScrolledLines.qml), a page's
// summary: up after their pause exactly as far as they overflow, resting
// there, over from their top to the new distance when their room is made
// another width; hidden, still at their top, and shown again started over
// after the pause; lines that fit standing still; a summary that looks like
// markup shown as it is written. And what a list says in its place
// (views/Components/PaneMessage.qml), a server's error as it is written too.
Item {
    id: root
    width: 640
    height: 480
    // Main.qml's, as the line reads it.
    property string globalFont: "monospace"
    // And as a list's message reads it: the stage's height, the pane and
    // the colour.
    property real sh: height
    property real paneX: 74
    property real paneWidth: 492
    property string primaryColor: "#ffffff"

    readonly property string longText: "A module whose name runs past the row"
    readonly property string longStory: "A family moves into an old house by the sea for the summer, and the summer "
                                        + "turns out longer than any of them expected, for reasons none of them will say."

    // A page's summary, and one in an item that hides it.
    Component {
        id: story
        ScrolledLines {
            width: 300
            font.pixelSize: 14
        }
    }
    Component {
        id: heldStory
        Item {
            property alias lines: heldLines
            width: 300
            height: 200
            ScrolledLines {
                id: heldLines
                width: 300
                font.pixelSize: 14
                text: root.longStory
            }
        }
    }

    Component {
        id: line
        ScrolledText {
            height: 30
            font.pixelSize: 20
        }
    }

    // What a list says in its place.
    Component {
        id: message
        PaneMessage {}
    }

    // A line in an item that hides it, with a box laid under its line, as a
    // list's selected row has (the row hidden under a keyboard or a page).
    Component {
        id: held
        Item {
            property alias line: heldLine
            property alias box: box
            width: 640
            height: 30
            ScrolledText {
                id: heldLine
                height: 30
                on: true
                text: root.longText
                font.pixelSize: 20
                textItem.leftPadding: 6
                textItem.rightPadding: 6
                Rectangle {
                    id: box
                    z: -1
                    anchors.fill: heldLine.textItem
                    color: "red"
                }
            }
        }
    }

    TestCase {
        name: "ScrolledText"
        when: windowShown

        function make(props) {
            var made = createTemporaryObject(line, root, props)
            verify(made !== null)
            return made
        }

        // From its start, as far as it overflows and no further, and it
        // rests there.
        function scrollsAsFarAsItOverflows(item) {
            var least = { x: 0 }
            var track = function() { least.x = Math.min(least.x, item.offset) }
            item.offsetChanged.connect(track)
            tryVerify(function() { return item.offset <= item.scrollBy + 0.5 }, 1500 - item.scrollBy * 20 + 1500,
                      "scrolled to its end: " + item.offset + " of " + item.scrollBy)
            wait(200)
            fuzzyCompare(item.offset, item.scrollBy, 0.5)
            verify(least.x >= item.scrollBy - 0.5, "no further than its end: " + least.x + " past " + item.scrollBy)
            item.offsetChanged.disconnect(track)
        }

        function test_scrollsAsFarAsItOverflowsAfterAResize() {
            var item = make({ on: true, text: root.longText })
            compare(item.implicitWidth, item.textItem.implicitWidth)
            // A room laid out one width, then settled to another in the
            // same turn: the scroll goes the settled distance.
            item.width = item.implicitWidth - 120
            item.width = item.implicitWidth - 40
            fuzzyCompare(item.scrollBy, -40, 0.01)
            verify(item.scrolls)
            verify(!item.cut)
            compare(item.textItem.width, item.implicitWidth)
            // A pause at its start.
            wait(1000)
            compare(item.offset, 0)
            scrollsAsFarAsItOverflows(item)
            // Made another width while it scrolls: over from its start, to
            // its new end.
            item.width = item.implicitWidth - 60
            tryCompare(item, "offset", 0, 200)
            scrollsAsFarAsItOverflows(item)
            fuzzyCompare(item.offset, -60, 0.5)
        }

        function test_stoppedWhileHidden() {
            var holder = createTemporaryObject(held, root)
            verify(holder !== null)
            var item = holder.line
            item.width = item.implicitWidth - 40
            tryVerify(function() { return item.offset < -5 }, 3000, "scrolling")
            // What is laid under the line moves with it.
            compare(holder.box.x, item.offset)
            compare(holder.box.width, item.implicitWidth)
            // Its holder hidden: back at its start, and still.
            holder.visible = false
            verify(!item.scrolls)
            tryCompare(item, "offset", 0, 200)
            wait(1800)
            compare(item.offset, 0)
            // Shown again: from its start, after its pause.
            holder.visible = true
            verify(item.scrolls)
            wait(1000)
            compare(item.offset, 0)
            tryVerify(function() { return item.offset < -5 }, 3000, "scrolling again")
            // Itself hidden, the same.
            item.visible = false
            verify(!item.scrolls)
            tryCompare(item, "offset", 0, 200)
            item.visible = true
            // Resting: at its start, still whole.
            item.resting = true
            verify(!item.scrolls)
            wait(1800)
            compare(item.offset, 0)
            verify(!item.cut)
        }

        function test_fitsStandsStill() {
            var item = make({ on: true, text: "Fits", width: 200 })
            verify(!item.scrolls)
            verify(!item.cut)
            compare(item.textItem.width, item.textItem.implicitWidth)
            wait(1800)
            compare(item.offset, 0)
        }

        function test_notOnIsCut() {
            var cutShort = make({ text: root.longText })
            cutShort.width = cutShort.implicitWidth - 40
            verify(!cutShort.scrolls)
            tryVerify(function() { return cutShort.cut })
            compare(cutShort.textItem.width, cutShort.width)
            // Cut at the room's edge instead: whole, and clipped there.
            var atEdge = make({ text: root.longText, elides: false })
            atEdge.width = atEdge.implicitWidth - 40
            verify(!atEdge.scrolls)
            verify(!atEdge.cut)
            compare(atEdge.textItem.width, atEdge.implicitWidth)
            verify(atEdge.clip)
            wait(1800)
            compare(cutShort.offset, 0)
            compare(atEdge.offset, 0)
            // On (selected): whole, and scrolled through.
            cutShort.on = true
            verify(cutShort.scrolls)
            tryVerify(function() { return !cutShort.cut })
            tryVerify(function() { return cutShort.offset < -5 }, 3000, "scrolling once on")
            // Off again: cut, at its start.
            cutShort.on = false
            tryVerify(function() { return cutShort.cut })
            tryCompare(cutShort, "offset", 0, 200)
        }

        function test_fontAndText() {
            var item = make({ text: "Mixed" })
            compare(item.textItem.font.family, root.globalFont)
            compare(item.textItem.font.capitalization, Font.AllUppercase)
            compare(item.textItem.font.pixelSize, 20)
            // Flat Panel's mixed case, the rest of the font kept.
            item.font.capitalization = Font.MixedCase
            compare(item.textItem.font.capitalization, Font.MixedCase)
            compare(item.textItem.font.family, root.globalFont)
            compare(item.textItem.font.pixelSize, 20)
            // A name such as "<b>AB</b>" is its nine letters, not a bold AB.
            var marked = make({ text: "<b>AB</b>" })
            var plain = make({ text: "AB" })
            verify(marked.implicitWidth > 2.5 * plain.implicitWidth,
                   marked.implicitWidth + " against " + plain.implicitWidth)
        }
    }

    TestCase {
        name: "ScrolledLines"
        when: windowShown

        function make(props) {
            var made = createTemporaryObject(story, root, props)
            verify(made !== null)
            return made
        }

        // Up from their top, as far as they overflow and no further, and
        // they rest there.
        function scrollsAsFarAsTheyOverflow(item) {
            var least = { y: 0 }
            var track = function() { least.y = Math.min(least.y, item.offset) }
            item.offsetChanged.connect(track)
            tryVerify(function() { return item.offset <= item.scrollBy + 0.5 }, 3000 - item.scrollBy * 120 + 1500,
                      "scrolled to their foot: " + item.offset + " of " + item.scrollBy)
            wait(300)
            fuzzyCompare(item.offset, item.scrollBy, 0.5)
            verify(least.y >= item.scrollBy - 0.5, "no further than their foot: " + least.y + " past " + item.scrollBy)
            item.offsetChanged.disconnect(track)
        }

        function test_scrollAsFarAsTheyOverflowAfterAResize() {
            var item = make({ text: root.longStory })
            item.height = item.textItem.implicitHeight - 12
            // A room laid out narrower, then settled to its width in the
            // same turn: the scroll goes the settled distance.
            item.width = 200
            item.width = 300
            fuzzyCompare(item.scrollBy, -12, 0.01)
            verify(item.scrolls)
            compare(item.textItem.width, item.width)
            // A pause at their top.
            wait(2000)
            compare(item.offset, 0)
            scrollsAsFarAsTheyOverflow(item)
            // Made narrower while they rest at their foot: a line more, over
            // from the top to the new foot.
            item.width = 260
            verify(item.scrollBy < -12)
            tryCompare(item, "offset", 0, 300)
            scrollsAsFarAsTheyOverflow(item)
        }

        function test_stoppedWhileHidden() {
            var holder = createTemporaryObject(heldStory, root)
            verify(holder !== null)
            var item = holder.lines
            item.height = item.textItem.implicitHeight - 12
            tryVerify(function() { return item.offset < -3 }, 5000, "scrolling")
            // Their holder hidden: back at the top, and still.
            holder.visible = false
            verify(!item.scrolls)
            tryCompare(item, "offset", 0, 300)
            wait(3200)
            compare(item.offset, 0)
            // Shown again: from the top, after the pause.
            holder.visible = true
            verify(item.scrolls)
            wait(2000)
            compare(item.offset, 0)
            tryVerify(function() { return item.offset < -3 }, 3000, "scrolling again")
        }

        function test_fitStandsStill() {
            var item = make({ text: "A short story.", height: 60 })
            verify(!item.scrolls)
            wait(3200)
            compare(item.offset, 0)
            // Wrapped at their width, in the app's font.
            compare(item.textItem.width, item.width)
            compare(item.textItem.wrapMode, Text.WordWrap)
            compare(item.textItem.font.family, root.globalFont)
            compare(item.textItem.font.pixelSize, 14)
        }

        // A summary is a server's words: one that looks like markup is shown
        // as it is written, "<b>AB</b>" its nine letters and not a bold AB,
        // and a picture's tag in it is its letters too, fetching nothing.
        function test_markupShownAsWritten() {
            var marked = make({ text: "<b>AB</b>", width: 600, height: 60 })
            var plain = make({ text: "AB", width: 600, height: 60 })
            verify(marked.textItem.implicitWidth > 2.5 * plain.textItem.implicitWidth,
                   marked.textItem.implicitWidth + " against " + plain.textItem.implicitWidth)
            var picture = make({ text: "<img src=\"http://127.0.0.1:9/poster.png\" width=\"400\" height=\"300\">",
                                 width: 600, height: 60 })
            verify(picture.textItem.implicitHeight < 100, "a line of letters, not a picture's room")
            compare(marked.textItem.textFormat, Text.PlainText)
        }
    }

    TestCase {
        name: "PaneMessage"
        when: windowShown

        // In the pane, wrapped at its width; a server's error that looks
        // like markup is shown as it is written.
        function test_markupShownAsWritten() {
            var marked = createTemporaryObject(message, root, { text: "<b>AB</b>" })
            var plain = createTemporaryObject(message, root, { text: "AB" })
            verify(marked !== null && plain !== null)
            compare(marked.x, root.paneX)
            compare(marked.width, root.paneWidth)
            compare(marked.wrapMode, Text.WordWrap)
            verify(marked.implicitWidth > 2.5 * plain.implicitWidth,
                   marked.implicitWidth + " against " + plain.implicitWidth)
            compare(marked.textFormat, Text.PlainText)
        }
    }
}
