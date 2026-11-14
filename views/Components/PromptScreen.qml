import QtQuick

// A question or a notice, full screen, in the window every view has: the
// question in the title bar behind a ? (a notice, an error say, behind a !),
// the hint line at the foot, where every view keeps it, and between them, in
// the middle of the space they leave both ways, what it is about and the
// answers. However few lines it has, the bars stay where they are.
//
// In a layout's frame (Main.qml's, Menu Layout's Camcorder) it is asked in
// the layout: the frame's column stays beside it, and what it is about and
// the answers are in the middle of the pane.
//
// It only draws: the host keeps its keys and the cursor (currentIndex), and
// shows it (visible); it tells the host which answer the mouse is over
// (hovered), for the cursor to follow. Items declared inside it go under the
// message, above the answers (BluetoothPrompt's code); they centre themselves
// across its width.
//
//     PromptScreen {
//         visible: overlayVisible
//         title: "Resume playback?"
//         message: itemTitle
//         choices: ["Resume from 0:23", "Start from the beginning"]
//         currentIndex: choiceIndex
//         onHovered: function(index) { choiceIndex = index }
//     }
Item {
    id: prompt

    // The question, or for a notice what happened: in the title bar.
    property string title: ""
    // "question" (a ? in the bar) or "notice" (a !).
    property string kind: "question"
    // Under the bar: what it is about (a title, a device), or a notice's
    // details. It wraps.
    property string message: ""
    // The answers: labels, or maps with a label ({ label, action }). A map
    // with starts: true is one that starts what the window is for (a game),
    // a ▶ before it; { divider: true } is a line between groups of answers,
    // which the cursor passes over (ChoiceOverlay) and the mouse leaves.
    property var choices: []
    property int currentIndex: 0
    // The answer the mouse is over (Main.qml's followPointer()): the host
    // moves its cursor there (onHovered), as it does for ▲ ▼.
    signal hovered(int index)
    function hoverRow(index) {
        if (index >= 0 && index < choices.length && !(choices[index] && choices[index].divider))
            hovered(index)
    }
    // More answers than this show a window of them that follows the cursor,
    // with ▲ / ▼ while some are hidden above or below it; one more with
    // Settings' Hint Bar off, in its room.
    property int maxChoices: root.hintBar ? 5 : 6
    // The hint line: unless set, back, with navigate and select while there
    // are answers.
    property string hint: ""

    default property alias content: extra.data

    anchors.fill: parent

    onCurrentIndexChanged: answers.keep()

    // Over the view it asks in, on the ground every view has (OSD BACKGROUND).
    OsdGround {
        anchors.fill: parent
    }

    // The frame's column again, over that ground, as the shell has it under
    // the view: its icons, the one framed, where it stands.
    Loader {
        active: prompt.visible && root.framed === true
        sourceComponent: IconColumn {
            x: root.frameColumn.x
            y: root.frameColumn.y
            width: root.frameColumn.width
            height: root.frameColumn.height
            model: root.shownFrameIcons
            currentIndex: root.shownFrameIndex
            active: false
        }
    }

    AppBar {
        id: titleBar
        iconSource: prompt.kind === "notice" ? "../../assets/images/notice.svg"
                                             : "../../assets/images/question.svg"
        title: prompt.title
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // The space between the bars, or with the hint bar off, down to where it
    // ends: what is in it is centred there.
    Item {
        id: middle
        anchors.top: titleBar.bottom
        anchors.bottom: root.hintBar ? hintBar.top : hintBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
    }

    Column {
        // In the middle of the pane (the window's, with no frame).
        x: root.paneX
        anchors.verticalCenter: middle.verticalCenter
        width: root.paneWidth
        spacing: root.sh * 0.05 //24

        Text {
            visible: prompt.message !== ""
            width: parent.width
            textFormat: Text.PlainText
            text: prompt.message
            color: root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.0416667 //20
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }

        Item {
            id: extra
            visible: children.length > 0 && childrenRect.height > 0
            width: parent.width
            height: childrenRect.height
        }

        // The answers, as many as maxChoices at a time, the cursor's among
        // them.
        Item {
            visible: prompt.choices.length > 0
            width: parent.width
            height: answers.height

            ListView {
                id: answers
                readonly property real rowHeight: root.sh * 0.0583333 //28
                width: parent.width
                // By the answers it is given, which it has as it is made,
                // not by its count, which it has only once made: a window
                // made as its question opens (ChoiceOverlay) then draws all
                // its answers at once, not from the next frame on.
                height: Math.min(prompt.choices.length, prompt.maxChoices) * rowHeight
                clip: true
                interactive: false
                highlightFollowsCurrentItem: false
                model: prompt.choices

                // The cursor's answer in view, as the cursor moves and as the
                // answers change.
                function keep() {
                    if (prompt.currentIndex >= 0 && prompt.currentIndex < count)
                        positionViewAtIndex(prompt.currentIndex, ListView.Contain)
                }
                onCountChanged: Qt.callLater(keep)

                delegate: Item {
                    id: answer
                    readonly property bool current: index === prompt.currentIndex
                    readonly property bool divider: typeof modelData === "object" && !!modelData.divider
                    readonly property bool starts: typeof modelData === "object" && !!modelData.starts
                    width: answers.width
                    height: answers.rowHeight

                    // A line between groups: a rule across the middle half.
                    Rectangle {
                        visible: answer.divider
                        anchors.centerIn: parent
                        width: Math.round(parent.width * 0.5)
                        height: root.px
                        color: root.primaryColor
                        antialiasing: false
                    }

                    // The answer under the cursor, boxed, or in the skin's
                    // picture of a selected line (Settings → Look → Skin):
                    // its ▶ and its label.
                    SelectionBox {
                        anchors.fill: line
                        visible: answer.current && !answer.divider
                    }

                    Row {
                        id: line
                        visible: !answer.divider
                        anchors.centerIn: parent
                        leftPadding: root.sw * 0.009375 //6
                        rightPadding: root.sw * 0.009375 //6
                        spacing: answer.starts ? root.sw * 0.0125 : 0 //8

                        PixelIcon {
                            visible: answer.starts
                            name: "play"
                            color: answer.current ? root.surfaceColor : root.primaryColor
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Text {
                            id: label
                            width: Math.min(implicitWidth, answers.width - line.leftPadding - line.rightPadding
                                            - (answer.starts ? line.spacing + root.px * 7 : 0))
                            textFormat: Text.PlainText
                            text: typeof modelData === "string" ? modelData : (modelData.label || "")
                            color: answer.current ? root.surfaceColor : root.primaryColor
                            font.family: root.globalFont
                            font.capitalization: Font.AllUppercase
                            font.pixelSize: root.sh * 0.05 //24
                            elide: Text.ElideRight
                            topPadding: root.sh * 0.0041667 //2
                            bottomPadding: root.sh * 0.00625 //3
                        }
                    }
                }
            }

            // ▲ / ▼ while answers are hidden above or below.
            ScrollMarks {
                anchors.fill: answers
                list: answers
            }
        }
    }

    HintBar {
        id: hintBar
        text: prompt.hint !== "" ? prompt.hint
              : prompt.choices.length > 0
                ? root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.select + ":SELECT"
                : root.hints.back + ":BACK"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }
}
