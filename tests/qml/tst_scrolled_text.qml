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
// otherwise; and a name that looks like markup is shown as it is.
Item {
    id: root
    width: 640
    height: 480
    // Main.qml's, as the line reads it.
    property string globalFont: "monospace"

    readonly property string longText: "A module whose name runs past the row"

    Component {
        id: line
        ScrolledText {
            height: 30
            font.pixelSize: 20
        }
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
}
