import QtQuick
import Components

FocusScope { 
    id: appRoot

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    Component.onCompleted: {
        appCore.scan_for_modules()
    }

    Connections {
        target: appCore;
        function onModulesLoaded(moduleData) {
            appRoot.modules = moduleData
            // With a video to go back to, the cursor starts on its row: the
            // quickest way out of the menus is then select.
            appRoot.showRows(appRoot.behind ? null : navListState)
        }
    }

    // The enabled modules (AppCore), led by a row for the video playing on
    // behind the menus (Transparent Background), or left with its menu's
    // Browse while mpv had the screen, when its player noted how to take it
    // back (root.takeBackNote): select opens that player again, which brings
    // the video back to full screen where it is, or starts it where it was.
    property var modules: []
    readonly property var behind: root.takeBackNote && root.takeBackNote.module ? root.takeBackNote : null

    // Settings → Look → Menu Layout's Camcorder (Main.qml's menuLayout): the
    // rows in groups by their module's settings_group (AppCore's "group"),
    // each group an icon in a column at the left (Components/IconColumn):
    // TV / Movie, Arcade, More (the modules of neither), then Settings; the
    // rows beside it the group's. ◄ goes to the icons (groupIcons), ▲ ▼ there
    // from group to group, ► or select into the group's rows; on Settings'
    // they open Settings. Studio Monitor does the same in a monitor's window
    // (Components/MonitorChrome), the icons in its strip. Broadcast and Flat
    // Panel are lists as Standard is, in a monitor's look: Broadcast's ends
    // in a row for Settings after a gap, Flat Panel's goes a page at a time.
    readonly property string layout: root.menuLayout
    readonly property bool camcorder: layout === "Camcorder"
    readonly property bool studio: layout === "Studio Monitor"
    readonly property bool broadcast: layout === "Broadcast"
    readonly property bool flat: layout === "Flat Panel"
    readonly property bool monitor: studio || broadcast || flat
    readonly property bool iconMenu: camcorder || studio
    property var groups: []
    property int groupIndex: 0
    property bool groupIcons: false
    readonly property var groupLooks: [{ key: "tv", icon: "tv", label: "TV / Movie" },
                                       { key: "arcade", icon: "joystick", label: "Arcade" },
                                       { key: "", icon: "apps", label: "More" }]
    // Menu Layout changed while the menu is open, once its rows are laid out
    // (not as the menu is made, when the binding first takes its value).
    // A tick later, so every binding on the layout has its new value.
    property bool laidOut: false
    onLayoutChanged: if (laidOut) Qt.callLater(function() { appRoot.showRows(appRoot.cursorState()) })

    // The rows in Camcorder's groups, each group's in the order the rows
    // come, the groups with none left out; Settings' last, always.
    function groupsOf(rows) {
        var made = []
        for (var i = 0; i < groupLooks.length; i++) {
            var look = groupLooks[i]
            var mine = rows.filter(function(row) {
                var g = row.group === "tv" || row.group === "arcade" ? row.group : ""
                return g === look.key
            })
            if (mine.length > 0)
                made.push({ key: look.key, icon: look.icon, label: look.label, rows: mine })
        }
        made.push({ key: "settings", icon: "gear", label: "Settings",
                    rows: [{ name: "Settings", entry_point: "views/Settings.qml" }] })
        return made
    }
    // Camcorder's icons: the group above (-1) or below (1), round, its rows
    // beside the column at once.
    function moveGroup(direction) {
        var n = groups.length
        if (n === 0)
            return
        groupIndex = (groupIndex + direction + n) % n
        menuList.model = groups[groupIndex].rows
        menuList.currentIndex = 0
        showCurrent()
    }
    // Where the cursor's row shows: in Flat Panel its page.
    function showCurrent() {
        if (flat)
            chrome.pageTo(menuList)
        else
            menuList.positionViewAtIndex(menuList.currentIndex, ListView.Contain)
    }
    // ► or select on an icon: into the group's rows; Settings' opens it.
    function enterGroup() {
        var group = groups[groupIndex]
        if (!group)
            return
        if (group.key === "settings")
            appRoot.navigateTo("views/Settings.qml", {}, appRoot.cursorState())
        else
            groupIcons = false
    }
    // Coming and going while the menu is up (STOP, the video ending) moves
    // the rows below it, so the cursor stays on its row rather than its index.
    onBehindChanged: if (modules.length > 0) showRows(cursorState())

    // The row the cursor is on, as listState keeps it: its index, and which
    // row it is (a backend's rows share their module's entry point).
    // In Camcorder, its group's and whether the keys were in the icons.
    function cursorState() {
        var row = menuList.model[menuList.currentIndex]
        var group = iconMenu ? groups[groupIndex] : null
        return { currentIndex: menuList.currentIndex, entry: row ? row.entry_point : "",
                 name: row ? row.name : "", behind: !!(row && row.behind),
                 group: group ? group.key : undefined, groupIcons: !!group && groupIcons }
    }

    // The row of `rows` keep (a cursorState()) was on: the same row, else
    // one of its module's; -1 for none.
    function keptRow(rows, keep) {
        if (!keep || !keep.entry)
            return -1
        for (var pass = 0; pass < 2; ++pass) {
            for (var i = 0; i < rows.length; ++i) {
                if (rows[i].entry_point === keep.entry
                        && (pass === 1 || (rows[i].name === keep.name && !!rows[i].behind === !!keep.behind)))
                    return i
            }
        }
        return -1
    }

    // Lays the rows out with the cursor on keep (a cursorState()): the same
    // row, else one of its module's, else the same index; on the first row
    // without. In Camcorder, in the kept row's group, else the group kept.
    function showRows(keep) {
        var rows = modules.slice()
        var note = appRoot.behind
        var entry = note && appCore ? appCore.moduleEntryPoint(note.module) : ""
        if (entry) {
            var owner = modules.filter(function(m) { return m.entry_point === entry })[0]
            rows.unshift({ name: "\u25BA " + (note.title || ""), entry_point: entry,
                           params: { resumePlayer: note.params || {} }, behind: true,
                           group: owner ? owner.group : "tv" })
        }
        var index = keptRow(rows, keep)
        if (iconMenu) {
            groups = groupsOf(rows)
            var at = -1
            var found = index >= 0 ? rows[index] : null
            for (var g = 0; g < groups.length && found && at < 0; g++) {
                at = groups[g].rows.indexOf(found)
                if (at >= 0)
                    groupIndex = g
            }
            if (at < 0) {
                var kept = keep && keep.group !== undefined
                           ? groups.findIndex(function(group) { return group.key === keep.group }) : -1
                groupIndex = Math.max(0, kept)
                at = kept >= 0 && keep.currentIndex !== undefined ? keep.currentIndex : 0
            }
            groupIcons = !!(keep && keep.groupIcons)
            rows = groups[groupIndex].rows
            index = Math.min(at, rows.length - 1)
        } else {
            groups = []
            groupIcons = false
            // Broadcast's Settings, after a gap the cursor passes over.
            if (broadcast) {
                rows.push({ name: "", gap: true })
                rows.push({ name: "Settings", entry_point: "views/Settings.qml" })
                index = keptRow(rows, keep)
            }
        }
        laidOut = true
        menuList.model = rows
        if (rows.length === 0)
            return
        if (index < 0)
            index = keep && keep.currentIndex !== undefined ? Math.min(keep.currentIndex, rows.length - 1) : 0
        menuList.currentIndex = index
        showCurrent()
    }

    // Header: in Camcorder, the group's name.
    AppBar {
        visible: !appRoot.monitor
        subtitle: appRoot.camcorder && appRoot.groups[appRoot.groupIndex] ? appRoot.groups[appRoot.groupIndex].label : ""
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // Empty state
    Column {
        anchors.centerIn: parent
        spacing: root.sh * 0.0333333 //16
        visible: appRoot.modules.length === 0 && !appRoot.behind
        Text {
            text: "No modules enabled"
            color: root.secondaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.05 //24
        }
        Text {
            text: "Please enable one in settings"
            color: root.tertiaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.0333333 //16
        }
    }

    // A monitor layout's window or ground, title, the groups' icons in
    // Studio Monitor's strip, and the key guide, under the rows. No help
    // line: the main menu has none.
    MonitorChrome {
        id: chrome
        anchors.fill: parent
        visible: appRoot.monitor
        hasHelp: false
        title: appRoot.studio ? (appRoot.groups[appRoot.groupIndex] || { label: "OSD/OS" }).label : "OSD/OS"
        icons: appRoot.studio ? appRoot.groups.map(function(group) { return group.icon }) : []
        iconIndex: appRoot.groupIndex
        iconsActive: appRoot.groupIcons
        guide: {
            var h = root.hints
            if (appRoot.studio)
                return [h.navigate + ":MENU/ITEM", appRoot.groupIcons ? h.browse + ":ENTER" : h.left + ":GROUPS",
                        h.back + ":SETTINGS"].concat(root.videoBehind ? [h.play_pause + ":STOP"] : [])
            // Broadcast's three lines, as a monitor's guide has them.
            if (appRoot.broadcast)
                return ["TO SELECT  :" + h.navigate, "TO ENTER   :" + h.select, "TO SETTINGS:" + h.back]
            return [h.navigate + " Select", h.select + " Open", h.back + " Settings"]
                   .concat(root.videoBehind ? [h.play_pause + " Stop"] : [])
        }
    }

    ListView {
        id: menuList;
        model: [];
        anchors.top: parent.top
        anchors.left: parent.left
        // Where a monitor layout has its rows (MonitorChrome), beside
        // Camcorder's icons, or where every view has its list.
        anchors.topMargin: appRoot.monitor ? chrome.listY : root.sh * 0.25 //120
        anchors.leftMargin: appRoot.monitor ? chrome.listX : appRoot.camcorder ? root.sw * 0.209375 : root.sw * 0.115625 //134 : 74
        width: appRoot.monitor ? chrome.listWidth : appRoot.camcorder ? root.sw * 0.675 : root.sw * 0.76875 //432 : 492
        height: appRoot.monitor ? chrome.listHeight : root.sh * 0.525 + root.hintRoom //252
        onHeightChanged: if (appRoot.flat) Qt.callLater(appRoot.showCurrent)
        // Flat Panel's last page filled out to a whole page, and the view
        // left where its page puts it (not moved to follow the cursor).
        footer: Item { width: 1; height: chrome.pageFill(menuList.count) }
        highlightFollowsCurrentItem: !appRoot.flat
        clip: true;
        focus: true;

        delegate: Item {
            // In Camcorder's icons the box is on the icon.
            readonly property bool current: menuList.currentIndex === index
                                            && !(appRoot.iconMenu && appRoot.groupIcons)
            width: menuList.width;
            height: root.sh * 0.0583333 //28

            // The row in a monitor layout: Broadcast's "...", Flat Panel's ▶.
            Loader {
                active: appRoot.monitor
                anchors.fill: parent
                sourceComponent: MonitorRow {
                    label: modelData.name || ""
                    opens: !modelData.gap && !appRoot.studio
                    selected: current
                }
            }

            Item {
                id: textClipContainer;
                visible: !appRoot.monitor
                width: Math.min(rowText.implicitWidth, menuList.width);
                height: parent.height;
                clip: true;

                // The selected row's box, or the skin's picture of a selected
                // line (Settings → Look → Skin).
                SelectionBox {
                    anchors.fill: rowText;
                    visible: current;
                }

                Text {
                    id: rowText;
                    text: modelData.name;
                    color: current ? root.surfaceColor : root.primaryColor;
                    font.family: root.globalFont;
                    font.capitalization: Font.AllUppercase;
                    anchors.verticalCenter: parent.verticalCenter
                    x: 0
                    topPadding: root.sh * 0.0041667 //2
                    leftPadding: root.sw * 0.009375 //6
                    rightPadding: root.sw * 0.009375 //6
                    bottomPadding: root.sh * 0.00625 //3
                    font.pixelSize: root.sh * 0.05 //24
                }

                SequentialAnimation {
                    id: marqueeAnim;
                    running: current && (rowText.implicitWidth > textClipContainer.width);
                    loops: Animation.Infinite;

                    onRunningChanged: {
                        if (!running) rowText.x = 0;
                    }

                    PauseAnimation { 
                        duration: 1500;
                    }
                    
                    NumberAnimation {
                        target: rowText;
                        property: "x";
                        to: textClipContainer.width - rowText.implicitWidth;
                        duration: Math.abs(to) * 20;
                    }

                    PauseAnimation { 
                        duration: 2000;
                    }

                    PropertyAction { 
                        target: rowText; 
                        property: "x"; 
                        value: 0;
                    }
                }
            }
        }

        Keys.onUpPressed: {
            if (appRoot.iconMenu && appRoot.groupIcons) {
                appRoot.moveGroup(-1)
                return
            }
            step(-1)
        }
        // A row up (-1) or down (1), round, over Broadcast's gap.
        function step(direction) {
            if (count === 0) return
            do {
                currentIndex = (currentIndex + direction + count) % count
            } while (model[currentIndex] && model[currentIndex].gap)
            appRoot.showCurrent()
        }
        Keys.onDownPressed: {
            if (appRoot.iconMenu && appRoot.groupIcons) {
                appRoot.moveGroup(1)
                return
            }
            step(1)
        }


        // Camcorder: ◄ from the rows to the icons, ► from the icons into the
        // rows.
        Keys.onLeftPressed: function(event) {
            if (appRoot.iconMenu)
                appRoot.groupIcons = true
            else
                event.accepted = false
        }
        Keys.onRightPressed: function(event) {
            if (appRoot.iconMenu && appRoot.groupIcons)
                appRoot.enterGroup()
            else
                event.accepted = false
        }

        // The mouse over a row (Hover.js): in Camcorder's icons, the keys go
        // into the rows.
        function hoverRow(index, list) {
            if (model[index] && model[index].gap)
                return
            appRoot.groupIcons = false
            currentIndex = index
        }

        Keys.onReturnPressed: {
            if (appRoot.iconMenu && appRoot.groupIcons) {
                appRoot.enterGroup()
                return
            }
            // A row is {name, entry_point, params}. Module rows carry no params;
            // rows contributed by a backend (see AppCore::menuEntriesForModule) use
            // them to tell the module's router what was picked, and so does the
            // row for the video behind the menus (resumePlayer). This view stays
            // ignorant of what any of them mean.
            var row = menuList.model[menuList.currentIndex]
            if (!row || row.gap) return
            console.log("Routing to: " + row.entry_point)
            appRoot.navigateTo(row.entry_point, row.params || {}, appRoot.cursorState())
        }

        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                appRoot.navigateTo("views/Settings.qml", {}, appRoot.cursorState())
                event.accepted = true
            } else if (event.key === Qt.Key_Space && root.videoBehind) {
                // The play/pause key stops a video left playing behind the
                // menus (Transparent Background).
                mpvController.stopBackground()
                event.accepted = true
            }
        }
    }

    // ▲ / ▼ while lines are hidden above or below; Flat Panel's page says
    // so instead.
    ScrollMarks {
        visible: !appRoot.flat
        anchors.fill: menuList
        list: menuList
        color: root.menuPalette ? root.menuPalette.ink : root.primaryColor
    }

    // Camcorder's icons, the groups, at the left of the rows.
    IconColumn {
        visible: appRoot.camcorder
        model: appRoot.groups
        currentIndex: appRoot.groupIndex
        active: appRoot.groupIcons
        x: root.sw * 0.115625 //74
        y: menuList.y
        width: root.sw * 0.084375 //54
        height: menuList.height
    }

    // --- FOOTER ---
    HintBar {
        id: footer
        visible: !appRoot.monitor
        text: root.hints.back + ":SETTINGS " + root.hints.navigate + ":NAVIGATE "
              + (appRoot.camcorder ? (appRoot.groupIcons ? root.hints.browse + ":OPEN " : root.hints.left + ":GROUPS ") : "")
              + (root.videoBehind ? root.hints.play_pause + ":STOP " : "") + root.hints.select + ":SELECT"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }
}