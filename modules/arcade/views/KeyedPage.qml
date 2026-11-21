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
//     KeyedPage {
//         subtitle: "Check ROMs"
//         keys: ["last", "check"]
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
        visible: !page.covered
        focus: !page.covered

        delegate: MenuRow {
            width: list.width
            height: root.menuRowHeight
            label: page.labelOf(modelData)
            value: page.valueOf(modelData)
            selected: list.currentIndex === index
            keepValue: page.keepValue
        }

        Keys.onReturnPressed: page.choose(page.currentKey)
        Keys.onPressed: function(event) {
            if (root.isBack(event)) {
                page.goBack()
                event.accepted = true
            }
        }
    }

    HelpLine {
        always: true
        visible: !page.covered
        text: page.descriptionOf(page.currentKey)
    }

    HintBar {
        readonly property string word: page.selectWord(page.currentKey)
        visible: !page.covered
        text: root.hintLine(["back:BACK", "navigate:NAVIGATE", word !== "" ? "select:" + word : ""])
    }
}
