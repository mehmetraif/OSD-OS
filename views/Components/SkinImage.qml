import QtQuick

// One of the skin's pictures (Settings → Look → Skin, or the theme's: root.skin) over
// a part of the window: its frame, a selected line, the title bar, the hint
// bar. The skin gives the shape, the colour scheme the colours: the picture is
// drawn in the scheme's two (image://osdskin), light in the scheme's colour,
// dark in its background, clear left clear. Nine slices, its corners and edges
// as the skin's border has them, drawn on art pixels and scaled up by root.px,
// so the pixel art stays crisp at any screen size. shown is false while the
// skin has no picture for it (or it can't be read): the caller draws its own
// then, as it does without a skin.
//
//     SkinImage { id: skinned; anchors.fill: parent; part: root.skin.hintBar }
//     Rectangle { anchors.fill: parent; visible: !skinned.shown; color: root.primaryColor }
Item {
    id: skinImage

    // { source, border: [left, top, right, bottom], tile } (AppCore::skin()),
    // or nothing.
    property var part
    readonly property bool shown: !!(part && part.source) && image.status === Image.Ready
    readonly property int tileMode: !part ? BorderImage.Stretch
                                  : part.tile === "repeat" ? BorderImage.Repeat
                                  : part.tile === "round" ? BorderImage.Round
                                  : BorderImage.Stretch

    BorderImage {
        id: image
        visible: skinImage.shown
        width: skinImage.width / root.px
        height: skinImage.height / root.px
        scale: root.px
        transformOrigin: Item.TopLeft
        source: skinImage.part && skinImage.part.source
                ? "image://osdskin/" + String(root.primaryColor).replace("#", "") + "/"
                  + String(root.surfaceColor).replace("#", "") + "/" + skinImage.part.source
                : ""
        border.left: skinImage.side(0)
        border.top: skinImage.side(1)
        border.right: skinImage.side(2)
        border.bottom: skinImage.side(3)
        horizontalTileMode: skinImage.tileMode
        verticalTileMode: skinImage.tileMode
        smooth: false
    }

    function side(i) {
        var b = part && part.border
        return b && b.length === 4 ? b[i] : 0
    }
}
