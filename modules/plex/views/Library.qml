import QtQuick
import Components

// Sub-menu for a single library: Recommended, Library, Collections, Playlists, Categories, Folders
FocusScope {
    id: subMenuRoot
    // In the layout's frame (Main.qml): its list in the pane, beside the
    // column a layout keeps on the screen.
    readonly property bool inFrame: true

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property string libraryName: navParams.libraryName || ""
    property string sectionId: navParams.sectionId || ""
    property string sectionType: navParams.sectionType || ""

    property var menuItems: []

    Connections {
        target: plexBackend

        function onCapabilitiesLoaded(caps) {
            var items = []
            if (caps.recommended) items.push({ label: "RECOMMENDED", action: "hubs" })
            items.push({ label: "LIBRARY", action: "library_all" })
            items.push({ label: "COLLECTIONS", action: "collections" })
            items.push({ label: "PLAYLISTS", action: "playlists" })
            items.push({ label: "CATEGORIES", action: "categories" })
            items.push({ label: "FOLDERS", action: "folders" })
            subMenuRoot.menuItems = items
            if (items.length > 0) {
                var restore = (navListState.currentIndex !== undefined) ? navListState.currentIndex : 0
                menuList.currentIndex = Math.min(restore, items.length - 1)
                menuList.positionViewAtIndex(menuList.currentIndex, ListView.Contain)
            }
        }
    }

    Component.onCompleted: {
        plexBackend.check_section_capabilities(sectionId)
    }

    focus: true
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
        }
    }

    // ---
    // UI
    // ---

    // Header
    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: libraryName
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // Loading Indicator
    Text {
        visible: menuItems.length === 0
        text: "LOADING..."
        color: root.tertiaryColor
        font.family: root.globalFont
        // In the middle of the pane.
        x: root.paneX
        anchors.verticalCenter: parent.verticalCenter
        width: root.paneWidth
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: root.sh * 0.05 //24
    }

    // Body
    ListView {
        id: menuList
        model: menuItems
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25 //120
        anchors.leftMargin: root.paneX
        width: root.paneWidth
        height: root.sh * 0.525 + root.hintRoom //252
        clip: true
        focus: true

        Keys.onUpPressed: {
            if (count === 0) return
            if (currentIndex > 0) currentIndex--
            else currentIndex = count - 1
            menuList.positionViewAtIndex(menuList.currentIndex, ListView.Contain)
        }
        Keys.onDownPressed: {
            if (count === 0) return
            if (currentIndex < count - 1) currentIndex++
            else currentIndex = 0
            menuList.positionViewAtIndex(menuList.currentIndex, ListView.Contain)
        }

        Keys.onReturnPressed: {
            var item = menuItems[currentIndex]
            if (!item) return

            var params = {
                listType: item.action,
                title: item.label,
                sectionId: sectionId,
                libraryName: libraryName
            }

            subMenuRoot.navigateTo("Items.qml", params, { currentIndex: menuList.currentIndex })
        }

        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace) {
                subMenuRoot.goBack()
                event.accepted = true
            }
        }

        delegate: Item {
            width: menuList.width
            height: root.sh * 0.0583333 //28

            Item {
                id: textClip
                width: Math.min(rowText.implicitWidth, menuList.width)
                height: parent.height
                clip: true

                Rectangle {
                    color: root.accentColor
                    anchors.fill: rowText
                    visible: menuList.currentIndex === index
                }

                Text {
                    id: rowText
                    text: modelData.label || ""
                    color: menuList.currentIndex === index ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    anchors.verticalCenter: parent.verticalCenter
                    x: 0
                    topPadding: root.sh * 0.0041667 //2
                    leftPadding: root.sw * 0.009375 //6
                    rightPadding: root.sw * 0.009375 //6
                    bottomPadding: root.sh * 0.00625 //3
                    font.pixelSize: root.sh * 0.05 //24
                }
            }
        }
    }

    // Footer
    HintBar {
        id: footer
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.select + ":SELECT"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }
}
