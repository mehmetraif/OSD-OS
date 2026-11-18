import QtQuick

// Putting a video on one of the app's playlists (the Playlists module's), in
// the standard window: from a module's OPTIONS (EntryOptions' ADD TO
// PLAYLIST), or wherever else a module offers it (Jellyfin's and Emby's item
// pages, right on PLAY). It lists every playlist, then NEW ONLINE PLAYLIST
// and NEW OFFLINE PLAYLIST, named on the on-screen keyboard; what became of
// the video then shows in the same window, until back or select closes it.
//
//     PlaylistAdder {
//         id: adder
//         anchors.fill: parent
//         onClosed: list.forceActiveFocus()
//     }
//     … if (adder.available(moduleRoot.moduleId)) adder.offer(moduleRoot.moduleId, entry)
//
// The entry is the video as its module has it (a tree's entry, a server's
// item): see PlaylistsBackend::addEntry.
ChoiceOverlay {
    id: adder

    property string moduleId: ""
    property var entry: null
    // What became of the video, once a playlist is chosen: { title, message }.
    property var outcome: null
    // The playlists, and the new ones: { label, action: "add" | "new", id,
    // name, kind }.
    property var rows: []
    // The on-screen keyboard, there while the window is (naming a playlist).
    readonly property var osk: oskLoader.item
    readonly property bool naming: !!osk && osk.visible

    // The window stays for what became of the video.
    closeOnSelect: false
    promptKind: outcome ? "notice" : "question"
    promptText: outcome ? outcome.title : naming ? "New playlist" : "Add to playlist?"
    subtitleText: outcome ? outcome.message : entry ? (entry.title || entry.name || "") : ""
    choices: outcome ? [] : rows
    hintText: outcome ? root.hintLine(["back:BACK", "select:OK"]) : ""

    // Whether a module's videos can go on a playlist: the Playlists module
    // there and on, and taking them.
    function available(forModule) {
        return typeof playlistsBackend !== "undefined" && !!playlistsBackend && !!appCore
               && appCore.is_module_enabled("com.osdos.playlists") && playlistsBackend.supports(forModule)
    }

    function offer(forModule, item) {
        moduleId = forModule
        entry = item
        outcome = null
        var out = []
        var lists = playlistsBackend.playlists()
        for (var i = 0; i < lists.length; i++)
            out.push({ label: lists[i].name + (lists[i].kind === "offline" ? " (Offline)" : ""),
                       action: "add", id: lists[i].id, name: lists[i].name })
        out.push({ label: "New Online Playlist", action: "new", kind: "online" })
        out.push({ label: "New Offline Playlist", action: "new", kind: "offline" })
        rows = out
        open()
    }

    function choose(action) {
        var row = rows[choiceIndex]
        if (action === "add") {
            addTo(row.id, row.name)
        } else {
            osk.kind = row.kind
            osk.title = row.kind === "offline" ? "Offline Playlist Name" : "Online Playlist Name"
            osk.open("")
        }
    }

    function addTo(playlistId, name) {
        var result = playlistsBackend.addEntry(playlistId, moduleId, entry)
        var title = result.title || (entry ? (entry.title || entry.name || "") : "")
        if (result.ok)
            outcome = { title: "Added to " + name,
                        message: title + (result.downloading ? "\nIt downloads in the background, once" : "") }
        else if (result.reason === "duplicate")
            outcome = { title: "Already on " + name, message: title }
        else
            outcome = { title: "Can't go on a playlist", message: title }
    }

    Loader {
        id: oskLoader
        anchors.fill: parent
        active: adder.visible
        sourceComponent: OnScreenKeyboard {
            property string kind: "online"
            // Closed, it gives the keys back: still this scope's focus, it
            // would take them again as the window does.
            onAccepted: function(text) {
                focus = false
                adder.forceActiveFocus()
                var id = playlistsBackend.createPlaylist(text, kind)
                adder.addTo(id, playlistsBackend.playlist(id).name || text)
            }
            onCanceled: {
                focus = false
                adder.forceActiveFocus()
            }
        }
    }
}
