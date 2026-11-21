import QtQuick
import "EntryFavorites.js" as EntryFavorites

// An entry's options, from right on it in a tree (or on its info screen): put
// it on the module's FAVORITES, or take it off, make it the favourite the
// app plays straight after the boot screen (PLAY AT STARTUP), and put it on a
// playlist (ADD TO PLAYLIST, where the Playlists module takes the module's
// videos: PlaylistAdder, over these options, which close with it, closed()
// once). A host can offer its own as well, ahead of these (firstChoices) or
// after them (moreChoices), acted on in onActivated as with any
// ChoiceOverlay, and refreshes its FAVORITES folder on favoritesEdited.
// FAVORITES and the startup favourite are EntryFavorites.js's, the entries
// kept as offer() was given them.
//
//     EntryOptions {
//         id: options
//         anchors.fill: parent
//         moduleId: moduleRoot.moduleId
//         onFavoritesEdited: tree.refresh("favorites")
//         onClosed: tree.forceActiveFocus()
//     }
//     … options.offer(item)
ChoiceOverlay {
    id: options

    property string moduleId: ""
    // The entry the options are for.
    property var entry: null
    property bool favorite: false
    // Whether it is the startup favourite.
    property bool atStartup: false
    // Whether it can go on a playlist.
    property bool playlists: false
    // The host's own, before FAVORITES' and after them: [{ label, action }].
    // Those with starts: true start the entry (a game played, hosted): they
    // come first, whichever list they are in, a ▶ before each and a line
    // under them (PromptScreen), the rest under the line.
    property var firstChoices: []
    property var moreChoices: []

    signal favoritesEdited()

    function offer(item) {
        entry = item
        favorite = EntryFavorites.isFavorite(appCore, moduleId, item)
        atStartup = EntryFavorites.isAtStartup(appCore, moduleId, item)
        playlists = !item.isFolder && adder.available(moduleId)
        open()
    }

    promptText: "Options"
    subtitleText: entry ? (entry.title || entry.name || "") : ""
    choices: {
        var all = firstChoices
                  .concat(EntryFavorites.choices(favorite, atStartup))
                  .concat(playlists ? [{ label: "Add to Playlist", action: "playlist" }] : [])
                  .concat(moreChoices)
        var starting = all.filter(function(c) { return !!c.starts })
        var rest = all.filter(function(c) { return !c.starts })
        return starting.length > 0 && rest.length > 0
               ? starting.concat([{ divider: true, label: "", action: "" }]).concat(rest)
               : starting.concat(rest)
    }

    // ADD TO PLAYLIST opens the adder over these options, which stay under
    // it and close with it; any other choice closes them first, as a
    // ChoiceOverlay's does.
    function choose(action) {
        if (action === "playlist") {
            adder.offer(moduleId, entry)
            return
        }
        close()
        activated(action)
    }

    onActivated: function(action) {
        if (!entry)
            return
        if (action === "favorite") {
            EntryFavorites.toggleFavorite(appCore, moduleId, entry)
            favoritesEdited()
        } else if (action === "startup") {
            if (EntryFavorites.toggleStartup(appCore, moduleId, entry))
                favoritesEdited()
        }
    }

    PlaylistAdder {
        id: adder
        anchors.fill: parent
        onClosed: options.close()
    }
}
