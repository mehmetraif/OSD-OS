.pragma library

// An entry on its module's FAVORITES, and the favourite the app plays
// straight after the boot screen (PLAY AT STARTUP), for every place that
// offers them: an entry's options (EntryOptions) and a video's own menu
// (Local Files' and YouTube's players). FAVORITES is one of the module's
// lists in AppCore (get_list(moduleId, "favorites")), keeping the entry as
// it is given; the startup favourite is the app setting "startup_favorite":
// { module, path, name }. `core` is appCore; `entry` is the entry as the
// module keeps it ({ path, name, title… }).
//
//     var favorite = EntryFavorites.isFavorite(appCore, moduleId, entry)
//     var atStartup = EntryFavorites.isAtStartup(appCore, moduleId, entry)
//     choices = EntryFavorites.choices(favorite, atStartup)
//     … EntryFavorites.toggleFavorite(appCore, moduleId, entry)

function isFavorite(core, moduleId, entry) {
    return core.list_contains(moduleId, "favorites", entry.path)
}

// Whether the entry is the startup favourite.
function isAtStartup(core, moduleId, entry) {
    var startup = core.get_setting("", "startup_favorite")
    return !!startup && startup.module === moduleId && startup.path === entry.path
}

// The two lines, as they read for the entry now: action "favorite" and
// action "startup".
function choices(favorite, atStartup) {
    return [{ label: favorite ? "Remove from Favorites" : "Add to Favorites", action: "favorite" },
            { label: atStartup ? "Don't Play at Startup" : "Play at Startup", action: "startup" }]
}

// "favorite": on FAVORITES, or off it. The startup favourite is one of the
// favourites, so it stops being that too.
function toggleFavorite(core, moduleId, entry) {
    if (isFavorite(core, moduleId, entry)) {
        var atStartup = isAtStartup(core, moduleId, entry)
        core.remove_from_list(moduleId, "favorites", entry.path)
        if (atStartup)
            core.save_setting("", "startup_favorite", "")
    } else {
        core.add_to_list(moduleId, "favorites", entry, 100)
    }
}

// "startup": the startup favourite, or no longer. Played at startup from
// FAVORITES, it goes there too. True when that put it on FAVORITES.
function toggleStartup(core, moduleId, entry) {
    if (isAtStartup(core, moduleId, entry)) {
        core.save_setting("", "startup_favorite", "")
        return false
    }
    var added = !isFavorite(core, moduleId, entry)
    if (added)
        core.add_to_list(moduleId, "favorites", entry, 100)
    core.save_setting("", "startup_favorite",
                      { module: moduleId, path: entry.path, name: entry.title || entry.name || "" })
    return added
}
