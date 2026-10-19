import QtQuick

// The title bar every view starts with, the way a deck's on-screen menu does:
// the module's logo, then a solid bar with its name and where you are in it in
// the background colour, and at its right end the status icons (Settings →
// Status Icons). The logo is in the bar's colour, a fifth taller than the bar
// so it stands out of it above and below, with an art pixel of space either
// side of it.
Item {
    id: appBar

    // Custom Properties
    property url iconSource: "../../assets/images/logo.svg"
    property string title: "OSD/OS"
    property string subtitle: ""
    // A note at the bar's right end, before the status icons: Settings'
    // address on the network.
    property string note: ""

    readonly property bool hasLogo: iconSource.toString() !== "" && logo.status === Image.Ready
    // No room for the subtitle beside a title that fills the bar.
    readonly property bool subtitleShown: subtitle !== "" && !titleClip.clip
                                          && titleClip.width + 2 * content.spacing + root.px < content.width
    // A fifth taller than the bar, in whole art pixels.
    readonly property real logoHeight: Math.round(height * 1.2 / root.px) * root.px

    // Fits the standard screen gutter — 80px (root.sw * 0.125) on each side.
    // The subtitle elides when it would overflow this width; a title too
    // long for it scrolls.
    width: root.sw * 0.75 //480
    height: content.height + 2 * root.px

    // The logo's own ground, the background colour: through the gaps in its
    // drawing, a video playing behind the menus would show otherwise.
    Rectangle {
        visible: appBar.hasLogo
        x: logo.x
        y: logo.y
        width: logo.width
        height: logo.height
        color: root.surfaceColor
        antialiasing: false
    }

    Image {
        id: logo
        visible: appBar.hasLogo
        x: root.px
        y: Math.round((appBar.height - height) / 2)
        height: appBar.logoHeight
        width: implicitWidth
        sourceSize.height: appBar.logoHeight
        // Drawn by OsdIconProvider in the bar's colour, from the original at
        // this height, or from the skin's icon of its name (Settings → Skin).
        // Resolved here, so a path relative to this file works as it always
        // has.
        source: appBar.iconSource.toString() !== "" && appBar.logoHeight > 0
                ? "image://osdicon/" + root.primaryColor.toString().replace("#", "")
                  + "/" + root.skinIcon(Qt.resolvedUrl(appBar.iconSource))
                : ""
    }

    Rectangle {
        id: bar
        x: appBar.hasLogo ? logo.x + logo.width + root.px : 0
        width: appBar.width - x
        height: appBar.height
        // The skin's picture of the bar, when it has one (Settings → Skin).
        color: skinnedBar.shown ? "transparent" : root.primaryColor
        antialiasing: false

        SkinImage {
            id: skinnedBar
            anchors.fill: parent
            part: root.skin.titleBar
        }

        // What is on, at the bar's right end, in the title's colour, and the
        // note before it.
        StatusIcons {
            id: statusRow
            anchors.right: parent.right
            anchors.rightMargin: content.x
            anchors.verticalCenter: parent.verticalCenter
            color: root.surfaceColor
        }
        Text {
            id: noteText
            visible: appBar.note !== ""
            anchors.right: statusRow.visible ? statusRow.left : parent.right
            anchors.rightMargin: statusRow.visible ? content.spacing : content.x
            anchors.verticalCenter: parent.verticalCenter
            text: appBar.note
            textFormat: Text.PlainText
            color: root.surfaceColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.0291667 //14
        }

        Row {
            id: content
            x: root.sw * 0.0125 //8
            // The bar but for the note and the status icons, a spacing short
            // of them.
            width: bar.width - 2 * x - (statusRow.visible ? statusRow.width + spacing : 0)
                   - (noteText.visible ? noteText.width + spacing : 0)
            // A line of title text, subtitle or not.
            height: Math.max(implicitHeight, root.sh * 0.05) //24
            anchors.verticalCenter: parent.verticalCenter
            spacing: root.sw * 0.025 //16

            // The title, scrolled through when it is too long for the bar (a
            // question naming a long display output, say), as a row's value is
            // while it is selected (MenuRow). Cut on the font's character
            // grid, as a row's value is.
            Item {
                id: titleClip
                readonly property real cell: Math.max(1, titleCell.advanceWidth)
                width: Math.min(titleText.implicitWidth, Math.floor(content.width / cell) * cell)
                height: titleText.implicitHeight
                anchors.verticalCenter: parent.verticalCenter
                clip: titleText.implicitWidth > width

                Text {
                    id: titleText
                    textFormat: Text.PlainText
                    text: appBar.title
                    color: root.surfaceColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: root.sh * 0.05 //24
                }
                TextMetrics {
                    id: titleCell
                    font: titleText.font
                    text: "M"
                }

                // Started (and started over) a turn of the event loop after
                // the layout changes, as MenuRow's is: the animation keeps
                // the distance it started with.
                readonly property bool scrolls: appBar.visible && titleText.implicitWidth > width
                readonly property real scrollBy: width - titleText.implicitWidth
                onScrollsChanged: Qt.callLater(updateScroll)
                onScrollByChanged: Qt.callLater(updateScroll)
                Component.onCompleted: Qt.callLater(updateScroll)
                function updateScroll() {
                    if (scrolls)
                        scroll.restart()
                    else
                        scroll.stop()
                }

                SequentialAnimation {
                    id: scroll
                    loops: Animation.Infinite
                    onRunningChanged: if (!running) titleText.x = 0

                    PauseAnimation { duration: 1500 }
                    NumberAnimation {
                        target: titleText
                        property: "x"
                        to: titleClip.scrollBy
                        duration: Math.abs(to) * 20
                    }
                    PauseAnimation { duration: 2000 }
                    PropertyAction { target: titleText; property: "x"; value: 0 }
                }
            }

            Rectangle {
                visible: appBar.subtitleShown
                color: root.surfaceColor
                anchors.verticalCenter: parent.verticalCenter
                width: root.px
                height: root.sh * 0.05 //24
                antialiasing: false
            }

            Text {
                visible: appBar.subtitleShown
                textFormat: Text.PlainText
                text: appBar.subtitle
                color: root.surfaceColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                anchors.verticalCenter: parent.verticalCenter
                font.pixelSize: root.sh * 0.0333333 //16
                // x is this Text's Row position, i.e. everything before it
                // (title, separator, spacings) — cap to the bar's remaining space.
                elide: Text.ElideRight
                width: Math.max(0, Math.min(implicitWidth, content.width - x))
            }
        }
    }
}
