import QtQuick
import Components

// Settings → Arcade → Favorites: how many games are favourites, Export, which
// writes them to osdos-favorites.txt in the games folder (a game's set a
// line, its title after a #: on an OSD/OS card the OSD-OS partition, which a
// computer opens), and Import, which adds those such a file names that
// aren't favourites yet, as many as the list holds
// (ArcadeBackend::exportFavorites(), importFavorites()). The help line says
// how the last one went, the file named.
FocusScope {
    id: favoritesRoot
    // In a monitor layout's frame, as Settings' own pages are (Root.qml's
    // inFrame).
    readonly property bool inFrame: true

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var navParams: ({})

    focus: true

    // Read again after an export or an import.
    property int revision: 0
    readonly property int count: (revision, arcadeBackend ? arcadeBackend.favoritesCount() : 0)
    readonly property string file: arcadeBackend ? arcadeBackend.favoritesFile() : ""
    // How the last export or import went.
    property string message: ""

    readonly property var keys: ["count", "export", "import"]

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

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: "Favorites"
    }

    MenuList {
        id: list
        model: favoritesRoot.keys
        focus: true
        onCurrentIndexChanged: favoritesRoot.message = ""

        delegate: MenuRow {
            width: list.width
            height: root.menuRowHeight
            label: favoritesRoot.labelOf(modelData)
            value: favoritesRoot.valueOf(modelData)
            selected: list.currentIndex === index
        }

        Keys.onReturnPressed: {
            var key = favoritesRoot.keys[currentIndex]
            if (!arcadeBackend)
                return
            var done = key === "export" ? arcadeBackend.exportFavorites()
                     : key === "import" ? arcadeBackend.importFavorites() : null
            if (!done)
                return
            favoritesRoot.message = done.message || ""
            favoritesRoot.revision++
        }
        Keys.onPressed: function(event) {
            if (root.isBack(event)) {
                favoritesRoot.goBack()
                event.accepted = true
            }
        }
    }

    HelpLine {
        always: true
        text: favoritesRoot.descriptionOf(favoritesRoot.keys[list.currentIndex])
    }

    HintBar {
        readonly property string key: favoritesRoot.keys[list.currentIndex] || ""
        text: root.hintLine(["back:BACK", "navigate:NAVIGATE",
                             key === "export" ? "select:EXPORT" : key === "import" ? "select:IMPORT" : ""])
    }
}
