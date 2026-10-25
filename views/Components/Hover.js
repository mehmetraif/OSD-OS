.pragma library

// The selection follows the mouse, as the keys would take it: Main.qml's
// pointer MouseArea gives follow() the pointer's place on the stage as it
// moves. Apart from Main.qml so that the views' tests can move a pointer over
// them too.

// A list of rows with indexAt(): a ListView (or a GridView), which has
// contentX. A Flickable has no indexAt(), a PathView no contentX.
function isList(item) {
    return typeof item.indexAt === "function" && item.contentX !== undefined
}

// What follows the mouse under a point of an item (x, y in the item's
// coordinates), with the point in its coordinates, or null: the top-most,
// deepest list, or item with a hoverAt(x, y) function of its own (the Arcade
// screen), whose bounds hold the point. Searched as the items are stacked,
// down through everything but a clipping item the point is outside of (an
// item's children may lie outside it). Only a list that stays put under the
// pointer can follow it: one that brings the row chosen to a fixed place (a
// TreeBrowser's spine) would move the next row under the pointer, and on
// with every move of the mouse.
function targetUnder(item, x, y) {
    var kids = item.children
    for (var i = kids.length - 1; i >= 0; i--) {
        var kid = kids[i]
        if (!kid.visible)
            continue
        var p = kid.mapFromItem(item, x, y)
        var inside = kid.contains(Qt.point(p.x, p.y))
        if (isList(kid)) {
            if (inside)
                return { item: kid, x: p.x, y: p.y }
            continue
        }
        if (kid.clip && !inside)
            continue
        var deeper = targetUnder(kid, p.x, p.y)
        if (deeper)
            return deeper
        if (inside && typeof kid.hoverAt === "function")
            return { item: kid, x: p.x, y: p.y }
    }
    return null
}

// Whether the keys go to the item: it is the focused item, holds it or is
// held by it.
function nearFocus(item, focused) {
    for (var up = focused; up; up = up.parent) {
        if (up === item)
            return true
    }
    for (var down = item; down; down = down.parent) {
        if (down === focused)
            return true
    }
    return false
}

// The row under a point of the stage (x, y in its coordinates) becomes the
// current one of the list the keys go to, passing over a heading or a note,
// which the cursor steps over too. A view with a cursor of its own takes the
// row through a hoverRow(index, list) function on the list or an item holding
// it (a prompt's answers, a show's seasons), the Arcade screen the point
// through its hoverAt(x, y).
function follow(stage, focused, x, y) {
    var under = targetUnder(stage, x, y)
    if (!under || !focused || !nearFocus(under.item, focused))
        return
    var target = under.item
    if (typeof target.hoverAt === "function") {
        target.hoverAt(under.x, under.y)
        return
    }
    var index = target.indexAt(target.contentX + under.x, target.contentY + under.y)
    if (index < 0)
        return
    var row = target.model && target.model.length !== undefined ? target.model[index] : null
    if (row && (row.type === "section" || row.type === "note" || row.heading === true))
        return
    for (var host = target; host; host = host.parent) {
        if (typeof host.hoverRow === "function") {
            host.hoverRow(index, target)
            return
        }
    }
    if (target.currentIndex !== index)
        target.currentIndex = index
}
