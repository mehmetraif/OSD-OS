import QtQuick

// A media server's home screen (Jellyfin's, Emby's): Continue Watching + library
// list. The MediaServer* views are the two modules' own, shared: each takes the
// module's backend, and names the server by the module's name. A module's
// Libraries.qml is just this, with its backend:
//     MediaServerLibraries { backend: jellyfinBackend }
FocusScope {
    id: browseRoot
    // In the layout's frame (Main.qml): its list in the pane, beside the
    // column a layout keeps on the screen.
    readonly property bool inFrame: true

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    // The module's backend (an EmbyApiBackend: jellyfinBackend, embyBackend),
    // which its view file names.
    property var backend: null

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var libraries: []
    property string serverName: ""
    property string userName: ""
    // Why there is no list: the server couldn't be read.
    property string errorMessage: ""
    // The server has answered: none listed is none (a server of music or
    // books alone, or every library turned off), not LOADING.
    property bool loaded: false

    Connections {
        target: backend

        function onLibrariesLoaded(items) {
            // The backend prepends Continue Watching / Up Next (only when they
            // have content), so render the list as given.
            browseRoot.errorMessage = ""
            browseRoot.loaded = true
            browseRoot.libraries = items
            if (items.length > 0) {
                var restore = (navListState.currentIndex !== undefined) ? navListState.currentIndex : 0
                libraryList.currentIndex = Math.min(restore, items.length - 1)
                libraryList.positionViewAtIndex(libraryList.currentIndex, ListView.Contain)
            }
        }

        function onErrorOccurred(msg) {
            console.log("[" + moduleRoot.moduleName + " Library] Error: " + msg)
            if (browseRoot.libraries.length === 0)
                browseRoot.errorMessage = msg
        }
    }

    Component.onCompleted: {
        browseRoot.serverName = backend.get_server_name()
        browseRoot.userName = backend.get_user_name()
        backend.load_libraries()
    }

    focus: true

    // ---
    // UI
    // ---

    // Header
    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: browseRoot.serverName + (browseRoot.userName ? " (" + browseRoot.userName + ")" : "")
    }

    // Loading Indicator, or why there is no list.
    PaneMessage {
        visible: libraries.length === 0
        text: browseRoot.errorMessage !== "" ? browseRoot.errorMessage
              : browseRoot.loaded ? "NO LIBRARIES\nSettings → " + moduleRoot.moduleName + " → Libraries chooses which are shown"
              : "LOADING..."
    }

    // Body
    ListView {
        id: libraryList
        model: libraries
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.listY //120
        anchors.leftMargin: root.paneX
        width: root.paneWidth
        height: root.tallListHeight //252
        clip: true
        focus: true

        Keys.onUpPressed: {
            if (count === 0) return
            if (currentIndex > 0) currentIndex--
            else currentIndex = count - 1
            libraryList.positionViewAtIndex(libraryList.currentIndex, ListView.Contain)
        }
        Keys.onDownPressed: {
            if (count === 0) return
            if (currentIndex < count - 1) currentIndex++
            else currentIndex = 0
            libraryList.positionViewAtIndex(libraryList.currentIndex, ListView.Contain)
        }

        Keys.onReturnPressed: {
            var lib = libraries[currentIndex]
            if (!lib) return

            if (lib.key === "continue_watching") {
                browseRoot.navigateTo("Items.qml", {
                    mode: "resume",
                    libraryName: "CONTINUE WATCHING"
                }, { currentIndex: libraryList.currentIndex })
                return
            }

            if (lib.key === "up_next") {
                browseRoot.navigateTo("Items.qml", {
                    mode: "up_next",
                    libraryName: "NEXT UP"
                }, { currentIndex: libraryList.currentIndex })
                return
            }

            // Box-set libraries list their box sets (direct children); Items.qml
            // "boxset" mode fetches them and routes each box set into Boxset.qml.
            var collectionType = lib.collectionType || ""
            if (collectionType === "boxsets") {
                browseRoot.navigateTo("Items.qml", {
                    parentId: lib.itemId,
                    libraryName: lib.title,
                    mode: "boxset"
                }, { currentIndex: libraryList.currentIndex })
                return
            }

            // homevideos libraries are a tree of folders + videos, browsed one
            // level at a time via Items.qml "folder" mode.
            if (collectionType === "homevideos") {
                browseRoot.navigateTo("Items.qml", {
                    parentId: lib.itemId,
                    libraryName: lib.title,
                    mode: "folder"
                }, { currentIndex: libraryList.currentIndex })
                return
            }

            // Map the (gated) library type to its item-type filter.
            var includeTypes = ""
            if (collectionType === "movies") includeTypes = "Movie"
            else if (collectionType === "tvshows") includeTypes = "Series"
            browseRoot.navigateTo("Items.qml", {
                parentId: lib.itemId,
                libraryName: lib.title,
                includeTypes: includeTypes
            }, { currentIndex: libraryList.currentIndex })
        }

        Keys.onPressed: function(event) {
            if (root.isBack(event)) {
                browseRoot.goBack()
                event.accepted = true
            }
        }

        delegate: Item {
            width: libraryList.width
            height: root.menuRowHeight

            // The row as a monitor layout draws one, in its frame (Main.qml's):
            // the layout's cursor, its case, its colours.
            Loader {
                anchors.fill: parent
                active: root.monitorInk === true
                sourceComponent: MonitorRow {
                    label: textClip.text
                    selected: libraryList.currentIndex === index
                }
            }

            ScrolledText {
                id: textClip
                visible: root.monitorInk !== true
                width: Math.min(implicitWidth, libraryList.width)
                height: parent.height
                on: libraryList.currentIndex === index
                elides: false
                text: modelData.title || ""
                color: libraryList.currentIndex === index ? root.cursorInk : root.primaryColor
                font.pixelSize: root.sh * 0.05 //24
                textItem.topPadding: root.sh * 0.0041667 //2
                textItem.leftPadding: root.sw * 0.009375 //6
                textItem.rightPadding: root.sw * 0.009375 //6
                textItem.bottomPadding: root.sh * 0.00625 //3

                Rectangle {
                    z: -1
                    color: root.cursorColor
                    anchors.fill: textClip.textItem
                    visible: libraryList.currentIndex === index
                }
            }
        }
    }

    // Flat Panel's pages for the list (Settings → Look → Menu Layout): the
    // frame says which.
    ListPager { list: libraryList }

    // Footer
    HintBar {
        id: footer
        text: root.hintLine(["back:BACK", "navigate:NAVIGATE", "select:SELECT"])
    }
}
