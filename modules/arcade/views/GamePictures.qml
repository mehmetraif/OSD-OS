import QtQuick

// The game's pictures in both of the Arcade screen's looks (ClassicLook's
// frame, OrbLook's orb), one after another as the Arcade screen shows them
// (arcadeScreen.pictures, pictureIndex): two pictures, the one on the
// screen and the next loading behind it, which takes its place once it is
// in (or can't be), so the pictures cut from one to the next. A screenshot
// (its shape near enough the game's screen's, whatever its pixels) fills
// the game's screen, screenWidth by screenHeight in the middle (4:3, a
// vertical game's 3:4, upright); one of another shape (a box, a flyer, a
// marquee) keeps its own, as big as fits. Each is decoded no bigger than
// the screen, in its own shape (sourceSize). Without one, or with one that
// can't be shown, the look says which file it would be (noteShown): not on
// Search, whose keys are in the frame.
Item {
    id: pictures

    property real screenWidth: width
    property real screenHeight: height

    // The picture to show, and the two that show it.
    readonly property string wanted: arcadeScreen.pictures.length > 0
                                     ? arcadeScreen.pictures[arcadeScreen.pictureIndex % arcadeScreen.pictures.length]
                                     : ""
    property Image shown: pictureA
    readonly property Image behind: shown === pictureA ? pictureB : pictureA
    readonly property bool noteShown: (wanted === "" || shown.status === Image.Error) && arcadeScreen.current !== null
                                      && arcadeScreen.filterId !== "search"
    onWantedChanged: showWanted()
    Component.onCompleted: showWanted()
    function showWanted() {
        if (wanted === "") {
            pictureA.source = ""
            pictureB.source = ""
        } else if (shown.source.toString() !== wanted) {
            behind.source = wanted
            takeOver(behind)
        }
    }
    function takeOver(picture) {
        if (picture === behind && picture.source.toString() === wanted
            && (picture.status === Image.Ready || picture.status === Image.Error))
            shown = picture
    }

    component Picture: Image {
        id: picture
        readonly property real shape: implicitHeight > 0 ? implicitWidth / implicitHeight : 0
        readonly property bool screenLike: shape === 0 || (arcadeScreen.details.vertical ? shape >= 0.55 && shape <= 1
                                                                                         : shape >= 1 && shape <= 2)
        visible: pictures.shown === picture && status === Image.Ready
        width: screenLike ? pictures.screenWidth : Math.min(pictures.screenWidth, Math.round(pictures.screenHeight * shape))
        height: screenLike ? pictures.screenHeight : Math.min(pictures.screenHeight, Math.round(pictures.screenWidth / shape))
        x: Math.round((pictures.width - width) / 2)
        y: Math.round((pictures.height - height) / 2)
        sourceSize.width: pictures.screenWidth
        sourceSize.height: pictures.screenHeight
        asynchronous: true
        onStatusChanged: pictures.takeOver(picture)
    }
    Picture { id: pictureA }
    Picture { id: pictureB }
}
