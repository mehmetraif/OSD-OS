import QtQuick
import Components

// The screen a game starts behind (Root.qml's playGame(), Play.qml): drawn
// before the game is started, it is what a headless Pi hands MAME or
// RetroArch, and so what the screen shows while the game loads, rather than
// the list it was chosen in, still there as if frozen. In the module's look,
// on the OSD's ground: the Arcade screen's bar saying PLAY ▶, or the title
// bar, and across the middle the game's name, what its name has in brackets
// on a line of its own, LOADING... two lines below, and under that how to
// come back where Start Info Screen says it. Nothing on it moves, as only one
// frame of it is seen there; it stays up until the game ends.
//
//     GameLoading {
//         anchors.fill: parent
//         visible: playing !== null
//         title: "Street Fighter II"
//     }
Item {
    id: loading
    objectName: "gameLoading"

    // The game's name.
    property string title: ""
    // Under LOADING..., smaller, line by line: how to come back; "" for
    // nothing.
    property string note: ""

    // The name, which may take two lines, and what it has in brackets after
    // it (MAME's "(World 910522)", a room's host), on a line of its own.
    readonly property int bracket: title.indexOf(" (")
    readonly property string name: bracket > 0 ? title.substring(0, bracket) : title
    readonly property string detail: bracket > 0 ? title.substring(bracket + 1) : ""

    OsdGround { anchors.fill: parent }

    AppBar {
        visible: !moduleRoot.arcadeScreen
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }
    ArcadeBar {
        visible: moduleRoot.arcadeScreen
        text: "Play"
        icon: "play"
    }

    // In the middle of the screen, whether how to come back follows or not.
    Column {
        id: middle
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: root.sw * 0.75 //480

        Text {
            width: parent.width
            text: loading.name
            textFormat: Text.PlainText
            horizontalAlignment: Text.AlignHCenter
            color: root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.05 //24
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        Text {
            visible: loading.detail !== ""
            width: parent.width
            text: loading.detail
            textFormat: Text.PlainText
            horizontalAlignment: Text.AlignHCenter
            color: root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.05 //24
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        // Two lines' space above it.
        Text {
            id: loadingLine
            width: parent.width
            topPadding: 2 * contentHeight
            text: "Loading..."
            horizontalAlignment: Text.AlignHCenter
            color: root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.05 //24
        }
    }
    Text {
        visible: loading.note !== ""
        anchors.top: middle.bottom
        anchors.topMargin: loadingLine.contentHeight
        x: middle.x
        width: middle.width
        text: loading.note
        horizontalAlignment: Text.AlignHCenter
        color: root.primaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        font.pixelSize: root.sh * 0.0375 //18
        wrapMode: Text.WordWrap
    }
}
