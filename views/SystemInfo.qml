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
    // In the layout's frame (Main.qml): its rows in the pane, beside the
    // column a layout keeps on the screen.
    readonly property bool inFrame: true

    signal goBack()

    property var navParams: ({})

    // Context properties read null while the Loader tears this view down.
    readonly property var rows: systemInfo ? systemInfo.rows : []
    readonly property bool canRename: !!systemInfo && systemInfo.renameAvailable
    readonly property bool renaming: !!systemInfo && systemInfo.renaming
    readonly property string message: systemInfo ? systemInfo.message : ""
    // The list is given the lines only as they come or go (a connection's,
    // MenuList's take()): read again, a line's value and help change in
    // place (latest, by key), so that a value scrolling through goes on
    // scrolling.
    property var latest: ({})
    readonly property var currentRow: {
        var row = (list.model || [])[list.currentIndex]
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
        list.take(next, function(row) { return row.key + ":" + row.label })
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
        // The device's name is no step of the way here.
        path: ["System Information"]
    }

    MenuList {
        id: list
        visible: !osk.visible
        focus: !osk.visible
        // Its help line stays, Help Line or not (always): a line more only
        // with the hint bar off (Main.qml's helpedListHeight).
        height: root.helpedListHeight //224

        delegate: MenuRow {
            width: list.width
            height: root.menuRowHeight
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
            if (root.isBack(event)) {
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
    }

    HintBar {
        visible: !osk.visible
        text: root.hintLine(["back:BACK", "navigate:NAVIGATE",
                             infoRoot.onName && infoRoot.canRename ? "select:RENAME" : ""])
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
