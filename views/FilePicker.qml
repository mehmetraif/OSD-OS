import QtQuick
import Components

// The picker every setting that names a folder or a file opens: the same
// horizontal tree Local Files is browsed with (TreeBrowser), from its places
// (home, where drives and partitions are mounted, the root) down. A folder is
// picked with its first entry, USE THIS FOLDER, a file with select on it, and
// the default (when there is one) with the entry before the places. The
// choice is saved to the setting and back comes back; back at the top leaves
// it as it was. It opens down to the folder or file chosen now.
//
// navParams:
//   moduleId, settingKey  the setting it saves to (moduleId "" for an app one)
//   currentPath           the folder or file chosen now; "" for the default
//   mode                  "folder" (when unset) or "file"
//   types                 with "file": the file types offered (["png", …])
//   defaultLabel          the entry that saves "" (a module's own folder,
//                         OSD/OS's logo); none when unset
//   label                 what is picked, for the title bar
FocusScope {
    id: picker

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var navParams: ({})
    readonly property bool pickFile: navParams.mode === "file"
    // Read once: what the tree starts from.
    readonly property var places: appCore ? appCore.filePlaces() : []

    focus: true

    function choose(value) {
        appCore.save_setting(navParams.moduleId || "", navParams.settingKey, value)
        picker.goBack()
    }

    // The tree's entries: at its root the default and the places, in a folder
    // USE THIS FOLDER (picking folders) and what is in it.
    function entriesOf(path, asynchronous) {
        if (!appCore)
            return []
        if (path === "places") {
            var top = []
            if (navParams.defaultLabel)
                top.push({ name: navParams.defaultLabel, path: "default:", kind: "default" })
            for (var i = 0; i < places.length; i++)
                top.push({ name: places[i].name, path: places[i].path, isFolder: true })
            return top
        }
        var types = pickFile ? (navParams.types || []) : []
        var entries = asynchronous ? appCore.requestFolderEntries(path, types) : appCore.folderEntries(path, types)
        if (entries === undefined || entries === null) return null
        // Not in a branch's glance at a folder: there it would be every
        // folder's first line.
        if (!pickFile)
            entries.unshift({ name: "Use This Folder", path: "choose:" + path, kind: "choose", target: path,
                              branchHidden: true })
        return entries
    }

    // The tree opened down to what is chosen now, from the deepest place that
    // holds it: the cursor on USE THIS FOLDER in a folder, on a file in its
    // folder, on the default when it is the default.
    function trailTo(target) {
        var trail = [{ path: "places", sel: 0, name: "", pushed: false }]
        target = String(target || "")
        if (target.length > 1 && target.charAt(target.length - 1) === "/")
            target = target.substring(0, target.length - 1)
        if (target === "")
            return trail
        var top = entriesOf("places")
        var best = -1
        for (var i = 0; i < top.length; i++) {
            var p = top[i].path
            if (top[i].isFolder && (target === p || target.indexOf(p === "/" ? "/" : p + "/") === 0)
                    && (best < 0 || p.length > top[best].path.length))
                best = i
        }
        if (best < 0)
            return trail
        trail[0].sel = best
        var folder = top[best].path
        trail.push({ path: folder, sel: 0, name: top[best].name, pushed: false })
        var rest = target.substring(folder.length).split("/")
        for (var j = 0; j < rest.length; j++) {
            if (rest[j] === "")
                continue
            var next = (folder === "/" ? "" : folder) + "/" + rest[j]
            var entries = entriesOf(folder)
            var idx = -1
            for (var k = 0; k < entries.length && idx < 0; k++) {
                if (!entries[k].kind && entries[k].path === next)
                    idx = k
            }
            if (idx < 0)
                break
            trail[trail.length - 1].sel = idx
            if (!entries[idx].isFolder)
                break
            folder = next
            trail.push({ path: folder, sel: 0, name: entries[idx].name, pushed: false })
        }
        return trail
    }

    Connections {
        target: appCore
        function onFolderEntriesReady(path) { tree.refresh(path) }
    }

    AppBar {
        iconSource: "../../assets/images/settings.svg"
        title: picker.navParams.label || "Settings"
        subtitle: tree.folderName
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    TreeBrowser {
        expandedFolderPreviews: true
        id: tree
        anchors.fill: parent
        focus: true
        rootPath: { if (appCore) appCore.clearFolderCache(); return "places" }
        fetch: function(path, preview) { return picker.entriesOf(path, true) }
        savedTrail: picker.trailTo(picker.navParams.currentPath)
        onActivated: function(item) {
            if (item.kind === "default")
                picker.choose("")
            else if (item.kind === "choose")
                picker.choose(item.target)
            else if (picker.pickFile)
                picker.choose(item.path)
        }
        onLeaveRequested: picker.goBack()
    }

    // Select opens a folder, and picks anything else.
    HintBar {
        readonly property var entry: tree.currentEntry
        text: root.hints.back + ":BACK " + root.hints.arrows + ":NAVIGATE " + root.hints.select
              + (entry && !entry.isFolder ? ":CHOOSE" : ":OPEN")
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }
}
