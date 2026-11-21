import QtQuick

// A question or a notice, full screen, in the window every view has: the
// question in the title bar behind a ? (a notice, an error say, behind a !),
// the hint line at the foot, where every view keeps it, and between them, in
// the middle of the space they leave both ways, what it is about and the
// answers. However few lines it has, the bars stay where they are.
//
// In a layout's frame (Main.qml's, Settings → Look → Menu Layout) it is
// asked in the layout. In Camcorder's the frame's column stays beside it,
// and what it is about and the answers are in the middle of the pane. In a
// monitor layout's the frame stays round it: the question is where the frame
// writes its title, the keys in its guide, the answers in the pane with the
// layout's cursor, over the view's rows. `inLayout` off is one asked over
// the whole screen whatever the layout (a display output's, at startup).
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
    // moves its cursor there (onHovered), as it does for ▲ ▼. A row of the
    // answers' own list alone: the frame's column, drawn again beside them
    // in Camcorder, is a list too, and the mouse over one of its icons is
    // over no answer.
    signal hovered(int index)
    function hoverRow(index, list) {
        if (list !== answers)
            return
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

    // Asked in the layout's frame, where one is up (its ground's inLayout):
    // on unless a host turns it off. And what follows from it (asked):
    // whether a monitor layout's frame is round it, Flat Panel's mixed case,
    // whether Camcorder's column is beside it, and whether what it is about
    // and the answers go in the pane.
    // All four are aliases, declared after `content`, so that `content`
    // keeps its place among the properties: PressPrompt's own `content` is an
    // alias of it, which a QML cache kept from an earlier version holds by
    // that place.
    property alias inLayout: ground.inLayout
    readonly property alias monitor: asked.monitor
    readonly property alias mixedCase: asked.mixedCase
    readonly property alias paned: asked.paned
    QtObject {
        id: asked
        readonly property bool monitor: ground.inLayout && root.monitorInk === true
        readonly property bool mixedCase: monitor && root.flatLayout
        readonly property bool columned: ground.inLayout && root.framed === true
        readonly property bool paned: monitor || columned
    }

    anchors.fill: parent

    onCurrentIndexChanged: answers.keep()

    // Over the view it asks in, on the ground every view has (OSD BACKGROUND).
    OsdGround {
        id: ground
        anchors.fill: parent
        inLayout: true
    }

    // The frame's column again, over that ground, as the shell has it under
    // the view: its icons, the one framed, where it stands. Not for one
    // asked over the whole screen.
    Loader {
        active: prompt.visible && asked.columned
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
        frameTitle: prompt.title
    }

    // The space between the bars, or with the hint bar off, down to where it
    // ends: what is in it is centred there.
    // In a monitor layout's frame, the pane from its rows' top to its foot.
    Item {
        id: middle
        readonly property real startY: prompt.monitor ? root.listY : titleBar.y + titleBar.height
        readonly property real endY: prompt.monitor ? root.paneBottom
                                     : root.hintBar ? hintBar.y : hintBar.y + hintBar.height
        y: startY
        width: parent.width
        height: endY - startY
    }

    Column {
        // In the middle of the pane (the window's, with no frame).
        x: prompt.paned ? root.paneX : root.contentBox.x
        anchors.verticalCenter: middle.verticalCenter
        width: prompt.paned ? root.paneWidth : root.contentBox.width
        spacing: root.sh * 0.05 //24

        Text {
            visible: prompt.message !== ""
            width: parent.width
            textFormat: Text.PlainText
            text: prompt.message
            color: root.primaryColor
            font.family: root.globalFont
            font.capitalization: prompt.mixedCase ? Font.MixedCase : Font.AllUppercase
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
        // them. A line between groups ({ divider: true }) is drawn in a strip
        // of its own, thinner than an answer, and the window has room for it
        // over its maxChoices answers: it never costs one of them. Only
        // whole lines show, and never more than maxChoices answers.
        Item {
            visible: prompt.choices.length > 0
            width: parent.width
            height: answers.windowHeight

            Item {
                id: wholeLines
                // The lines wholly in the window where the list stands now.
                readonly property var span: (answers.contentY, answers.originY, answers.contentHeight,
                                             answers.count, answers.windowHeight, prompt.maxChoices,
                                             answers.shownSpan())
                y: span.top
                width: parent.width
                height: span.bottom - span.top
                clip: true

                ListView {
                    id: answers
                    readonly property real rowHeight: root.menuRowHeight
                    readonly property real ruleHeight: Math.round(rowHeight / 2)
                    y: -wholeLines.y
                    width: parent.width
                    // By the answers it is given, which it has as it is made,
                    // not by its count, which it has only once made: a window
                    // made as its question opens (ChoiceOverlay) then draws
                    // all its answers at once, not from the next frame on.
                    height: windowHeight
                    interactive: false
                    highlightFollowsCurrentItem: false
                    model: prompt.choices

                    function isRule(i) {
                        var c = prompt.choices[i]
                        return c !== null && c !== undefined && typeof c === "object" && !!c.divider
                    }
                    function lineHeight(i) {
                        return isRule(i) ? ruleHeight : rowHeight
                    }
                    // The window's height: the tallest run of up to
                    // maxChoices answers, with the lines among them.
                    readonly property real windowHeight: {
                        var n = prompt.choices.length
                        var tallest = 0
                        for (var s = 0; s < n; s++) {
                            if (isRule(s))
                                continue
                            var h = 0
                            var answered = 0
                            for (var j = s; j < n && answered < prompt.maxChoices; j++) {
                                h += lineHeight(j)
                                if (!isRule(j)) {
                                    answered++
                                    tallest = Math.max(tallest, h)
                                }
                            }
                        }
                        return tallest
                    }
                    // Where the whole lines in the window begin and end, from
                    // its top: the first that starts in it, to the last that
                    // ends in it, maxChoices answers at most. By the lines as
                    // the list has laid them out (those in and around the
                    // window are always made).
                    function shownSpan() {
                        var top = -1
                        var bottom = 0
                        var answered = 0
                        for (var i = 0; i < count; i++) {
                            var item = itemAtIndex(i)
                            if (!item)
                                continue
                            var y = item.y - contentY
                            if (top < 0 && y >= -0.5)
                                top = y
                            if (top < 0)
                                continue
                            if (y + item.height > windowHeight + 0.5 || (!isRule(i) && answered === prompt.maxChoices))
                                break
                            if (!isRule(i))
                                answered++
                            bottom = y + item.height
                        }
                        return top < 0 ? { top: 0, bottom: 0 } : { top: top, bottom: bottom }
                    }

                    // The cursor's answer in view, as the cursor moves and as
                    // the answers change.
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
                        height: divider ? answers.ruleHeight : answers.rowHeight

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
                            id: answerBox
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
                                color: answer.current ? answerBox.ink : root.primaryColor
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                id: label
                                width: Math.min(implicitWidth, answers.width - line.leftPadding - line.rightPadding
                                                - (answer.starts ? line.spacing + root.px * 7 : 0))
                                textFormat: Text.PlainText
                                text: typeof modelData === "string" ? modelData : (modelData.label || "")
                                color: answer.current ? answerBox.ink : root.primaryColor
                                font.family: root.globalFont
                                font.capitalization: prompt.mixedCase ? Font.MixedCase : Font.AllUppercase
                                font.pixelSize: root.sh * 0.05 //24
                                elide: Text.ElideRight
                                topPadding: root.sh * 0.0041667 //2
                                bottomPadding: root.sh * 0.00625 //3
                            }
                        }
                    }
                }
            }

            // ▲ / ▼ while answers are hidden above or below.
            ScrollMarks {
                anchors.fill: parent
                list: answers
                pages: false
            }
        }

        // What the answer under the cursor means, for an answer that says
        // ({ label, action, description }: Host LAN Game's advice), its
        // lines one under another. The room the longest of them takes is
        // kept while any answer has one (each laid out unseen, `sizes`), so
        // the answers stay where they are as the cursor moves.
        Item {
            readonly property var notes: Array.from(prompt.choices || []).map(function(c) {
                return c !== null && typeof c === "object" && c.description ? String(c.description) : ""
            })
            visible: notes.some(function(n) { return n !== "" })
            width: parent.width
            height: sizes.childrenRect.height

            Item {
                id: sizes
                visible: false
                width: parent.width
                Repeater {
                    model: parent.parent.notes
                    Text {
                        required property string modelData
                        width: sizes.width
                        textFormat: Text.PlainText
                        text: modelData
                        font: note.font
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Text {
                id: note
                objectName: "choiceNote"
                readonly property var choice: prompt.choices[prompt.currentIndex]
                width: parent.width
                textFormat: Text.PlainText
                text: choice !== undefined && choice !== null && typeof choice === "object" && choice.description
                      ? String(choice.description) : ""
                color: root.primaryColor
                font.family: root.globalFont
                font.capitalization: prompt.mixedCase ? Font.MixedCase : Font.AllUppercase
                font.pixelSize: root.sh * 0.0375 //18
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }

    HintBar {
        id: hintBar
        text: prompt.hint !== "" ? prompt.hint
              : prompt.choices.length > 0
                ? root.hintLine(["back:BACK", "navigate:NAVIGATE", "select:SELECT"])
                : root.hints.back + ":BACK"
    }
}
