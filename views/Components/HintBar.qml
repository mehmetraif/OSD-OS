import QtQuick

// The footer every view carries: its hint line in the background colour on a
// solid bar across the screen's safe width, the way a deck's menu ends with
// "SELECT WITH (▲▼) AND (OK)". It is a Text, so a view sets its text and anchors
// exactly as on one; the bar keeps its own font size, a size a CRT reads, and
// steps it down only as far as a long hint line needs to fit the safe width.
// Settings' HINT BAR (root.hintBar) hides every one at once: by its opacity,
// so a view's own visible binding holds, and the window keeps its shape.
Text {
    id: hint

    opacity: root.hintBar ? 1 : 0

    readonly property real largest: root.sh * 0.0375 //18
    readonly property real smallest: root.sh * 0.0291667 //14

    width: root.sw * 0.75 //480
    color: root.surfaceColor
    font.family: root.globalFont
    font.pixelSize: Math.max(smallest, Math.min(largest,
        Math.floor(largest * (width - leftPadding - rightPadding) / Math.max(1, probe.advanceWidth))))
    elide: Text.ElideRight
    leftPadding: root.sw * 0.0125 //8
    rightPadding: root.sw * 0.0125 //8
    topPadding: root.px
    bottomPadding: root.px

    // The hint line's width at the largest size. TextMetrics rather than
    // FontMetrics.advanceWidth(), so the size follows the font once it is set.
    TextMetrics {
        id: probe
        font.family: root.globalFont
        font.pixelSize: hint.largest
        text: hint.text
    }

    // The bar, or the skin's picture of it (Settings → Skin).
    SkinImage {
        id: skinnedBar
        z: -1
        anchors.fill: parent
        part: root.skin.hintBar
    }
    Rectangle {
        z: -1
        anchors.fill: parent
        visible: !skinnedBar.shown
        color: root.primaryColor
        antialiasing: false
    }
}
