import QtQuick

// Settings → Background Effect, or the theme's (root.backgroundEffect):
// something going on behind the menus, over the window's ground and under
// what a view shows. Matrix rain, stars, snow, a checkered flag, a plasma and
// fire fill the window inside its frame (the whole screen without one), the
// fire burning up from the frame's foot. A theme may bring a shader of its own, and draw it from the
// window's top down to the screen's foot ("area": "foot").
//
// Drawn in art pixels and scaled up, so it is as blocky as the menus, and on
// the screen's grid: the ground every dialog lays (OsdGround) draws the same
// picture there as the window's, which carries on under it. Hidden while the
// effects rest (a video playing or loading, root.effectsRest): nothing of it
// ever lies over a video.
//
// OsdGround places it, in screen coordinates: `area` is where it draws.
Item {
    id: fx

    // Where it draws, on the screen.
    property rect area

    visible: root.backgroundShader !== "" && !root.effectsRest

    // Made only while it shows: a shader effect without its shader would
    // draw the default one, which wants a picture this has none of.
    Loader {
        active: fx.visible
        sourceComponent: ShaderEffect {
            width: Math.ceil(fx.width / root.px)
            height: Math.ceil(fx.height / root.px)
            scale: root.px
            transformOrigin: Item.TopLeft
            // Drawn at art-pixel size, then scaled up without smoothing.
            layer.enabled: true
            layer.smooth: false
            fragmentShader: root.backgroundShader
            property size size: Qt.size(width, height)
            property point origin: Qt.point(Math.round(fx.area.x / root.px), Math.round(fx.area.y / root.px))
            property real time: root.fxTime
            property color ink: root.primaryColor
            property color paper: root.surfaceColor
            onStatusChanged: {
                if (status === ShaderEffect.Error)
                    console.warn("[Effect] " + fragmentShader + " can't be used" + (log ? ": " + log : ""))
            }
        }
    }
}
