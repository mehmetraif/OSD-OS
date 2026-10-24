import QtQuick

// The Arcade module's screens with nothing to list: no games, what stands in
// the way of the list (ArcadeBackend's problem), and where the games go,
// unless that is the folder that can't be read; while MAME is being asked
// about them (reading), that it is.
Column {
    id: noGames

    property string problem: ""
    property bool folderReadable: true
    property string gamesFolder: ""
    property bool reading: false

    anchors.centerIn: parent
    width: root.sw * 0.75 //480
    spacing: root.sh * 0.0333333 //16

    Text {
        width: parent.width
        text: noGames.reading ? "Reading the games…" : "No games found"
        color: root.secondaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: root.sh * 0.05 //24
    }
    Text {
        visible: !noGames.reading && noGames.problem !== ""
        width: parent.width
        text: noGames.problem
        color: root.secondaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        font.pixelSize: root.sh * 0.0375 //18
    }
    // How to put MAME on, for this system (AppCore::installHint()).
    Text {
        visible: !noGames.reading && noGames.problem === "MAME is not installed"
        width: parent.width
        text: visible ? appCore.installHint("mame") : ""
        color: root.tertiaryColor
        font.family: root.globalFont
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        font.pixelSize: root.sh * 0.0333333 //16
    }
    Text {
        visible: !noGames.reading && noGames.folderReadable
        width: parent.width
        text: "Put MAME's ROM sets, a zip file for each game, in " + noGames.gamesFolder
        color: root.tertiaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        font.pixelSize: root.sh * 0.0375 //18
    }
}
