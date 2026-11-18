import QtQuick
import Components

// Shown when the module can't start: no location file, an unusable one, or a
// name that doesn't resolve. Explains exactly what to do rather than failing
// silently on a black screen.
FocusScope {
    id: setupRoot
    // In the layout's frame (Main.qml): what it says in the middle of the
    // pane, beside the column a layout keeps on the screen.
    readonly property bool inFrame: true

    property var navParams: ({})
    signal goBack()

    // "missing" | "empty" | "unreadable" | "notfound" | "network"
    property string reason: navParams.reason || "missing"
    property string locationPath: weatherBackend ? weatherBackend.location_file_path() : ""

    property string headline: {
        switch (reason) {
        case "notfound":   return "LOCATION NOT FOUND"
        case "network":    return "CAN'T REACH THE LOCATION SERVICE"
        case "unreadable": return "LOCATION FILE COULD NOT BE READ"
        case "empty":      return "LOCATION FILE IS EMPTY"
        default:           return "NO LOCATION SET"
        }
    }

    property string detail: {
        switch (reason) {
        case "notfound":
            return "Please check the place's name in Settings, in WEATHER's Location, or in the location file"
        case "network":
            return "Please check your network connection and try again"
        case "unreadable":
            return "Please check the location file's permissions"
        default:
            return "Please type your town, or its latitude and longitude, in Settings, in WEATHER's Location"
        }
    }
    // The file itself, named for the reasons that are about it.
    readonly property bool namesFile: reason !== "network"

    focus: true

    Keys.onPressed: function(event) {
        if (root.isBack(event)) {
            goBack()
            event.accepted = true
        }
    }

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
    }

    Column {
        anchors.centerIn: parent
        // In the middle of the pane, in a layout's frame (Main.qml's).
        anchors.horizontalCenterOffset: root.framed || root.monitorFramed === true
                                        ? root.paneX + root.paneWidth / 2 - root.sw / 2 : 0
        anchors.verticalCenterOffset: root.monitorFramed === true
                                      ? (root.paneTop + root.paneBottom) / 2 - root.sh / 2 : 0
        width: root.paneWidth
        spacing: root.sh * 0.0333333

        Text {
            text: setupRoot.headline
            color: root.secondaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            width: parent.width
            wrapMode: Text.WordWrap
            font.pixelSize: root.sh * 0.05
        }

        Text {
            text: setupRoot.detail
            color: root.tertiaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            width: parent.width
            wrapMode: Text.WordWrap
            font.pixelSize: root.sh * 0.0333333
        }

        // The file's path as it is: a path is case-sensitive.
        Text {
            visible: setupRoot.namesFile && setupRoot.locationPath !== ""
            text: setupRoot.locationPath
            color: root.tertiaryColor
            font.family: root.globalFont
            horizontalAlignment: Text.AlignHCenter
            width: parent.width
            wrapMode: Text.WrapAnywhere
            font.pixelSize: root.sh * 0.0291667 //14
        }
    }

    HintBar {
        text: root.hints.back + ":BACK"
    }
}
