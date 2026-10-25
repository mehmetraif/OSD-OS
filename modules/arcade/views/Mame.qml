import QtQuick
import Components

// Settings → Arcade → MAME: the arcade machine emulator the games play in,
// whether it is there and where, whether its hiscore plugin is beside it,
// and on Windows, where no package manager brings it, its download
// (ArcadeBackend::downloadMame()): mamedev's release for 64-bit Windows of
// the version the OSD/OS image has, into OSD/OS's own folder, under MAME's
// line while there is none, or the one downloaded is the one found.
FocusScope {
    id: mameRoot

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var navParams: ({})

    focus: true

    readonly property bool downloading: arcadeBackend ? arcadeBackend.mameDownloading : false
    readonly property int progress: arcadeBackend ? arcadeBackend.mameProgress : 0
    readonly property string failure: arcadeBackend ? arcadeBackend.mameMessage : ""
    // Read again as a download ends, and as the games are read again after.
    property int revision: 0
    readonly property var info: (revision, downloading, arcadeBackend ? arcadeBackend.mameInfo() : ({}))

    // The page's lines, by key: the list is made again only when a line
    // comes or goes, so the cursor stays where it is as a download goes on.
    property var keys: []
    function updateKeys() {
        var made = ["mame"]
        if (info.canDownload && (!info.mame || info.downloaded))
            made.push("download")
        if (info.mame)
            made.push("plugin")
        if (JSON.stringify(made) === JSON.stringify(keys))
            return
        var at = keys[list.currentIndex]
        keys = made
        list.currentIndex = Math.max(0, made.indexOf(at))
    }
    onInfoChanged: updateKeys()
    Component.onCompleted: updateKeys()

    function labelOf(key) {
        switch (key) {
        case "mame": return "MAME"
        case "download": return info.mame ? "Download MAME Again" : "Download MAME"
        case "plugin": return "Hiscore Plugin"
        }
        return ""
    }
    function valueOf(key) {
        switch (key) {
        case "mame": return info.mame ? (info.downloaded ? "Downloaded" : "Installed") : "Not Installed"
        case "download": return downloading ? progress + "%" : (failure !== "" ? "Failed" : "")
        case "plugin": return info.plugins ? "Installed" : "Not Found"
        }
        return ""
    }
    function descriptionOf(key) {
        switch (key) {
        case "mame":
            return info.mame ? info.mame
                             : "The arcade machine emulator the games play in. " + appCore.installHint("mame")
        case "download":
            return failure !== "" ? failure
                 : info.mame ? "MAME " + info.downloadVersion + " for 64-bit Windows from mamedev's releases, the OSD/OS"
                               + " image's version, in place of the one downloaded before, with whatever was put in its"
                               + " folder (some 100 MB to download)"
                             : "MAME " + info.downloadVersion + " for 64-bit Windows from mamedev's releases, the OSD/OS"
                               + " image's version, into OSD/OS's own folder (some 100 MB to download, half a gigabyte"
                               + " unpacked)"
        case "plugin":
            return info.plugins ? "MAME's hiscore plugin keeps each game's high score table, for the High Scores module and"
                                  + " the Arcade screen: " + info.pluginsFolder
                                : "MAME's hiscore plugin, which keeps each game's high score table, isn't beside this MAME:"
                                  + " the games keep no tables without it"
        }
        return ""
    }

    Connections {
        target: arcadeBackend
        ignoreUnknownSignals: true
        function onMameChanged() { mameRoot.revision++ }
        function onScanningChanged() { mameRoot.revision++ }
    }

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: "MAME"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    MenuList {
        id: list
        model: mameRoot.keys
        focus: true

        delegate: MenuRow {
            width: list.width
            height: root.sh * 0.0583333 //28
            label: mameRoot.labelOf(modelData)
            value: mameRoot.valueOf(modelData)
            selected: list.currentIndex === index
            keepValue: true
        }

        Keys.onReturnPressed: {
            var key = mameRoot.keys[currentIndex]
            if (arcadeBackend && key === "download" && !mameRoot.downloading)
                arcadeBackend.downloadMame()
        }
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                mameRoot.goBack()
                event.accepted = true
            }
        }
    }

    HelpLine {
        always: true
        text: mameRoot.descriptionOf(mameRoot.keys[list.currentIndex])
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.helpLineMargin
        anchors.leftMargin: root.sw * 0.125 //80
    }

    HintBar {
        readonly property string key: mameRoot.keys[list.currentIndex] || ""
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE"
              + (key === "download" && !mameRoot.downloading ? " " + root.hints.select + ":DOWNLOAD" : "")
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }
}
