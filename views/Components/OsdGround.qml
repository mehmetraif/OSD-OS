import QtQuick

// The ground the OSD is drawn on, as Settings' OSD BACKGROUND has it
// (root.osdBackground):
//   FULL    the colour scheme's background all over (the default);
//   WINDOW  a window of it behind what a view shows (root.osdWindow), black
//           around it, framed as Settings' WINDOW FRAME has it
//           (root.osdFrame): ON a line in the scheme's colour, or the skin's
//           frame (Settings → Look → Skin, root.skin.window); OFF none; SHADOW the
//           frame and a shadow below and to the right, like a DOS window's;
//   OFF     none: black, the menus in the scheme's lighter colour on it, as a
//           deck's OSD with nothing playing (root.surfaceColor is then black).
//
// Main.qml lays one under every view. A full-screen layer that hides the view
// under it (PromptScreen, the on-screen keyboard, an info screen) lays its own,
// so the window goes on under it just as it was. It is laid out in screen
// coordinates, in a layer that fills the screen: anchors.fill its area, or a
// strip of it (the keyboard's, below the title bar), and the window shows
// through that strip as it lies on the screen.
//
// Over the window's ground goes the background effect (Settings → Look → Effects → Background
// Effect, or the theme's: BackgroundFx), the same picture under every ground.
// One laid over a view `cover`s it: the selector effect plays round a selected
// line in front of it only (Main.qml's root.selector).
//
//     OsdGround { anchors.fill: parent }
Item {
    id: ground

    // Laid over a view, hiding it (true but for Main.qml's, under them all).
    property bool cover: true
    readonly property bool covering: cover && visible
    onCoveringChanged: root.coverShown(ground, covering)
    Component.onCompleted: if (covering) root.coverShown(ground, true)
    Component.onDestruction: root.coverShown(ground, false)

    // Black around the window. Main.qml turns it off over a video playing
    // behind the menus, which then shows there whole.
    property bool surround: true

    // Where the stage's top lies in this ground's parent, and the whole of
    // the screen in the stage's coordinates: the stage itself for a layer on
    // it; Main.qml's, under the views, is the screen's on a screen taller
    // than wide, the stage a band across its middle (root.stageY down).
    property real bandY: 0
    property rect wholeArea: Qt.rect(0, 0, root.sw, root.sh)

    readonly property bool windowed: root.osdBackground === "Window"
    readonly property bool framed: root.osdFrame !== "Off"
    // A DOS window's shadow, a few art pixels deep both ways, dithered: it
    // darkens the video it falls on, every other art pixel black; on the black
    // around the window, where there is nothing to darken, it is a half tone
    // of the window's colour instead.
    readonly property bool shadowed: windowed && root.osdFrame === "Shadow"
    readonly property real shadowDepth: 6 * root.px

    clip: windowed

    Rectangle {
        anchors.fill: parent
        visible: !ground.windowed || ground.surround
        color: ground.windowed ? "#000000" : root.surfaceColor
    }

    // The shadow's two arms, down the window's right side and along its foot
    // (only those: over a video the window lets the picture through), one
    // checkerboard across both.
    Repeater {
        model: ground.shadowed ? [
            Qt.rect(root.osdWindow.x + root.osdWindow.width, root.osdWindow.y + ground.shadowDepth,
                    ground.shadowDepth, root.osdWindow.height),
            Qt.rect(root.osdWindow.x + ground.shadowDepth, root.osdWindow.y + root.osdWindow.height,
                    root.osdWindow.width - ground.shadowDepth, ground.shadowDepth)
        ] : []
        Dither {
            required property rect modelData
            x: modelData.x - ground.x
            y: modelData.y + ground.bandY - ground.y
            width: modelData.width
            height: modelData.height
            color: ground.surround ? root.surfaceColor : "#000000"
            phase: Math.round(modelData.x / root.px + modelData.y / root.px) % 2
        }
    }

    // The window, framed: placed where it lies on the screen, whatever part of
    // the screen this ground covers.
    Rectangle {
        visible: ground.windowed
        x: root.osdWindow.x - ground.x
        y: root.osdWindow.y + ground.bandY - ground.y
        width: root.osdWindow.width
        height: root.osdWindow.height
        // The skin's frame draws the window whole, its middle too: what the
        // picture leaves clear (a rounded corner) shows what is around it.
        color: ground.framed && skinnedFrame.shown ? "transparent" : root.surfaceColor
        border.color: root.primaryColor
        border.width: ground.framed && !skinnedFrame.shown ? root.px : 0
        antialiasing: false

        SkinImage {
            id: skinnedFrame
            anchors.fill: parent
            visible: ground.framed
            part: root.skin.window
        }
    }

    // The background effect: in the window, inside its frame (the skin's
    // border, or the line's art pixel), or over the whole screen without
    // one; or, for a theme's along the foot ("area": "foot"), from the
    // screen's foot up through the window.
    BackgroundFx {
        readonly property rect win: ground.windowed ? root.osdWindow : ground.wholeArea
        readonly property var border: ground.windowed && ground.framed
                                      ? (skinnedFrame.shown ? root.skin.window.border : [1, 1, 1, 1]) : [0, 0, 0, 0]
        area: root.backgroundEffect.area === "foot"
              ? Qt.rect(win.x, win.y, win.width, ground.wholeArea.y + ground.wholeArea.height - win.y)
              : Qt.rect(win.x + border[0] * root.px, win.y + border[1] * root.px,
                        win.width - (border[0] + border[2]) * root.px,
                        win.height - (border[1] + border[3]) * root.px)
        x: area.x - ground.x
        y: area.y + ground.bandY - ground.y
        width: area.width
        height: area.height
    }
}
