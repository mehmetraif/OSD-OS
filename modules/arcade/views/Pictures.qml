import QtQuick
import Components

// Settings → Arcade → Game Pictures: how many of the folder's games have a
// picture, where the pictures come from (Pictures From: RetroAchievements
// with its Web API key, progetto-SNAPS through ArcadeDB), the folders they
// go in, and Download Pictures, the look through every game for them
// (ArcadeBackend::downloadPictures()), with how far it has got.
FocusScope {
    id: picturesRoot

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var navParams: ({})

    focus: true

    readonly property var progress: arcadeBackend ? arcadeBackend.picturesProgress : ({})
    readonly property bool active: !!progress.active
    // Read again as the look goes on, and as the folder is read again.
    property int revision: 0
    readonly property var info: (revision, progress, arcadeBackend ? arcadeBackend.picturesInfo() : ({}))

    readonly property var keys: ["games", "achievements", "snaps", "download"]

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
        function onPicturesChanged(set) { picturesRoot.revision++ }
        function onGamesChanged() { picturesRoot.revision++ }
    }

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: "Game Pictures"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    MenuList {
        id: list
        model: picturesRoot.keys
        focus: true

        delegate: MenuRow {
            width: list.width
            height: root.sh * 0.0583333 //28
            label: picturesRoot.labelOf(modelData)
            value: picturesRoot.valueOf(modelData)
            selected: list.currentIndex === index
            keepValue: true
        }

        Keys.onReturnPressed: {
            var key = picturesRoot.keys[currentIndex]
            if (!arcadeBackend)
                return
            if (key === "download" && !picturesRoot.active)
                arcadeBackend.downloadPictures()
        }
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                picturesRoot.goBack()
                event.accepted = true
            }
        }
    }

    HelpLine {
        always: true
        text: picturesRoot.descriptionOf(picturesRoot.keys[list.currentIndex])
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.helpLineMargin
        anchors.leftMargin: root.sw * 0.125 //80
    }

    HintBar {
        readonly property string key: picturesRoot.keys[list.currentIndex] || ""
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE"
              + (key === "download" && !picturesRoot.active ? " " + root.hints.select + ":DOWNLOAD" : "")
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }
}
