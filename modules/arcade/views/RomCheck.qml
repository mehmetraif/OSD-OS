import QtQuick
import Components

// Settings → Arcade → Check ROMs: whether the folder's games have their files
// whole, as the last check found (ArcadeBackend::romCheckInfo()), and Check
// ROMs itself (ArcadeBackend::checkRoms()), started here alone, with how far
// it has got.
FocusScope {
    id: romCheckRoot

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var navParams: ({})

    focus: true

    readonly property var progress: (arcadeBackend && arcadeBackend.romCheckProgress) || ({})
    readonly property bool active: !!progress.active
    // Read again as a check ends and as the folder is read again: not at
    // each of its steps.
    property int revision: 0
    readonly property var info: (revision, arcadeBackend ? arcadeBackend.romCheckInfo() : ({}))
    onActiveChanged: revision++

    readonly property var keys: ["last", "complete", "fbneoOnly", "mameOnly", "incomplete", "unchecked", "check"]

    function labelOf(key) {
        switch (key) {
        case "last": return "Last Check"
        case "complete": return "Complete"
        case "fbneoOnly": return "FinalBurn Neo Only"
        case "mameOnly": return "MAME Only"
        case "incomplete": return "Incomplete"
        case "unchecked": return "Not Checked"
        case "check": return "Check ROMs"
        }
        return ""
    }
    // Where the check is: the stage it is on and how far, waiting for a
    // game to end, or how the last one ended.
    function progressValue() {
        if (!active)
            return progress.result || ""
        if (progress.held)
            return "Waiting"
        if (!progress.total)
            return "Starting"
        return progress.stage + " " + Math.min(progress.done, progress.total) + "/" + progress.total
    }
    function valueOf(key) {
        switch (key) {
        case "last": return info.checked ? Qt.formatDate(new Date(info.checked), "d MMM yyyy") : "Never"
        case "check": return progressValue()
        }
        return info[key] !== undefined ? String(info[key]) : ""
    }
    function descriptionOf(key) {
        switch (key) {
        case "last":
            return "When the games' files were last checked • What it found holds for a game while its file, its parent's and its BIOS set's are as they were"
        case "complete":
            return "Games whose files are whole for every engine that knows them"
        case "fbneoOnly":
            return "Games missing files for MAME but whole for FinalBurn Neo: they play in FinalBurn Neo"
        case "mameOnly":
            return "Games missing files for FinalBurn Neo but whole for MAME: they play in MAME, and aren't hosted or joined online"
        case "incomplete":
            return "Games missing files for every engine that knows them: their Game Details say how many • Hide Incomplete Games leaves them off the lists"
        case "unchecked":
            return "Games not checked yet, or whose files, MAME or FinalBurn Neo's list have changed since"
        case "check":
            return active ? "Every game's files are being checked: their archives first, then MAME's own check, which waits while a game plays"
                          : "Check every game's files, for MAME and FinalBurn Neo: it reads every game's archive, a few minutes on a Raspberry Pi"
        }
        return ""
    }

    Connections {
        target: arcadeBackend
        ignoreUnknownSignals: true
        function onGamesChanged() { romCheckRoot.revision++ }
        function onScanningChanged() { romCheckRoot.revision++ }
    }

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: "Check ROMs"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    MenuList {
        id: list
        model: romCheckRoot.keys
        focus: true

        delegate: MenuRow {
            width: list.width
            height: root.sh * 0.0583333 //28
            label: romCheckRoot.labelOf(modelData)
            value: romCheckRoot.valueOf(modelData)
            selected: list.currentIndex === index
            keepValue: true
        }

        Keys.onReturnPressed: {
            var key = romCheckRoot.keys[currentIndex]
            if (arcadeBackend && key === "check" && !romCheckRoot.active)
                arcadeBackend.checkRoms()
        }
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                romCheckRoot.goBack()
                event.accepted = true
            }
        }
    }

    HelpLine {
        always: true
        text: romCheckRoot.descriptionOf(romCheckRoot.keys[list.currentIndex])
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.helpLineMargin
        anchors.leftMargin: root.sw * 0.125 //80
    }

    HintBar {
        readonly property string key: romCheckRoot.keys[list.currentIndex] || ""
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE"
              + (key === "check" && !romCheckRoot.active ? " " + root.hints.select + ":CHECK" : "")
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }
}
