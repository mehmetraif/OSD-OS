import QtQuick
import Components

FocusScope {
    id: itemsRoot
    // In the layout's frame (Main.qml): its list in the pane, beside the
    // column a layout keeps on the screen.
    readonly property bool inFrame: true

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    // None until the list has its model (Component.onCompleted).
    readonly property var currentScript: scriptList.model ? (scriptList.model[scriptList.currentIndex] || null) : null

    property var  pendingScript: null

    focus: true

    // Scripts marked "confirm = yes" get a yes/no prompt first; everything else
    // launches straight away.
    function activate(entry) {
        if (!entry) return
        if (entry.confirm) {
            pendingScript = entry
            confirmOverlay.open()
        } else {
            launch(entry)
        }
    }

    // The run mode decides the view: Console.qml keeps OSD/OS on screen and shows
    // the output; Takeover.qml hands the display to the script and reports back.
    function launch(entry) {
        var view = entry.mode === "takeover" ? "Takeover.qml" : "Console.qml"
        // ranScript marks the return trip, so coming back from the runner can be
        // told apart from arriving fresh — same idea as Ambient Mode's
        // returnedFromPlayer. Without it, a favorite routed here to be confirmed
        // would re-open its prompt every time the script finished.
        navigateTo(view,
                   { basename: entry.basename, name: entry.name },
                   { currentIndex: scriptList.currentIndex, ranScript: true })
    }

    // Rewrites the sidecar and rescans, so the row and the main menu both update.
    function toggleFavorite() {
        var entry = currentScript
        if (!entry) return
        var keep = scriptList.currentIndex
        if (scriptsBackend.setFavorite(entry.basename, !entry.favorite)) {
            var loaded = scriptsBackend.getScripts()
            scriptList.model = loaded
            scriptList.currentIndex = Math.min(keep, loaded.length - 1)
        }
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
        } else if (event.key === Qt.Key_Right) {
            // Right, not Space: a USB remote reliably has a d-pad but often no
            // space key. Matches the YouTube module's watch-later save/remove.
            toggleFavorite()
            event.accepted = true
        }
    }

    // ---
    // UI
    // ---

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // Empty state
    Column {
        anchors.centerIn: parent
        spacing: root.sh * 0.0333333 //16
        visible: scriptList.count === 0
        Text {
            text: "No scripts found"
            color: root.secondaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.05 //24
        }
        Text {
            text: "Please add .sh files to the scripts directory"
            color: root.tertiaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.0333333 //16
        }
    }

    ListView {
        id: scriptList
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.listY //120
        anchors.leftMargin: root.paneX //74
        width: root.paneWidth //492
        // A row more for each bar that is off (Main.qml's helpRoom, hintRoom).
        height: root.listHeight //224
        clip: true
        focus: true

        Keys.onReturnPressed: itemsRoot.activate(scriptList.model[scriptList.currentIndex])

        delegate: Item {
            width: scriptList.width
            height: root.sh * 0.0583333 //28

            // The mode tag is laid out first so the name's clip region can stop
            // short of it — a long name must never run underneath the tag.
            Text {
                id: modeTag
                visible: root.monitorInk !== true
                text: (modelData.favorite ? "* " : "")
                      + (modelData.mode === "takeover" ? "TAKEOVER" : "CONSOLE")
                color: scriptList.currentIndex === index ? root.accentColor : root.tertiaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: root.sw * 0.009375 //6
                font.pixelSize: root.sh * 0.0291667 //14
            }

            // The row as a monitor layout draws one, in its frame (Main.qml's):
            // the layout's cursor, its case, its colours.
            Loader {
                anchors.fill: parent
                active: root.monitorInk === true
                sourceComponent: MonitorRow {
                    label: rowText.text
                    value: modeTag.text
                    selected: scriptList.currentIndex === index
                }
            }

            Item {
                id: textClip
                visible: root.monitorInk !== true
                width: Math.min(rowText.implicitWidth,
                                scriptList.width - modeTag.width - root.sw * 0.028125)
                height: parent.height
                clip: true

                Rectangle {
                    color: root.accentColor
                    anchors.fill: rowText
                    visible: scriptList.currentIndex === index
                }

                Text {
                    id: rowText
                    text: modelData.name
                    color: scriptList.currentIndex === index ? root.surfaceColor : root.primaryColor
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

                SequentialAnimation {
                    running: (scriptList.currentIndex === index) &&
                             (rowText.implicitWidth > textClip.width)
                    loops: Animation.Infinite
                    onRunningChanged: if (!running) rowText.x = 0
                    PauseAnimation { duration: 1500 }
                    NumberAnimation {
                        target: rowText; property: "x"
                        to: textClip.width - rowText.implicitWidth
                        duration: Math.abs(to) * 20
                    }
                    PauseAnimation { duration: 2000 }
                    PropertyAction { target: rowText; property: "x"; value: 0 }
                }
            }
        }

        Keys.onUpPressed: {
            if (count === 0) return
            if (currentIndex > 0) currentIndex--
            else currentIndex = count - 1
            scriptList.positionViewAtIndex(scriptList.currentIndex, ListView.Contain)
        }
        Keys.onDownPressed: {
            if (count === 0) return
            if (currentIndex < count - 1) currentIndex++
            else currentIndex = 0
            scriptList.positionViewAtIndex(scriptList.currentIndex, ListView.Contain)
        }
    }

    // Flat Panel's pages for the list (Settings → Look → Menu Layout): the
    // frame says which.
    ListPager { list: scriptList }

    // The help line: the selected script's file and sidecar flags, so what
    // was parsed out of the .txt is visible without leaving the app.
    HelpLine {
        visible: !!itemsRoot.currentScript
        text: {
            var s = itemsRoot.currentScript
            if (!s) return ""
            var flags = []
            if (s.favorite) flags.push("FAVORITE")
            if (s.confirm)  flags.push("CONFIRM")
            return [s.basename].concat(flags).join("\n")
        }
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.helpLineMargin
        anchors.leftMargin: root.sw * 0.125 //80
    }

    Component.onCompleted: {
        var loaded = scriptsBackend.getScripts()
        scriptList.model = loaded
        if (loaded.length > 0) {
            var restore = (navListState.currentIndex !== undefined) ? navListState.currentIndex : 0
            scriptList.currentIndex = Math.min(restore, loaded.length - 1)
            scriptList.positionViewAtIndex(scriptList.currentIndex, ListView.Contain)
        }
        scriptList.forceActiveFocus()

        // Routed here by Root.qml because a main-menu favorite needs confirming:
        // select that script and open the prompt straight away. Cancelling leaves
        // the user on this list, which is a reasonable place to end up.
        // This view exists only to host the prompt when a main-menu favorite needs
        // confirming, so once its script has run there is nothing here for the user
        // — go back to where the favorite was picked rather than re-prompting.
        if (navParams.confirmScript && navListState.ranScript) {
            Qt.callLater(function() { itemsRoot.goBack() })
            return
        }

        // Deferred, because Root.qml's Loader calls forceActiveFocus() on this view
        // after onCompleted returns — opening the overlay before that would hand
        // focus straight back to the list.
        if (navParams.confirmScript) {
            Qt.callLater(function() {
                for (var i = 0; i < loaded.length; ++i) {
                    if (loaded[i].basename === navParams.confirmScript) {
                        scriptList.currentIndex = i
                        scriptList.positionViewAtIndex(i, ListView.Contain)
                        itemsRoot.activate(loaded[i])
                        return
                    }
                }
                console.log("[Scripts] favorite to confirm no longer exists: "
                            + navParams.confirmScript)
            })
        }
    }

    // --- RUN CONFIRMATION OVERLAY --- (sidecar "confirm = yes"). A window
    // over the list: every key stops at it (► would otherwise mark the
    // script a favourite under it), but for a chord like Ctrl+Q.
    ChoiceOverlay {
        id: confirmOverlay
        anchors.fill: parent
        // Above the list AND the footer, which is a later sibling and would
        // otherwise draw through the overlay.
        z: 100
        promptText: "Run script?"
        subtitleText: itemsRoot.pendingScript ? itemsRoot.pendingScript.name : ""
        // Cancel first so a stray ENTER can't run something destructive.
        choices: [{ label: "Cancel", action: "cancel" },
                  { label: "Run",    action: "run" }]

        function dismiss() {
            itemsRoot.pendingScript = null
            // Arrived here only to confirm a main-menu favorite: cancelling should
            // return to the menu, not strand the user in a list they never asked for.
            if (navParams.confirmScript) itemsRoot.goBack()
            else                         scriptList.forceActiveFocus()
        }

        onActivated: function(action) {
            var entry = itemsRoot.pendingScript
            if (action === "run" && entry) {
                // Not through dismiss(): for a favorite that path navigates
                // back, which destroys this view before launch() can be
                // called on it.
                itemsRoot.pendingScript = null
                itemsRoot.launch(entry)
            } else {
                confirmOverlay.dismiss()
            }
        }
        onCanceled: confirmOverlay.dismiss()
    }

    // Footer
    HintBar {
        id: footer
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE "
              + root.hints.browse
              + ((itemsRoot.currentScript && itemsRoot.currentScript.favorite)
                 ? ":UNFAVORITE " : ":FAVORITE ")
              + root.hints.select + ":RUN"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }
}
