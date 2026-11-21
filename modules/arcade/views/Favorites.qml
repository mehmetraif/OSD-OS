import QtQuick
import Components

// Settings → Arcade → Favorites: Export, which
// writes them to osdos-favorites.txt in the games folder (a game's set a
// line, its title after a #: on an OSD/OS card the OSD-OS partition, which a
// computer opens), and Import, which adds those such a file names that
// aren't favourites yet, as many as the list holds
// (ArcadeBackend::exportFavorites(), importFavorites()), then how many games
// are favorites. The help line says how the last one went, the file named.
KeyedPage {
    id: favoritesRoot
    subtitle: "Favorites"

    // Read again after an export or an import.
    property int revision: 0
    readonly property int count: (revision, arcadeBackend ? arcadeBackend.favoritesCount() : 0)
    readonly property string file: arcadeBackend ? arcadeBackend.favoritesFile() : ""
    // How the last export or import went.
    property string message: ""

    // Export and Import first, the cursor on Export; how many there are
    // under them.
    keys: ["export", "import", "count"]
    keepValue: false
    onCurrentIndexChanged: message = ""

    function labelOf(key) {
        switch (key) {
        case "count": return "Favorites"
        case "export": return "Export"
        case "import": return "Import"
        }
        return ""
    }
    function valueOf(key) {
        return key === "count" ? (count === 1 ? "1 Game" : count + " Games") : ""
    }
    function descriptionOf(key) {
        if (message !== "" && key !== "count")
            return message
        switch (key) {
        case "count":
            return "The Arcade's favorites: a game is put on them, or taken off, in its options"
        case "export":
            return "Writes them to " + file + ", a game's set a line, its title after a #: a copy to keep, to edit, or to take to another OSD/OS"
        case "import":
            return "Adds the games " + file + " names that aren't favorites yet, as many as fit (100 at most)"
        }
        return ""
    }

    function selectWord(key) {
        return key === "export" ? "EXPORT" : key === "import" ? "IMPORT" : ""
    }
    function choose(key) {
        if (!arcadeBackend)
            return
        var done = key === "export" ? arcadeBackend.exportFavorites()
                 : key === "import" ? arcadeBackend.importFavorites() : null
        if (!done)
            return
        message = done.message || ""
        revision++
    }
}
