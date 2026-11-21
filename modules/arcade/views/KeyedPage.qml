import QtQuick
import Components

// A page of Settings → Arcade's (MAME, FinalBurn Neo, the RetroAchievements
// account, Game Pictures, Check ROMs, Favorites): the module's bar with the
// page's name (subtitle), its lines by key (keys) under it, each with its
// label and value (labelOf(), valueOf()), the help line on the line selected
// (descriptionOf()) and the hint bar, with what select does there
// (selectWord(): "DOWNLOAD", "" for nothing); select on a line is
// choose(key), back leaves the page. In a monitor layout's frame, as
// Settings' own pages are (Root.qml's inFrame). A page whose lines come and
// go hands them to setKeys(); one with something over it (covered: a
// keyboard) hides its list and its bars meanwhile.
//
// A line that can't be used now says why (offOf(): "" for one that can):
// it is dimmed, never hidden, its help line opens with the reason, and
// select does nothing on it. A line of a setting with choices
// (choicesOf(): [{ label, value }], the one it is on chosenOf()) shows the
// choice's name between ◄ and ►, which change it, pick(key, value) keeping
// it; in Broadcast and Flat Panel (root.twoStep) in two steps, as Settings
// changes a value there: select starts it, ▲ ▼ step it, select keeps it and
// back leaves it as it was. A line that destroys something asks first
// (ask()): the question in the standard window, Cancel under the cursor and
// then the action's own verb, which is confirmed(action).
//     KeyedPage {
//         subtitle: "Check ROMs"
//         keys: ["check", "last"]
//         function labelOf(key) { … }
//         function choose(key) { … }
//     }
FocusScope {
    id: page
    readonly property bool inFrame: true

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var navParams: ({})

    focus: true

    property string subtitle: ""
    property var keys: []
    readonly property string currentKey: keys[list.currentIndex] || ""
    property alias currentIndex: list.currentIndex
    // Whether each line keeps its value whole, its label cut short first.
    property bool keepValue: true
    property bool covered: false

    function labelOf(key) { return "" }
    function valueOf(key) { return "" }
    function descriptionOf(key) { return "" }
    function selectWord(key) { return "" }
    function choose(key) {}
    function offOf(key) { return "" }
    function choicesOf(key) { return [] }
    function chosenOf(key) { return 0 }
    function pick(key, value) {}
    function confirmed(action) {}

    // Whether ◄ ► change the line's value.
    function changes(key) {
        return offOf(key) === "" && choicesOf(key).length > 0
    }
    // The name of the choice steps away from the one the line is on.
    function choiceLabel(key, steps) {
        var choices = choicesOf(key)
        if (choices.length === 0)
            return ""
        var at = Math.max(0, chosenOf(key))
        return choices[((at + steps) % choices.length + choices.length) % choices.length].label
    }
    // The line's value a step on (1) or back (-1), or as many as it was
    // moved in two steps.
    function stepValue(key, steps) {
        var choices = choicesOf(key)
        if (!changes(key) || steps % choices.length === 0)
            return
        var at = Math.max(0, chosenOf(key))
        pick(key, choices[((at + steps) % choices.length + choices.length) % choices.length].value)
    }
    // The value being changed in two steps (Broadcast, Flat Panel): its
    // line, -1 for none, and how far it was moved.
    property int editIndex: -1
    property int editSteps: 0
    function shownValue(key, index) {
        if (choicesOf(key).length === 0)
            return valueOf(key)
        return choiceLabel(key, editIndex === index ? editSteps : 0)
    }

    // A question before an action that destroys something: what it asks,
    // what it says of it under that, and the action's verb, the answer
    // under Cancel; confirmed(action) on the verb.
    property string askedAction: ""
    function ask(question, about, verb, action) {
        askedAction = action
        prompt.promptText = question
        prompt.subtitleText = about || ""
        prompt.choices = [{ label: "Cancel", action: "cancel" }, { label: verb, action: action }]
        prompt.open()
    }

    // The page's lines made again only when a line comes or goes, so the
    // cursor stays on its line (as a download goes on), or the first where
    // its line has gone.
    function setKeys(made) {
        if (JSON.stringify(made) === JSON.stringify(keys))
            return
        var at = keys[list.currentIndex]
        keys = made
        list.currentIndex = Math.max(0, made.indexOf(at))
    }
    // The keys back to the list, from what was over it.
    function focusList() {
        list.forceActiveFocus()
    }

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: page.subtitle
    }

    MenuList {
        id: list
        model: page.keys
        visible: !page.covered && !prompt.visible
        focus: !page.covered

        delegate: MenuRow {
            width: list.width
            height: root.menuRowHeight
            label: page.labelOf(modelData)
            value: page.shownValue(modelData, index)
            selected: list.currentIndex === index
            keepValue: page.keepValue
            changes: page.changes(modelData)
            editing: page.editIndex === index
            dimmed: page.offOf(modelData) !== ""
        }

        held: page.editIndex >= 0
        onHeldStep: function(delta) { page.editSteps -= delta }
        onCurrentIndexChanged: page.editIndex = -1

        Keys.onReturnPressed: {
            var key = page.currentKey
            if (page.editIndex >= 0) {
                var steps = page.editSteps
                page.editIndex = -1
                page.stepValue(key, steps)
            } else if (page.offOf(key) !== "") {
                return
            } else if (root.twoStep && page.changes(key)) {
                page.editSteps = 0
                page.editIndex = list.currentIndex
            } else {
                page.choose(key)
            }
        }
        Keys.onLeftPressed: {
            if (root.twoStep) {
                // Flat Panel: ◄ is back, and leaves a value being changed.
                if (root.flatLayout) {
                    if (page.editIndex >= 0)
                        page.editIndex = -1
                    else
                        page.goBack()
                }
                return
            }
            page.stepValue(page.currentKey, -1)
        }
        Keys.onRightPressed: {
            if (!root.twoStep)
                page.stepValue(page.currentKey, 1)
        }
        Keys.onPressed: function(event) {
            if (root.isBack(event)) {
                if (page.editIndex >= 0)
                    page.editIndex = -1
                else
                    page.goBack()
                event.accepted = true
            }
        }
    }

    HelpLine {
        readonly property string off: page.offOf(page.currentKey)
        always: true
        visible: !page.covered && !prompt.visible
        text: (off !== "" ? off + "\n" : "") + page.descriptionOf(page.currentKey)
    }

    HintBar {
        readonly property string word: page.offOf(page.currentKey) !== "" ? "" : page.selectWord(page.currentKey)
        readonly property bool changing: page.changes(page.currentKey)
        visible: !page.covered && !prompt.visible
        text: page.editIndex >= 0 ? root.hintLine(["back:CANCEL", "navigate:CHANGE", "select:CONFIRM"])
              : root.hintLine(["back:BACK", "navigate:NAVIGATE",
                               changing ? (root.twoStep ? "select:CHANGE" : "change:CHANGE") : "",
                               word !== "" ? "select:" + word : ""])
    }

    // The question ask() asks; back, or Cancel, does nothing.
    ChoiceOverlay {
        id: prompt
        objectName: "question"
        anchors.fill: parent
        onActivated: function(action) {
            if (action !== "cancel")
                page.confirmed(action)
        }
        onClosed: page.focusList()
    }
}
