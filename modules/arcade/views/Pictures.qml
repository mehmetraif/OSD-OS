import QtQuick
import Components

// Settings → Arcade → Game Pictures: where the pictures come from (Pictures
// From, the setting, ◄ ► on its line: RetroAchievements with its Web API
// key, progetto-SNAPS through ArcadeDB), Download Pictures, the look through
// every game for them (ArcadeBackend::downloadPictures()), with how far it
// has got, then how many of the folder's games have a picture and whether
// each source is in use.
KeyedPage {
    id: picturesRoot
    subtitle: "Game Pictures"

    readonly property var progress: (arcadeBackend && arcadeBackend.picturesProgress) || ({})
    readonly property bool active: !!progress.active
    // Read again as the folder is read again, as pictures come in and as
    // the look ends: not at each of its steps, as it goes through every game.
    // While pictures come in, once a second at most (recount): the count
    // walks every game, and a download saves several pictures a second.
    property int revision: 0
    readonly property var info: (revision, arcadeBackend ? arcadeBackend.picturesInfo() : ({}))
    onActiveChanged: revision++
    Timer {
        id: recount
        interval: 1000
        onTriggered: picturesRoot.revision++
    }

    // Source first, then Download Pictures, which the cursor opens
    // on; what there is under them.
    keys: ["source", "download", "games", "achievements", "snaps"]
    Component.onCompleted: currentIndex = keys.indexOf("download")

    // Source: the row the manifest keeps in Game Pictures' own
    // (its "settings"), which Settings' page doesn't show, as Settings has
    // it in a list's place: its choices' names over the values saved
    // (modules.<id>.pictures_source), the default where none is or one no
    // longer offered.
    readonly property var sourceRow: {
        var schema = appCore && appCore.get_module_settings_schema
                     ? appCore.get_module_settings_schema(moduleRoot.moduleId) || [] : []
        for (var i = 0; i < schema.length; i++) {
            if (schema[i].key === "game_pictures") {
                var inner = schema[i].settings || []
                for (var j = 0; j < inner.length; j++) {
                    if (inner[j].key === "pictures_source")
                        return inner[j]
                }
            }
        }
        return null
    }
    readonly property var sources: sourceRow ? Array.from(sourceRow.options || []).map(function(o) {
        return typeof o === "string" ? { label: o, value: o } : { label: o[0], value: o[1] }
    }) : []
    property var savedSource: appCore ? appCore.get_setting(moduleRoot.moduleId, "pictures_source") : undefined
    function sourceIndex() {
        var wanted = savedSource !== undefined && savedSource !== null && savedSource !== ""
                     ? String(savedSource) : (sourceRow ? String(sourceRow.default || "") : "")
        for (var i = 0; i < sources.length; i++) {
            if (sources[i].value === wanted)
                return i
        }
        return 0
    }
    function choicesOf(key) { return key === "source" ? sources : [] }
    function chosenOf(key) { return key === "source" ? sourceIndex() : 0 }
    function pick(key, value) {
        if (key === "source" && appCore && appCore.save_setting(moduleRoot.moduleId, "pictures_source", value)) {
            savedSource = value
            revision++
        }
    }

    function labelOf(key) {
        switch (key) {
        case "source": return sourceRow ? sourceRow.label : "Source"
        case "games": return "Games With Pictures"
        case "achievements": return "RetroAchievements"
        case "snaps": return "progetto-SNAPS"
        case "download": return "Download Pictures"
        }
        return ""
    }
    // What the look is up to: the pass it is on and how far, the games
    // still to try again, or how the last one ended.
    function progressValue() {
        if (!active)
            return progress.result || ""
        if (progress.again)
            return "Trying Again Soon"
        if (!progress.total)
            return "Starting"
        return progress.source + " " + Math.min(progress.done, progress.total) + "/" + progress.total
    }
    function valueOf(key) {
        switch (key) {
        case "games": return info.games > 0 ? info.withPictures + "/" + info.games : "None"
        case "achievements":
            return !info.achievements ? "Off" : info.key === "none" ? "No Web API Key"
                 : info.key === "refused" ? "Key Refused" : "On"
        case "snaps": return info.snaps ? "On" : "Off"
        case "download": return progressValue()
        }
        return ""
    }
    // A row's help as Settings shows a manifest's: its first line, then the
    // bracket of the value on the row.
    function sourceHelp() {
        var text = String(sourceRow ? sourceRow.description || "" : "")
        var cut = text.indexOf("\n")
        var first = cut < 0 ? text : text.slice(0, cut)
        var rest = cut < 0 ? "" : text.slice(cut + 1).replace(/\n/g, "  ")
        var shown = sources[sourceIndex()]
        var at = shown ? rest.indexOf("[" + shown.label.toUpperCase() + "]") : -1
        if (at < 0)
            return first
        var end = rest.indexOf("  [", at + 1)
        return first + "\n" + rest.slice(at, end < 0 ? rest.length : end)
    }
    function descriptionOf(key) {
        switch (key) {
        case "source":
            return sourceHelp()
        case "games":
            return info.writable === false ? "The games folder's snap folder can't be written: no picture can be downloaded into it"
                 : "The games with a picture of any kind: snap/<set>.png, those numbered after the set, titles/<set>.png, or MAME's own snapshot"
        case "achievements":
            return "Its title screen, a screenshot in play and its box art, of the games RetroAchievements has, with the Web API key of Settings → Arcade → RetroAchievements Account • Source, above, says whether it is used"
        case "snaps":
            return "A screenshot in play and the title screen of nearly every MAME game, from progetto-SNAPS through ArcadeDB (adb.arcadeitalia.net), for the games still without a picture • Source, above, says whether it is used"
        case "download":
            return active ? "Every game's pictures are being looked for in the background, a game a second; a game played holds it until the game ends"
                          : "Look for every game's pictures: RetroAchievements' first, then progetto-SNAPS' for the games still without any, a game a second in the background; a look cut short goes on at the next start"
        }
        return ""
    }

    Connections {
        target: arcadeBackend
        ignoreUnknownSignals: true
        function onScanningChanged() { picturesRoot.revision++ }
        function onPicturesChanged(set) { if (!recount.running) recount.start() }
        function onGamesChanged() { picturesRoot.revision++ }
    }

    function selectWord(key) {
        return key === "download" && !active ? "DOWNLOAD" : ""
    }
    function choose(key) {
        if (arcadeBackend && key === "download" && !active)
            arcadeBackend.downloadPictures()
    }
}
