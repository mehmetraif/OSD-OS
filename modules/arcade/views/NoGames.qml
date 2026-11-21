import QtQuick

// The Arcade module's screens with nothing to list: no games, what stands in
// the way of the list (the module's problem), and where the games go,
// unless that is the folder that can't be read; while MAME is being asked
// about them (reading), that it is. Where MAME is what is missing and the
// system can download it (canDownload: Windows), that select does, how far
// the download is, or why it failed; the screen's own keys start it.
Column {
    id: noGames

    property bool reading: false
    readonly property string problem: moduleRoot.problem
    readonly property bool folderReadable: moduleRoot.folderReadable
    readonly property string gamesFolder: moduleRoot.gamesFolder
    readonly property bool canDownload: moduleRoot.canDownloadMame
    readonly property bool downloading: moduleRoot.downloadingMame
    readonly property int progress: arcadeBackend ? arcadeBackend.mameProgress : 0
    readonly property string failure: arcadeBackend ? arcadeBackend.mameMessage : ""

    anchors.centerIn: parent
    width: root.sw * 0.75 //480
    spacing: root.sh * 0.0333333 //16

    Text {
        width: parent.width
        text: noGames.reading ? "Reading the games…" : "No games found"
        color: root.primaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: root.sh * 0.05 //24
    }
    Text {
        visible: !noGames.reading && noGames.problem !== ""
        width: parent.width
        text: noGames.problem
        color: root.primaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        font.pixelSize: root.sh * 0.0375 //18
    }
    // How to put MAME on, for this system (AppCore::installHint()).
    Text {
        visible: !noGames.reading && moduleRoot.mameMissing
        width: parent.width
        text: visible ? appCore.installHint("mame") : ""
        color: root.primaryColor
        font.family: root.globalFont
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        font.pixelSize: root.sh * 0.0333333 //16
    }
    // Or downloaded from here: select starts it, and the line follows it.
    Text {
        visible: !noGames.reading && moduleRoot.mameMissing && (noGames.canDownload || noGames.downloading)
        width: parent.width
        text: noGames.downloading ? "Downloading MAME… " + noGames.progress + "%"
              : noGames.failure !== "" ? "The download failed: " + noGames.failure + " • " + root.hints.select + ": TRY AGAIN"
              : root.hints.select + ": DOWNLOAD MAME"
        color: root.primaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        font.pixelSize: root.sh * 0.0375 //18
    }
    Text {
        visible: !noGames.reading && noGames.folderReadable
        width: parent.width
        text: "Put MAME's ROM sets, a zip file for each game, in " + noGames.gamesFolder
        color: root.primaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        font.pixelSize: root.sh * 0.0375 //18
    }
}
