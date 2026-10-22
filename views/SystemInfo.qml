import QtQuick
import Components

// SYSTEM INFORMATION, from About: what this device is and how the network
// knows it (systemInfo, src/system/SystemInfo.h), its name first, then its
// model, system, processor, memory, storage, temperature and time up, and
// each network connection's IP and MAC addresses, read again every few
// seconds while the page is up. On the OSD/OS image select on the name
// renames the device, typed on the keyboard: the help line says how it went.
FocusScope {
    id: infoRoot

    signal goBack()

    property var navParams: ({})

    // Context properties read null while the Loader tears this view down.
    readonly property var rows: systemInfo ? systemInfo.rows : []
    readonly property bool canRename: !!systemInfo && systemInfo.renameAvailable
    readonly property bool renaming: !!systemInfo && systemInfo.renaming
    readonly property string message: systemInfo ? systemInfo.message : ""
    // The lines the list shows, put in only as lines come or go (a
    // connection's): read again, a line's value and help change in place
    // (latest, by key), so that a value scrolling through goes on scrolling.
    property var shownRows: []
    property var latest: ({})
    readonly property var currentRow: {
        var row = shownRows[list.currentIndex]
        return row && row.key !== undefined && latest[row.key] ? latest[row.key] : row
    }
    readonly property bool onName: !!currentRow && currentRow.key === "name"

    Component.onCompleted: {
        takeRows(rows)
        if (systemInfo) {
            systemInfo.clearMessage()
            systemInfo.refresh()
        }
    }
    onRowsChanged: takeRows(rows)

    function takeRows(next) {
        var byKey = {}
        for (var i = 0; i < next.length; i++) {
            if (next[i].key !== undefined)
                byKey[next[i].key] = next[i]
        }
        latest = byKey
        var same = next.length === shownRows.length
        for (i = 0; i < next.length && same; i++)
            same = next[i].key === shownRows[i].key && next[i].label === shownRows[i].label
        if (same)
            return
        // New lines take the list's cursor back to the first: it goes back
        // to its line, by its key, else where it was, never on the heading.
        var was = list.currentIndex
        var key = shownRows[was] ? shownRows[was].key : undefined
        shownRows = next
        var at = -1
        for (i = 0; i < next.length && at < 0; i++) {
            if (key !== undefined && next[i].key === key)
                at = i
        }
        if (at < 0)
            at = Math.min(Math.max(was, 0), next.length - 1)
        if (at >= 0 && next[at].type === "section")
            at = Math.min(at + 1, next.length - 1)
        list.currentIndex = at
        if (at >= 0)
            list.show(at)
    }

    Timer {
        interval: 3000
        repeat: true
        running: true
        onTriggered: if (systemInfo) systemInfo.refresh()
    }

    AppBar {
        iconSource: "../../assets/images/logo.svg"
        title: "System Information"
        subtitle: systemInfo ? systemInfo.deviceName : ""
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    MenuList {
        id: list
        model: infoRoot.shownRows
        visible: !osk.visible
        focus: !osk.visible
        // Its help line stays, Help Line or not (always): a line more only
        // with the hint bar off.
        height: root.sh * 0.4666667 + root.hintRoom //224

        delegate: MenuRow {
            width: list.width
            height: root.sh * 0.0583333 //28
            heading: modelData.type === "section"
            label: modelData.label
            value: modelData.key === "name" && infoRoot.renaming ? "Renaming"
                   : ((infoRoot.latest[modelData.key] || modelData).value || "")
            selected: list.currentIndex === index
            // Read rather than chosen: a value too long for its line (an
            // address, a model's name) scrolls through, selected or not.
            alwaysScroll: true
        }

        Keys.onReturnPressed: {
            if (infoRoot.onName && infoRoot.canRename && !infoRoot.renaming)
                osk.open(infoRoot.currentRow.value)
        }
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                infoRoot.goBack()
                event.accepted = true
            }
        }
    }

    // What the line is; on the name, how its rename went, once it has.
    HelpLine {
        always: true
        visible: !osk.visible
        text: infoRoot.onName && infoRoot.message !== "" ? infoRoot.message
              : !infoRoot.currentRow ? ""
              : (infoRoot.currentRow.description || "")
                + (infoRoot.onName && infoRoot.canRename ? " • " + root.hints.select + " Rename it" : "")
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.helpLineMargin
        anchors.leftMargin: root.sw * 0.125 //80
    }

    HintBar {
        visible: !osk.visible
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE"
              + (infoRoot.onName && infoRoot.canRename ? " " + root.hints.select + ":RENAME" : "")
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // The new name: letters, digits and hyphens, a space typed as a hyphen.
    OnScreenKeyboard {
        id: osk
        anchors.fill: parent
        title: "Device Name"
        maxLength: 63
        onAccepted: function(text) {
            list.forceActiveFocus()
            if (systemInfo)
                systemInfo.rename(text)
        }
        onCanceled: list.forceActiveFocus()
    }
}
