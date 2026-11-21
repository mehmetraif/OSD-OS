.pragma library

// Words built from a name the app doesn't know in advance (a module's, a
// pad's, a player's), written as English has them.
//
//     "To " + Words.possessive(moduleName) + " lists"   // "To Local Files' lists"

// A name with its possessive: "Plex's", and the apostrophe alone after a
// final s, "Local Files'", "Playlists'".
function possessive(name) {
    var text = String(name)
    return /s$/i.test(text) ? text + "'" : text + "'s"
}
