import QtQuick
import Components

// Settings → Arcade → Game Pictures: how many of the folder's games have a
// picture, where the pictures come from (Pictures From: RetroAchievements
// with its Web API key, progetto-SNAPS through ArcadeDB), the folders they
// go in, and Download Pictures, the look through every game for them
// (ArcadeBackend::downloadPictures()), with how far it has got.
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

    keys: ["games", "achievements", "snaps", "download"]

    function labelOf(key) {
        switch (key) {
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
    function descriptionOf(key) {
        switch (key) {
        case "games":
            return info.writable === false ? "The games folder's snap folder can't be written: no picture can be downloaded into it"
                 : "The games with a picture of any kind: snap/<set>.png, those numbered after the set, titles/<set>.png, or MAME's own snapshot"
        case "achievements":
            return "Its title screen, a screenshot in play and its box art, of the games RetroAchievements has, with the Web API key of Settings → Arcade → RetroAchievements Account • Pictures From says whether it is used"
        case "snaps":
            return "A screenshot in play and the title screen of nearly every MAME game, from progetto-SNAPS through ArcadeDB (adb.arcadeitalia.net), for the games still without a picture • Pictures From says whether it is used"
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
