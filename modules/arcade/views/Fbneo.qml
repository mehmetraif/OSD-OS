import QtQuick
import Components

// Settings → Arcade → FinalBurn Neo: the second engine's parts, RetroArch and
// the FinalBurn Neo core with its list of games, whether each is there, and
// the core's download (ArcadeBackend::downloadFbneo()) for an install the
// OSD/OS image didn't bring it with. Its licence, which comes with it, is in
// Settings → About, with the rest of the arcade's.
FocusScope {
    id: fbneoRoot

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var navParams: ({})

    focus: true

    readonly property bool downloading: arcadeBackend ? arcadeBackend.fbneoDownloading : false
    readonly property int progress: arcadeBackend ? arcadeBackend.fbneoProgress : 0
    readonly property string failure: arcadeBackend ? arcadeBackend.fbneoMessage : ""
    // Read again as a download ends, and as the list is read again after.
    property int revision: 0
    readonly property var info: (revision, downloading, arcadeBackend ? arcadeBackend.fbneoInfo() : ({}))

    // The page's lines, by key: the list is made again only when a line
    // comes or goes, so the cursor stays where it is as a download goes on.
    property var keys: []
    function updateKeys() {
        var made = ["retroarch", "core", "games"]
        if (info.canDownload)
            made.push("download")
        if (JSON.stringify(made) === JSON.stringify(keys))
            return
        var at = keys[list.currentIndex]
        keys = made
        // The list made again, on the line it was on, or the first (a line
        // gone: signed in).
        list.currentIndex = Math.max(0, made.indexOf(at))
    }
    onInfoChanged: updateKeys()
    Component.onCompleted: updateKeys()

    function labelOf(key) {
        switch (key) {
        case "retroarch": return "RetroArch"
        case "core": return "FinalBurn Neo"
        case "games": return "Games It Knows"
        case "download": return info.core && info.downloaded ? "Download Again" : "Download"
        }
        return ""
    }
    function valueOf(key) {
        switch (key) {
        case "retroarch": return info.retroarch ? "Installed" : "Not Installed"
        case "core": return !info.core ? "Not Installed" : (info.downloaded ? "Downloaded " + info.coreDate : "Installed")
        case "games": return info.games > 0 ? String(info.games) : "None"
        case "download": return downloading ? progress + "%" : (failure !== "" ? "Failed" : "")
        }
        return ""
    }
    function descriptionOf(key) {
        switch (key) {
        case "retroarch":
            return info.retroarch ? info.retroarch
                                  : "The emulator FinalBurn Neo plays in. " + appCore.installHint("retroarch")
        case "core":
            return (info.core ? info.core : "Its core for RetroArch, with its list of games")
                   + " • Free to use and share, never to sell; its license is in Settings → About"
        case "games":
            return "FinalBurn Neo's arcade games: a set of these plays in it when Online High Scores or Online Play is on, or its options choose it"
        case "download":
            return failure !== "" ? failure
                                  : "The latest core from libretro's build server, its list of games and its licence (some 35 MB)"
        }
        return ""
    }

    Connections {
        target: arcadeBackend
        ignoreUnknownSignals: true
        function onFbneoChanged() { fbneoRoot.revision++ }
        function onScanningChanged() { fbneoRoot.revision++ }
    }

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: "FinalBurn Neo"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    MenuList {
        id: list
        model: fbneoRoot.keys
        focus: true

        delegate: MenuRow {
            width: list.width
            height: root.sh * 0.0583333 //28
            label: fbneoRoot.labelOf(modelData)
            value: fbneoRoot.valueOf(modelData)
            selected: list.currentIndex === index
            keepValue: true
        }

        Keys.onReturnPressed: {
            var key = fbneoRoot.keys[currentIndex]
            if (!arcadeBackend)
                return
            if (key === "download" && !fbneoRoot.downloading)
                arcadeBackend.downloadFbneo()
        }
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                fbneoRoot.goBack()
                event.accepted = true
            }
        }
    }

    HelpLine {
        always: true
        text: fbneoRoot.descriptionOf(fbneoRoot.keys[list.currentIndex])
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.helpLineMargin
        anchors.leftMargin: root.sw * 0.125 //80
    }

    HintBar {
        readonly property string key: fbneoRoot.keys[list.currentIndex] || ""
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE"
              + (key === "download" && !fbneoRoot.downloading ? " " + root.hints.select + ":DOWNLOAD" : "")
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }
}
