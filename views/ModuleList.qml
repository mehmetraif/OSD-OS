import QtQuick
import Components

FocusScope { 
    id: appRoot

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var navParams: ({})
    property var navListState: ({})

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
    // Coming and going while the menu is up (STOP, the video ending) moves
    // the rows below it, so the cursor stays on its row rather than its index.
    onBehindChanged: if (modules.length > 0) showRows(cursorState())

    // The row the cursor is on, as listState keeps it: its index, and which
    // row it is (a backend's rows share their module's entry point).
    function cursorState() {
        var row = menuList.model[menuList.currentIndex]
        return { currentIndex: menuList.currentIndex, entry: row ? row.entry_point : "",
                 name: row ? row.name : "", behind: !!(row && row.behind) }
    }

    // Lays the rows out with the cursor on keep (a cursorState()): the same
    // row, else one of its module's, else the same index; on the first row
    // without.
    function showRows(keep) {
        var rows = modules.slice()
        var note = appRoot.behind
        var entry = note && appCore ? appCore.moduleEntryPoint(note.module) : ""
        if (entry)
            rows.unshift({ name: "\u25BA " + (note.title || ""), entry_point: entry,
                           params: { resumePlayer: note.params || {} }, behind: true })
        menuList.model = rows
        if (rows.length === 0)
            return
        var index = -1
        if (keep && keep.entry) {
            for (var pass = 0; pass < 2 && index < 0; ++pass) {
                for (var i = 0; i < rows.length; ++i) {
                    if (rows[i].entry_point === keep.entry
                            && (pass === 1 || (rows[i].name === keep.name
                                               && !!rows[i].behind === !!keep.behind))) {
                        index = i
                        break
                    }
                }
            }
        }
        if (index < 0)
            index = keep && keep.currentIndex !== undefined ? Math.min(keep.currentIndex, rows.length - 1) : 0
        menuList.currentIndex = index
        menuList.positionViewAtIndex(index, ListView.Contain)
    }

    // Header
    AppBar {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // Empty state
    Column {
        anchors.centerIn: parent
        spacing: root.sh * 0.0333333 //16
        visible: menuList.count === 0
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

    ListView {
        id: menuList;
        model: [];
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25 //120
        anchors.leftMargin: root.sw * 0.115625 //74
        width: root.sw * 0.76875 //492
        height: root.sh * 0.525 + root.hintRoom //252
        clip: true;
        focus: true;

        delegate: Item {
            width: menuList.width;
            height: root.sh * 0.0583333 //28

            Item {
                id: textClipContainer;
                width: Math.min(rowText.implicitWidth, menuList.width);
                height: parent.height;
                clip: true;

                // The selected row's box, or the skin's picture of a selected
                // line (Settings → Skin).
                SelectionBox {
                    anchors.fill: rowText;
                    visible: menuList.currentIndex === index;
                }

                Text {
                    id: rowText;
                    text: modelData.name;
                    color: menuList.currentIndex === index ? root.surfaceColor : root.primaryColor;
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
                    running: (menuList.currentIndex === index) && (rowText.implicitWidth > textClipContainer.width);
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
            if (count === 0) return
            if (currentIndex > 0) currentIndex--
            else                  currentIndex = count - 1
            menuList.positionViewAtIndex(menuList.currentIndex, ListView.Contain)
        }
        Keys.onDownPressed: {
            if (count === 0) return
            if (currentIndex < count - 1) currentIndex++
            else                          currentIndex = 0
            menuList.positionViewAtIndex(menuList.currentIndex, ListView.Contain)
        }


        Keys.onReturnPressed: {
            // A row is {name, entry_point, params}. Module rows carry no params;
            // rows contributed by a backend (see AppCore::menuEntriesForModule) use
            // them to tell the module's router what was picked, and so does the
            // row for the video behind the menus (resumePlayer). This view stays
            // ignorant of what any of them mean.
            var row = menuList.model[menuList.currentIndex]
            if (!row) return
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

    // ▲ / ▼ while lines are hidden above or below.
    ScrollMarks {
        anchors.fill: menuList
        list: menuList
    }

    // --- FOOTER ---
    HintBar {
        id: footer
        text: root.hints.back + ":SETTINGS " + root.hints.navigate + ":NAVIGATE "
              + (root.videoBehind ? root.hints.play_pause + ":STOP " : "") + root.hints.select + ":SELECT"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }
}