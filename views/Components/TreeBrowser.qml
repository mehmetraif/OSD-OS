import QtQuick

// A browser laid out as a horizontal tree, for anything shaped like folders:
// Local Files' folders, or a streaming catalogue's categories. The folders on
// the way to the current one run left to right along a line through the middle
// of the screen (the spine). Each folder's contents are stacked above and below
// the item that leads on. Every folder in the current one branches off to the
// right, on a dotted line to a few of its own entries, and the folder under the
// cursor branches once more, from each folder in it.
//
// Up/down move within the current folder, right (or select) opens a folder,
// left (or back) returns to its parent. Select on anything else is the host's
// to handle (activated), right on it too (previewRequested with preview on,
// for its info: the tree's last layer; optionsRequested otherwise), and so is
// back at the top (leaveRequested). With previewDelay set, resting the cursor
// on it that long asks for its info too. It draws only the tree, in the area
// between a view's title bar and its footer.
//
// The entries come from fetch(path, preview): [{ name, path, isFolder, ... }],
// or null while they are still on their way, shown as "loading…" until the
// host calls refresh(path). preview is true when only a branch wants them, so a
// slow source can return null then without fetching; the branch shows "…".
// Entries can carry anything else the host needs back in activated(item).
FocusScope {
    id: tree

    // The folder the tree starts at.
    property string rootPath: ""
    property var fetch: null
    property var labelOf: function(item) { return item.name }
    // A trail saved by trailState(), to reopen on creation.
    property var savedTrail: []
    // Local browsing can expose complete sibling contents, plus the selected
    // folder's child folders. Remote catalogues retain their compact previews.
    property bool expandedFolderPreviews: false
    // How faint the branches off any other folder than the one under the
    // cursor are drawn, their lines with half their dots.
    readonly property real faintOpacity: 0.45

    signal activated(var item)
    signal optionsRequested(var item)
    signal previewRequested(var item)
    signal leaveRequested()

    // An entry's info (the host shows it): with right, or the info key, on
    // anything that isn't a folder, and on its own after the cursor has rested
    // on one previewDelay ms (0: only when asked).
    property bool preview: false
    property int previewDelay: 0

    // The open folders, root first, with the cursor's row in each: to hand
    // back as savedTrail when the view comes back.
    function trailState() {
        var saved = []
        for (var i = 0; i < trail.count; ++i) {
            var c = trail.get(i)
            saved.push({ path: c.path, sel: c.sel, name: c.name, pushed: c.pushed })
        }
        return saved
    }

    // Opens a folder that isn't an entry of the current one, like a search's
    // results: { name, path }.
    function openItem(item) {
        trail.append({ path: item.path, sel: remembered[item.path] || 0, name: item.name, pushed: true })
        relayout()
    }

    // A folder's entries changed, or came in after fetch() returned null. Its
    // cursor keeps its row, or with keepEntry the entry it was on, while that
    // is still there (entries came or went above it: a drive plugged in).
    function refresh(path, keepEntry) {
        var before = listings[path]
        delete listings[path]
        revision++
        if (!ready) return
        for (var i = 0; i < trail.count; ++i) {
            if (trail.get(i).path === path) {
                var items = listing(path).items
                var sel = trail.get(i).sel
                var was = keepEntry && before && !before.pending ? before.items[sel] : null
                if (was) {
                    for (var j = 0; j < items.length; ++j) {
                        if (items[j].path === was.path) {
                            sel = j
                            break
                        }
                    }
                }
                sel = Math.max(0, Math.min(sel, items.length - 1))
                trail.setProperty(i, "sel", sel)
                remembered[path] = sel
                relayout()
                return
            }
        }
        if (branched)
            layoutBranches(true)
    }

    // A folder gone with all in it (a drive taken out): the open folders at it
    // and in it close, back to the one that held it, and what they listed is
    // forgotten.
    function leave(prefix) {
        var inside = function(p) { return p === prefix || p.indexOf(prefix + "/") === 0 }
        for (var p in listings) {
            if (inside(p))
                delete listings[p]
        }
        for (var i = 1; i < trail.count; ++i) {
            if (inside(trail.get(i).path)) {
                trail.remove(i, trail.count - i)
                revision++
                if (ready)
                    relayout()
                return
            }
        }
    }

    // Under the cursor, or null.
    function currentItem() { return selectedItem() }
    // The same, as a property that follows the cursor: for a footer that says
    // what select will do with it.
    property var currentEntry: null

    // --- Layout ---
    readonly property real fontSize: root.sh * 0.0375 //18
    readonly property real rowHeight: root.sh * 0.05 //24
    // Never a single line: on an interlaced CRT that sits on one field and flickers.
    readonly property int lineWidth: Math.max(2, Math.round(root.sh * 0.0041667)) //2
    readonly property real gap: root.sw * 0.046875 //30
    readonly property real pad: root.sw * 0.009375 //6
    readonly property real maxColumnWidth: root.sw * 0.34375 //220
    readonly property real leftEdge: root.sw * 0.125 //80
    readonly property real rightEdge: root.sw * 0.875 //560
    readonly property real treeTop: root.sh * 0.2083333 //100
    readonly property real treeBottom: root.sh * 0.8333333 //400
    // Where the area ends: over the hint bar, or with Settings' Hint Bar off,
    // at the content box's foot, more rows showing under the spine.
    readonly property real areaBottom: root.hintBar ? treeBottom : root.contentBox.y + root.contentBox.height //400, 430
    // The spine, in tree-area coordinates: midway down the area over the hint
    // bar, wherever the area ends.
    readonly property real spine: Math.round((treeBottom - treeTop) / 2)
    // Room a host keeps under the tree for a line of its own (a HelpLine
    // while it shows): the area stops short of it, the spine stays put.
    property real reservedBottom: 0
    // How far the area reaches above and below the spine: only rows wholly
    // inside it are drawn, so none is cut in half by its edge.
    readonly property real bandTop: -spine
    readonly property real bandBottom: areaBottom - treeTop - reservedBottom - spine
    onBandBottomChanged: if (ready && branched) layoutBranches(true)
    // Branches: the gap before each level leaves room for the lanes their
    // lines turn in, one a line's width apart from the next.
    readonly property real branchGap: root.sw * 0.0625 //40
    readonly property real laneStep: 2 * lineWidth
    // Narrower than this, a level of branches isn't worth drawing.
    readonly property real minBranchWidth: root.sw * 0.1 //64
    readonly property real blockGap: Math.round(rowHeight / 2)
    // Entries a branch shows: a window around the remembered row for the folder
    // under the cursor, the first few for the others.
    readonly property int anchorRows: 5
    readonly property int branchRows: 3

    // --- Tree state ---
    // The open folders, root first; the last one holds the cursor.
    // Roles: path, sel (the row the spine runs through), name (what it is
    // called), pushed (opened with openItem rather than from its parent).
    ListModel { id: trail }
    readonly property int active: trail.count - 1
    // Each folder is listed once per visit of this view, until refresh().
    property var listings: ({})
    // Bumped by refresh(), so the columns list their folders again.
    property int revision: 0
    // Last cursor row per folder, so reopening one lands where it was left.
    property var remembered: ({})
    // Left edge and width of each open folder's column.
    property var columnX: []
    property var columnW: []
    // The branches off the current folder, in strip coordinates with y from the
    // spine: blocks of entries { x, top, width, rows: [{ label }] }, the dotted
    // lines to them { x0, y0, x1, y1, lane }, and how far right they reach.
    property var blocks: []
    property var wires: []
    property real branchLeft: 0
    property real branchRight: 0
    // Where placeStrip() put the strip (it may still be sliding there).
    property real stripTarget: 0
    property bool branched: false
    // Open folders left of this one are off to the left: their names are
    // hidden and the spine runs on through them from the screen's edge.
    property int firstShown: 0
    property string folderName: ""
    // The top folder has nothing in it (once it has come in). Not asked
    // before the tree is ready: a listing is measured once and kept, and until
    // then the measuring font may not be set yet.
    readonly property bool rootEmpty: { revision; return ready && !listing(rootPath).pending && listing(rootPath).items.length === 0 }
    property bool ready: false

    FontMetrics {
        id: metrics
        font.family: root.globalFont
        font.pixelSize: tree.fontSize
    }

    function displayName(item) {
        return labelOf(item)
    }

    // Rows are drawn upper-case, so they are measured that way.
    function textWidth(text) {
        return Math.ceil(metrics.advanceWidth(text.toUpperCase()))
    }

    // { items, width, fullWidth, leaf, pending }: width is the column's as a
    // folder on the way, fullWidth what its longest name needs, and leaf says
    // nothing in it is a folder. A folder only a branch asked for, and that its
    // source left unfetched, is asked for again once it is opened.
    function listing(path, preview) {
        if (!path) return { items: [], width: 0, fullWidth: 0, leaf: true, pending: false }
        // Bindings can ask before the host has set fetch; nothing is kept then.
        if (!fetch) return { items: [], width: 0, fullWidth: 0, leaf: true, pending: true }
        var l = listings[path]
        if (l && !(l.pending && l.previewOnly && !preview))
            return l
        var items = fetch(path, !!preview)
        if (items === null || items === undefined) {
            var lw = textWidth("loading\u2026")
            l = { items: [], width: lw, fullWidth: lw, leaf: true, pending: true, previewOnly: !!preview }
        } else {
            var w = items.length > 0 ? 0 : textWidth("(empty)")
            var leaf = true
            for (var i = 0; i < items.length; ++i) {
                w = Math.max(w, textWidth(displayName(items[i])))
                if (items[i].isFolder) leaf = false
            }
            l = { items: items, width: Math.min(maxColumnWidth, w), fullWidth: w, leaf: leaf, pending: false }
        }
        listings[path] = l
        return l
    }

    function selectedItem() {
        if (active < 0) return null
        var col = trail.get(active)
        return listing(col.path).items[col.sel] || null
    }

    function baseName(path) {
        var parts = path.split("/")
        return parts[parts.length - 1] || path
    }

    // Places the columns side by side. The one with the cursor, when nothing
    // in it branches, takes the room up to the right edge for long names.
    function placeColumns() {
        var xs = []
        var ws = []
        var x = 0
        for (var i = 0; i < trail.count; ++i) {
            var l = listing(trail.get(i).path)
            var w = l.width
            if (expandedFolderPreviews && !l.leaf)
                w = Math.min(w, (rightEdge - leftEdge - 2 * branchGap) / 3)
            if (i === trail.count - 1 && l.leaf) {
                // The cursor's box reaches a pad past the column.
                var room = rightEdge - leftEdge - pad - (i > 0 ? ws[i - 1] + gap : 0)
                w = Math.max(w, Math.min(l.fullWidth, room))
            }
            xs.push(x)
            ws.push(w)
            x += w + gap
        }
        columnX = xs
        columnW = ws
    }

    // Slides the strip so the parent folder starts at the left edge, unless
    // that would push the branches off the right; then the folder with the
    // cursor starts there instead, and the spine runs in to it from off to
    // the left. Nothing named is ever left of the edge: on a CRT that is
    // where the picture starts to go under the bezel. Only opening and
    // closing folders move it.
    function placeStrip() {
        var activeLeft = columnX[active]
        var right = Math.max(activeLeft + columnW[active], branchRight)
        var parentAtEdge = leftEdge - (active > 0 ? columnX[active - 1] : 0)
        var stripX = right + parentAtEdge <= rightEdge ? parentAtEdge : leftEdge - activeLeft
        if (expandedFolderPreviews)
            stripX = Math.min(leftEdge, Math.max(leftEdge - activeLeft, rightEdge - right))
        stripTarget = stripX
        strip.x = stripX
        // Only the parent stays named, and only while it is at the edge.
        firstShown = Math.max(0, active - 1)
        if (active > 0 && stripX !== parentAtEdge)
            firstShown = active
        if (expandedFolderPreviews) {
            firstShown = 0
            while (firstShown < active && columnX[firstShown] + stripX < leftEdge)
                firstShown++
        }
        folderName = active > 0 ? (trail.get(active).name || baseName(trail.get(active).path)) : ""
    }

    // A block of a folder's entries for a branch: a window of anchorRows
    // around entry `around`, or with around < 0 its first few, the last of
    // them "…" when there are more. null for an empty folder off the spine.
    // An entry marked branchHidden is left out of a folder's first few (the
    // file picker's USE THIS FOLDER, in every folder).
    function blockFor(path, around) {
        var l = listing(path, true)
        if (expandedFolderPreviews) {
            if (!l.expandedBlock) {
                var expandedRows = []
                var expandedItems = []
                var expandedWidth = 0
                for (var e = 0; e < l.items.length; ++e) {
                    if (l.items[e].branchHidden) continue
                    var label = displayName(l.items[e])
                    expandedRows.push({ label: label, item: l.items[e] })
                    expandedItems.push(l.items[e])
                    expandedWidth = Math.max(expandedWidth, textWidth(label))
                }
                l.expandedBlock = { rows: expandedRows, items: expandedItems, width: expandedWidth }
            }
            var cached = l.expandedBlock
            if (cached.rows.length > 0) {
                var selectedIndex = around >= 0 ? cached.items.indexOf(l.items[around]) : 0
                return { path: path, rows: cached.rows, offset: Math.max(0, selectedIndex),
                         width: Math.min(maxColumnWidth, cached.width) }
            }
        }
        var items = expandedFolderPreviews || around < 0
            ? l.items.filter(function(item) { return !item.branchHidden }) : l.items
        if (expandedFolderPreviews && around >= 0) {
            var selected = l.items[Math.min(around, l.items.length - 1)]
            around = Math.max(0, items.indexOf(selected))
        }
        var rows = []
        var offset = 0
        if (items.length === 0) {
            if (around < 0) return null
            // A branch's glance its source wouldn't fetch: there is more,
            // to be seen once it is opened.
            rows.push({ label: !l.pending ? "(empty)" : l.previewOnly ? "\u2026" : "loading\u2026" })
        } else if (expandedFolderPreviews) {
            for (var n = 0; n < items.length; ++n)
                rows.push({ label: displayName(items[n]), item: items[n] })
            offset = around >= 0 ? Math.min(around, items.length - 1) : 0
        } else if (around >= 0) {
            var r = Math.min(around, items.length - 1)
            var first = Math.max(0, Math.min(r - Math.floor(anchorRows / 2), items.length - anchorRows))
            for (var i = first; i < Math.min(items.length, first + anchorRows); ++i)
                rows.push({ label: displayName(items[i]), item: items[i] })
            offset = r - first
        } else {
            var shown = items.length > branchRows ? branchRows - 1 : items.length
            for (var j = 0; j < shown; ++j)
                rows.push({ label: displayName(items[j]), item: items[j] })
            if (items.length > branchRows)
                rows.push({ label: "\u2026" })
        }
        // A folder still on its way keeps a whole column's room, so the
        // strip is placed for its entries rather than for "loading…".
        var w = l.pending ? maxColumnWidth : 0
        for (var k = 0; k < rows.length; ++k)
            w = Math.max(w, textWidth(rows[k].label))
        return { path: path, rows: rows, offset: offset, width: Math.min(maxColumnWidth, w) }
    }

    // One level of branches, its blocks no wider than room. parents: the
    // folders it branches from, top to bottom, { path, y, end } with y the
    // middle of the folder's row and end where a line can leave it. The one
    // on the spine keeps its remembered
    // entry on the spine; every other block grows away from the spine from
    // its folder's row, as near to it as the block before it allows. A line
    // that has to turn does it in a lane of its own, the farther from the
    // spine the further left, so no two lines cross.
    function branchLevel(parents, laneLeft, x, room) {
        var level = { blocks: [], wires: [], width: 0, anchor: null, spine: null }
        var top = -rowHeight / 2
        var bottom = rowHeight / 2
        var above = []
        var below = []
        for (var i = 0; i < parents.length; ++i) {
            var p = parents[i]
            if (Math.abs(p.y) < 1) {
                var a = blockFor(p.path, remembered[p.path] || 0)
                a.top = -(a.offset + 0.5) * rowHeight
                top = a.top
                bottom = a.top + a.rows.length * rowHeight
                level.anchor = a
                level.blocks.push(a)
                level.spine = { x0: p.end, y0: 0, x1: x - pad, y1: 0, lane: -1 }
                level.wires.push(level.spine)
            } else if (p.y < 0) {
                above.unshift(p)
            } else {
                below.push(p)
            }
        }
        var sides = [{ list: above, up: true }, { list: below, up: false }]
        for (var s = 0; s < sides.length; ++s) {
            var limit = sides[s].up ? top - blockGap : bottom + blockGap
            var turning = []
            for (var j = 0; j < sides[s].list.length; ++j) {
                var q = sides[s].list[j]
                var b = blockFor(q.path, -1)
                if (!b) continue
                var h = b.rows.length * rowHeight
                var target
                if (sides[s].up) {
                    b.top = Math.min(q.y + rowHeight / 2, limit) - h
                    target = b.top + h - rowHeight / 2
                } else {
                    b.top = Math.max(q.y - rowHeight / 2, limit)
                    target = b.top + rowHeight / 2
                }
                // A line ending on another entry's row would read as that
                // entry's, so such a block moves half a row further out.
                var phase = ((target % rowHeight) + rowHeight) % rowHeight
                if (Math.abs(target - q.y) >= 1 && (phase < 1 || phase > rowHeight - 1)) {
                    var shift = sides[s].up ? -rowHeight / 2 : rowHeight / 2
                    b.top += shift
                    target += shift
                }
                // One that would cross the area's edge is left out with its
                // line, and so is every one further out.
                if (!expandedFolderPreviews && (b.top < bandTop || b.top + h > bandBottom))
                    break
                limit = sides[s].up ? b.top - blockGap : b.top + h + blockGap
                level.blocks.push(b)
                var wire = { x0: q.end, y0: q.y, x1: x - pad, y1: target, lane: -1 }
                level.wires.push(wire)
                if (Math.abs(target - q.y) >= 1)
                    turning.push(wire)
            }
            // Nearest first in `turning`, so the farthest gets lane 0. The
            // lanes stop a step short of the blocks; past that they share one.
            var lastLane = Math.max(0, Math.floor((x - laneLeft - 2 * pad) / laneStep) - 1)
            for (var t = 0; t < turning.length; ++t)
                turning[t].lane = laneLeft + pad + Math.min(turning.length - 1 - t, lastLane) * laneStep
        }
        for (var m = 0; m < level.blocks.length; ++m) {
            level.blocks[m].x = x
            level.blocks[m].width = Math.min(level.blocks[m].width, room)
            level.width = Math.max(level.width, level.blocks[m].width)
        }
        return level
    }

    // Lays out the branches off the folder with the cursor: one level from
    // every folder in it that is near enough to show, and a second from each
    // folder in the block under the cursor. stripFixed: the strip stays where
    // it is (the cursor moved, or a branch's entries came in), rather than
    // being placed again for these branches.
    function layoutBranches(stripFixed) {
        var col = trail.get(active)
        var items = listing(col.path).items
        var colX = columnX[active]
        var colW = columnW[active]
        var reach = Math.ceil(spine / rowHeight) + 1
        var parents = []
        for (var i = Math.max(0, col.sel - reach); i < Math.min(items.length, col.sel + reach + 1); ++i) {
            if (!items[i].isFolder) continue
            var name = Math.min(colW, textWidth(displayName(items[i])))
            parents.push({
                path: items[i].path,
                y: (i - col.sel) * rowHeight,
                // The cursor's box reaches a pad further than a name does.
                end: colX + name + (i === col.sel ? 2 : 1) * pad
            })
        }
        // Nothing may reach past the right edge: where it is now, if the strip
        // stays put, or else where it is once the strip has slid as far as it
        // goes, which puts this column at the left edge.
        var reachRight = stripFixed ? rightEdge - stripTarget : colX + rightEdge - leftEdge
        var x1 = colX + colW + branchGap
        if (reachRight - x1 < minBranchWidth)
            parents = []
        var selected = items[col.sel]
        var twoLevels = expandedFolderPreviews && selected && selected.isFolder
            && !listing(selected.path, true).leaf
        var firstRoom = twoLevels ? (reachRight - x1 - branchGap) / 2 : reachRight - x1
        var first = branchLevel(parents, colX + colW, x1, firstRoom)
        var all = first.blocks.slice()
        var lines = first.wires.slice()
        var right = first.blocks.length > 0 ? x1 + first.width : 0
        var a = first.anchor
        var x2 = x1 + first.width + branchGap
        if (a && reachRight - x2 >= minBranchWidth) {
            var next = []
            for (var k = 0; k < a.rows.length; ++k) {
                var entry = a.rows[k].item
                if (!entry || !entry.isFolder) continue
                var parentY = a.top + (k + 0.5) * rowHeight
                // Full geometry, but only visible parents cause filesystem reads.
                if (expandedFolderPreviews && (parentY < bandTop - rowHeight || parentY > bandBottom + rowHeight))
                    continue
                next.push({
                    path: entry.path,
                    y: a.top + (k + 0.5) * rowHeight,
                    end: x1 + Math.min(a.width, textWidth(a.rows[k].label)) + pad
                })
            }
            var second = branchLevel(next, x1 + first.width, x2, reachRight - x2)
            all = all.concat(second.blocks)
            lines = lines.concat(second.wires)
            if (second.blocks.length > 0)
                right = x2 + second.width
        }
        // The folder under the cursor is the one looked into: its entries, and
        // the line to them, in full; every other branch faint, so folders side
        // by side don't run together.
        for (var f = 0; f < all.length; ++f)
            all[f].faint = all[f] !== first.anchor
        for (var g = 0; g < lines.length; ++g)
            lines[g].faint = lines[g] !== first.spine
        branchLeft = colX
        branchRight = right
        blocks = all
        wires = lines
        branched = true
    }

    // Keep the complete layout, creating text objects only for visible rows.
    function visibleRows(block) {
        var first = Math.max(0, Math.ceil((bandTop - block.top) / rowHeight))
        var last = Math.min(block.rows.length, Math.floor((bandBottom - block.top) / rowHeight))
        return { first: first, rows: block.rows.slice(first, Math.max(first, last)) }
    }

    function clearBranches() {
        branched = false
        blocks = []
        wires = []
    }

    function move(delta) {
        var col = trail.get(active)
        var n = listing(col.path).items.length
        if (n === 0) return
        var sel = (col.sel + delta + n) % n
        trail.setProperty(active, "sel", sel)
        remembered[col.path] = sel
        currentEntry = selectedItem()
        armPreview()
        // The branches are wrong now; new ones grow once the cursor rests.
        clearBranches()
        branchTimer.restart()
    }

    // After opening or closing a folder: the columns, its branches, and the
    // strip slid to show them.
    function relayout() {
        branchTimer.stop()
        placeColumns()
        layoutBranches(false)
        placeStrip()
        currentEntry = selectedItem()
        armPreview()
    }

    function openFolder() {
        var item = selectedItem()
        if (!item || !item.isFolder) return false
        trail.append({ path: item.path, sel: remembered[item.path] || 0, name: displayName(item), pushed: false })
        relayout()
        return true
    }

    function closeFolder() {
        if (active <= 0) return false
        trail.remove(active)
        relayout()
        return true
    }

    function activate() {
        var item = selectedItem()
        if (item && !item.isFolder)
            activated(item)
    }

    // Reopens the folders saved by trailState(), as far as they still exist.
    // A folder still on its way is taken on trust; refresh() puts its cursor
    // back in range once it is in.
    function restore(saved) {
        trail.append({ path: rootPath, sel: 0, name: "", pushed: false })
        for (var i = 0; i < saved.length; ++i) {
            if (saved[i].path !== trail.get(i).path) break
            var l = listing(trail.get(i).path)
            var sel = l.pending ? saved[i].sel : Math.max(0, Math.min(saved[i].sel, l.items.length - 1))
            trail.setProperty(i, "sel", sel)
            remembered[trail.get(i).path] = sel
            var next = saved[i + 1]
            if (!next) break
            var item = l.items[sel]
            var leadsOn = next.pushed || l.pending
                || (item && item.isFolder && item.path === next.path)
            if (!leadsOn) break
            trail.append({ path: next.path, sel: 0, name: next.name || "", pushed: !!next.pushed })
        }
    }

    Timer {
        id: branchTimer
        interval: 180
        onTriggered: tree.layoutBranches(true)
    }

    // Once per resting place: the info comes up after the cursor stops on an
    // entry, not again after it has been closed there.
    function armPreview() {
        previewTimer.stop()
        if (preview && previewDelay > 0 && currentEntry && !currentEntry.isFolder)
            previewTimer.start()
    }
    Timer {
        id: previewTimer
        interval: Math.max(1, tree.previewDelay)
        // Not over the host's keyboard or a dialog it has open.
        onTriggered: if (tree.activeFocus && tree.currentEntry && !tree.currentEntry.isFolder)
                         tree.previewRequested(tree.currentEntry)
    }

    // A key already held as the tree appears (BACK held to close a player,
    // say) goes on repeating into it. Only presses that begin here count, or
    // the repeat would climb out of every folder, and out of the module.
    property bool keysArmed: false

    Keys.onPressed: function(event) {
        if (event.isAutoRepeat && !keysArmed) {
            event.accepted = true
            return
        }
        keysArmed = true
        switch (event.key) {
        case Qt.Key_Up:
            move(-1)
            break
        case Qt.Key_Down:
            move(1)
            break
        case Qt.Key_Right:
            if (!openFolder() && selectedItem()) {
                if (preview) previewRequested(selectedItem())
                else optionsRequested(selectedItem())
            }
            break
        // A remote's INFO key, or the play/pause button, which has nothing
        // to play or pause here.
        case Qt.Key_Info:
        case Qt.Key_I:
        case Qt.Key_Space:
            if (preview && selectedItem() && !selectedItem().isFolder)
                previewRequested(selectedItem())
            break
        case Qt.Key_Left:
            closeFolder()
            break
        case Qt.Key_Return:
        case Qt.Key_Enter:
            if (!openFolder()) activate()
            break
        case Qt.Key_Escape:
        case Qt.Key_Backspace:
        case Qt.Key_Back:
            if (!closeFolder()) leaveRequested()
            break
        default:
            return
        }
        event.accepted = true
    }

    // One folder's column. Only the rows that fit on screen exist; they show
    // whichever entries sit around the cursor, and slide a row when it moves.
    component TreeColumn: Item {
        id: col

        property string folderPath: ""
        property int cursorIndex: 0
        // "path" (an open folder left of the cursor) or "active".
        property string role: "path"
        // Whether a line runs on from the cursor row to the next column.
        property bool leadsOn: false
        // An open folder off to the left: no names, just the spine through it.
        property bool collapsed: false
        // The next column is collapsed too, so the line runs into it unbroken.
        property bool joinsNext: false
        // From placeColumns(); its listing's own width until then.
        property real columnWidth: -1

        readonly property var entries: { tree.revision; return tree.listing(folderPath) }
        readonly property var items: entries.items
        readonly property string cursorLabel: items[cursorIndex] ? tree.displayName(items[cursorIndex]) : ""
        readonly property int reach: Math.ceil(tree.spine / tree.rowHeight) + 1
        property real slide: 0
        property int lastIndex: 0

        width: columnWidth >= 0 ? columnWidth : entries.width
        height: parent ? parent.height : 0

        onCursorIndexChanged: {
            var delta = cursorIndex - lastIndex
            lastIndex = cursorIndex
            slideAnim.stop()
            // One row at a time slides; a wrap-around just jumps.
            slide = Math.abs(delta) === 1 ? delta : 0
            if (slide !== 0) slideAnim.start()
        }
        onFolderPathChanged: {
            slideAnim.stop()
            slide = 0
            lastIndex = cursorIndex
        }
        Component.onCompleted: lastIndex = cursorIndex

        NumberAnimation {
            id: slideAnim
            target: col
            property: "slide"
            to: 0
            duration: 140
            easing.type: Easing.OutCubic
        }

        Repeater {
            model: col.reach * 2 + 1

            Item {
                id: row
                required property int index
                opacity: col.collapsed ? 0 : 1
                Behavior on opacity { NumberAnimation { duration: 120 } }
                readonly property int entryIndex: col.cursorIndex + index - col.reach
                readonly property var entry: col.items[entryIndex]
                readonly property bool current: index === col.reach
                readonly property bool cursor: current && col.role === "active"
                readonly property string label: entry ? tree.displayName(entry) : ""
                // Where it rests between slides; a row the area's edge would
                // cut there isn't drawn.
                readonly property real restY: tree.spine - height / 2 + (index - col.reach) * height

                visible: entry !== undefined && restY >= 0 && restY + height <= col.height
                width: col.width
                height: tree.rowHeight
                y: restY + col.slide * height

                // The cursor: the row in a solid box, its name in the
                // background colour, as a deck's menu marks what is selected
                // (or the skin's picture of a selected line, Settings → Skin).
                SelectionBox {
                    visible: row.cursor
                    x: -tree.pad
                    width: Math.min(labelText.implicitWidth, row.width) + 2 * tree.pad
                    height: row.height
                }

                Item {
                    width: row.width
                    height: row.height
                    clip: row.cursor

                    Item {
                        id: slider
                        height: parent.height
                        Text {
                            id: labelText
                            anchors.verticalCenter: parent.verticalCenter
                            width: row.cursor ? implicitWidth : row.width
                            text: row.label
                            elide: row.cursor ? Text.ElideNone : Text.ElideRight
                            color: row.cursor ? root.surfaceColor : root.primaryColor
                            font.family: root.globalFont
                            font.capitalization: Font.AllUppercase
                            font.pixelSize: tree.fontSize
                        }
                    }
                }

                // A cursor row too long for its column scrolls through.
                SequentialAnimation {
                    running: row.cursor && labelText.implicitWidth > row.width
                    loops: Animation.Infinite
                    onRunningChanged: if (!running) slider.x = 0
                    PauseAnimation { duration: 1500 }
                    NumberAnimation {
                        target: slider
                        property: "x"
                        to: row.width - labelText.implicitWidth
                        duration: Math.abs(to) * 20
                    }
                    PauseAnimation { duration: 2000 }
                    PropertyAction { target: slider; property: "x"; value: 0 }
                }
            }
        }

        Text {
            visible: col.folderPath !== "" && col.items.length === 0
            y: tree.spine - height / 2
            text: col.entries.pending ? "loading\u2026" : "(empty)"
            color: root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: tree.fontSize
        }

        // The spine on from the cursor row to the next open folder.
        Rectangle {
            readonly property real start: col.collapsed ? 0
                : Math.min(col.width, tree.textWidth(col.cursorLabel)) + tree.pad
            visible: col.leadsOn && col.items.length > 0
            x: start
            y: tree.spine - height / 2
            width: Math.max(0, col.width + tree.gap - (col.joinsNext ? 0 : tree.pad) - start)
            height: tree.lineWidth
            color: root.primaryColor
        }
    }

    // The tree
    Item {
        y: tree.treeTop
        width: parent.width
        height: tree.areaBottom - tree.treeTop - tree.reservedBottom
        clip: true

        Item {
            id: strip
            height: parent.height
            Behavior on x {
                enabled: tree.ready
                NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
            }

            Repeater {
                model: trail
                TreeColumn {
                    required property int index
                    required property string path
                    required property int sel
                    x: tree.columnX[index] !== undefined ? tree.columnX[index] : 0
                    columnWidth: tree.columnW[index] !== undefined ? tree.columnW[index] : -1
                    folderPath: path
                    cursorIndex: sel
                    role: index === tree.active ? "active" : "path"
                    leadsOn: index < tree.active
                    collapsed: index < tree.firstShown
                    joinsNext: index + 1 < tree.firstShown
                }
            }

            // The branches off the current folder, grown in once the cursor rests.
            Item {
                id: branches
                width: parent.width
                height: parent.height
                opacity: tree.branched ? 1 : 0
                Behavior on opacity {
                    enabled: tree.branched
                    NumberAnimation { duration: 120 }
                }

                // Dotted lines, on a checkerboard of the line's width so every
                // segment and corner falls on the same dots.
                Canvas {
                    id: wiresCanvas
                    readonly property real cell: tree.lineWidth
                    x: Math.floor(tree.branchLeft / cell) * cell
                    width: Math.max(1, tree.branchRight - x)
                    height: parent.height
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.reset()
                        ctx.fillStyle = root.primaryColor
                        var c = cell
                        var ox = x
                        // Cell row 0 is the spine's own line.
                        var oy = tree.spine - c / 2
                        function dots(gx0, gx1, gy0, gy1, every) {
                            for (var gx = Math.min(gx0, gx1); gx <= Math.max(gx0, gx1); ++gx)
                                for (var gy = Math.max(Math.min(gy0, gy1), Math.floor(-oy / c));
                                     gy <= Math.min(Math.max(gy0, gy1), Math.ceil((height - oy) / c)); ++gy)
                                    if ((gx + gy) % every === 0)
                                        ctx.fillRect(gx * c - ox, gy * c + oy, c, c)
                        }
                        var ws = tree.wires
                        for (var i = 0; i < ws.length; ++i) {
                            var w = ws[i]
                            var gy0 = Math.round(w.y0 / c)
                            var gy1 = Math.round(w.y1 / c)
                            var gx0 = Math.ceil(w.x0 / c)
                            var gx1 = Math.floor(w.x1 / c) - 1
                            // A faint branch's line: half its dots.
                            var every = w.faint ? 4 : 2
                            if (w.lane < 0) {
                                dots(gx0, gx1, gy0, gy0, every)
                            } else {
                                // On a dot where it leaves the folder's row.
                                var gl = Math.round(w.lane / c)
                                if ((gl + gy0) % 2 !== 0) gl += 1
                                dots(gx0, gl, gy0, gy0, every)
                                dots(gl, gl, gy0, gy1, every)
                                dots(gl, gx1, gy1, gy1, every)
                            }
                        }
                    }
                    Connections {
                        target: tree
                        function onWiresChanged() { wiresCanvas.requestPaint() }
                    }
                }

                Repeater {
                    model: tree.blocks
                    Item {
                        id: block
                        required property var modelData
                        x: modelData.x
                        readonly property var visiblePart: tree.visibleRows(modelData)
                        y: tree.spine + modelData.top + visiblePart.first * tree.rowHeight
                        width: rowsColumn.width
                        height: rowsColumn.height
                        // Another folder's entries than the one under the
                        // cursor: faint. Not dithered as other dimmed things
                        // are: over a video behind the menus a dither's dots
                        // would lie on the picture between the letters.
                        opacity: block.modelData.faint ? tree.faintOpacity : 1
                        Column {
                            id: rowsColumn
                            Repeater {
                                model: block.visiblePart.rows
                                Text {
                                    required property var modelData
                                    width: Math.min(implicitWidth, block.modelData.width)
                                    height: tree.rowHeight
                                    verticalAlignment: Text.AlignVCenter
                                    text: modelData.label
                                    elide: Text.ElideRight
                                    color: root.primaryColor
                                    font.family: root.globalFont
                                    font.capitalization: Font.AllUppercase
                                    font.pixelSize: tree.fontSize
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Component.onCompleted: {
        restore(savedTrail || [])
        relayout()
        ready = true
    }
}
