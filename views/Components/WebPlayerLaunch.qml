import QtQuick

// A web player module's one view (Netflix, Prime Video): opens the service
// full screen and says how to come back. The service runs as its own web
// player in Chromium (see WebPlayerBackend), which has the screen until it is
// closed: with Ctrl+W, or by holding BACK here, since this view keeps
// receiving keys while the browser is up (both read the input devices). On a
// headless Pi this view is not drawn while the browser runs: Qt has handed the
// screen over and its rendering is suspended, so what it shows just before the
// hand-off is what the user reads.
//
// It opens navParams.item, a title from the catalogue (WebPlayerBrowse), at the
// page Wikidata knows for it on the service or at the service's search for its
// name; without one, the service's home page. With navParams.signIn (SIGN IN in
// the module's settings), the service's sign-in page, once the user says so:
// signing in takes a keyboard, which they may have to fetch first.
//
// A module's Launch.qml is just this, with its backend and name:
//     WebPlayerLaunch { backend: netflixBackend; serviceName: "Netflix" }
FocusScope {
    id: launchRoot

    property var navParams: ({})
    // The module's WebPlayerBackend.
    property var backend: null
    property string serviceName: ""
    // What is being opened, for the screen: a title's name, or the service's.
    readonly property string opening: navParams.name || serviceName
    // The page to open; "" for the home page. A title's is looked up first.
    property string url: ""
    property bool urlKnown: !navParams.item
    readonly property bool signIn: navParams.signIn === true
    // A word on signing in, under how to come back (YouTube's on its account).
    property string signInNote: ""

    signal goBack()

    focus: true

    // "ready" (the sign-in page, waiting for SELECT), "opening", "running",
    // "closed" (by holding BACK, which is still down), "error" (never
    // started) or "failed" (closed badly).
    property string phase: navParams.signIn === true ? "ready" : "opening"
    // BACK is down on this view: a hold to close the browser, or what is left
    // of one once it has.
    property bool backHeld: false
    property string message: ""
    property string output: ""
    readonly property bool running: backend ? backend.running : false

    function isBack(key) {
        return key === Qt.Key_Escape || key === Qt.Key_Backspace || key === Qt.Key_Back
    }
    // While the browser is open, Backspace is for its text fields: holding it
    // to clear one mustn't close the browser.
    function holdsBack(key) {
        return key === Qt.Key_Escape || key === Qt.Key_Back
    }

    function launch() {
        if (launchRoot.backend.launch(launchRoot.url)) {
            launchRoot.phase = "running"
        } else {
            launchRoot.phase = "error"
            launchRoot.message = launchRoot.backend.lastError()
        }
    }

    Component.onCompleted: {
        if (signIn && backend)
            url = backend.signInUrl
        else if (navParams.item && backend)
            backend.catalog.resolveTitleUrl(navParams.item)
    }
    Connections {
        target: launchRoot.backend ? launchRoot.backend.catalog : null
        function onTitleUrlReady(path, url) {
            // Another title's, asked for by a view since closed.
            if (launchRoot.urlKnown || path !== launchRoot.navParams.item.path) return
            launchRoot.url = url
            launchRoot.urlKnown = true
            if (!launchTimer.running && launchRoot.phase === "opening")
                launchRoot.launch()
        }
    }

    // Select on a title opens it straight away: one frame of this view first,
    // so it is on screen before a headless Pi hands the screen over. The first
    // time this run, long enough to read how to come back, since the screen is
    // dark while the browser starts. A title still being looked up opens once
    // it is found. The sign-in page waits for SELECT, by which time how to come
    // back has been read.
    Timer {
        id: launchTimer
        interval: launchRoot.signIn || (launchRoot.backend && launchRoot.backend.opened) ? 50 : 1200
        running: !launchRoot.signIn
        onTriggered: if (launchRoot.urlKnown) launchRoot.launch()
    }

    // Holding BACK this long closes the browser.
    Timer {
        id: holdTimer
        interval: 2000
        onTriggered: launchRoot.backend.close()
    }

    // While the browser is open its keys are for typing, and the app reads the
    // same keyboard. With a text field focused here, InputManager leaves them
    // as they are: Right Shift stays Shift rather than standing in for BACK
    // (held for an "@", it closed the browser), and remote remaps don't fire.
    // The keys it doesn't take still reach the handlers below.
    TextInput {
        id: typingFocus
        width: 1
        height: 1
        opacity: 0
        maximumLength: 0
        focus: launchRoot.running
    }

    // An open browser counts as activity: a screen saver would take the keys
    // this view needs to hold BACK, and be what greets the user when the
    // browser closes.
    Timer {
        interval: 10000
        repeat: true
        triggeredOnStart: true
        running: launchRoot.running
        onTriggered: if (idleTracker) idleTracker.resetActivity()
    }

    Connections {
        target: launchRoot.backend
        function onFinished(exitCode, reason) {
            holdTimer.stop()
            // Closed, either way round: back to where it was opened from, as
            // after playback. Closed by holding BACK, once that is let go: its
            // repeats would carry on back through the menus.
            if (reason === "ok" || reason === "stopped") {
                if (launchRoot.backHeld)
                    launchRoot.phase = "closed"
                else
                    launchRoot.goBack()
                return
            }
            launchRoot.phase = "failed"
            launchRoot.message = reason === "failed_to_start" ? "The browser did not start"
                                                               : "The browser closed with error " + exitCode
            launchRoot.output = launchRoot.backend.output()
        }
    }

    Keys.onPressed: function(event) {
        // Every key is taken here: while the browser is open it belongs to the
        // browser, and nothing may reach the main menu underneath.
        event.accepted = true
        if (launchRoot.phase === "ready") {
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                launchRoot.phase = "opening"
                launchTimer.start()
            } else if (isBack(event.key)) {
                launchRoot.goBack()
            }
            return
        }
        if (launchRoot.running) {
            if (holdsBack(event.key) && !event.isAutoRepeat) {
                launchRoot.backHeld = true
                holdTimer.restart()
            }
            return
        }
        if (!isBack(event.key)) return
        if (launchRoot.phase === "closed") {
            // A new press: the release went elsewhere.
            if (!event.isAutoRepeat) launchRoot.goBack()
        } else if (launchRoot.phase === "opening") {
            launchTimer.stop()
            launchRoot.urlKnown = false
            launchRoot.phase = "canceled"
            launchRoot.goBack()
        } else if (launchRoot.phase === "error" || launchRoot.phase === "failed") {
            launchRoot.goBack()
        }
    }
    Keys.onReleased: function(event) {
        event.accepted = true
        if (!holdsBack(event.key) || event.isAutoRepeat) return
        launchRoot.backHeld = false
        holdTimer.stop()
        if (launchRoot.phase === "closed") launchRoot.goBack()
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

    Column {
        x: root.sw * 0.125 //80
        y: root.sh * 0.25 //120
        width: root.sw * 0.75 //480
        spacing: root.sh * 0.025 //12

        // The deck's own display while a tape plays, as on the boot screen.
        Row {
            visible: !launchRoot.signIn
                     && (launchRoot.phase === "opening" || launchRoot.phase === "running")
            spacing: root.sw * 0.0125 //8
            Text {
                id: playLabel
                text: "PLAY"
                color: root.primaryColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.05 //24
            }
            PixelIcon {
                name: "play"
                color: root.primaryColor
                anchors.verticalCenter: playLabel.verticalCenter
            }
        }

        Text {
            width: parent.width
            text: launchRoot.signIn && (launchRoot.phase === "ready" || launchRoot.phase === "opening")
                    ? "Sign in to " + launchRoot.serviceName
                : launchRoot.phase === "opening" ? "Opening " + launchRoot.opening
                : launchRoot.phase === "running" ? launchRoot.serviceName + " has the screen"
                : launchRoot.phase === "closed" ? launchRoot.serviceName + " is closed"
                : "Could not open " + launchRoot.serviceName
            color: root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.05 //24
            wrapMode: Text.WordWrap
        }

        // How to come back, in a box, the way a deck prints a notice.
        Rectangle {
            visible: launchRoot.phase === "ready" || launchRoot.phase === "opening"
                     || launchRoot.phase === "running"
            width: parent.width
            height: howTo.height + 2 * root.sh * 0.025
            color: "transparent"
            border.width: root.px
            border.color: root.primaryColor
            antialiasing: false
            Column {
                id: howTo
                x: root.sw * 0.0125 //8
                y: root.sh * 0.025 //12
                width: parent.width - 2 * x
                spacing: root.sh * 0.0125 //6
                Repeater {
                    model: (launchRoot.signIn ? ["Sign in with a keyboard, then"] : []).concat([
                        "Hold " + root.hints.back + " for 2 seconds",
                        "to come back to OSD/OS",
                        "or close " + launchRoot.serviceName + " with Ctrl+W"
                    ])
                    Text {
                        required property string modelData
                        width: parent.width
                        text: modelData
                        color: root.primaryColor
                        font.family: root.globalFont
                        font.capitalization: Font.AllUppercase
                        font.pixelSize: root.sh * 0.0375 //18
                        elide: Text.ElideRight
                    }
                }
            }
        }

        Text {
            visible: launchRoot.phase === "ready" && launchRoot.signInNote !== ""
            width: parent.width
            text: launchRoot.signInNote
            color: root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.0375 //18
            wrapMode: Text.WordWrap
        }

        // Why it did not open, and what the browser said.
        Text {
            visible: launchRoot.phase === "error" || launchRoot.phase === "failed"
            width: parent.width
            text: launchRoot.message
            color: root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.0375 //18
            wrapMode: Text.WordWrap
        }
        // How to put the browser on, or its screen without a desktop (cage),
        // for this system (AppCore::installHint()).
        Text {
            readonly property string hint: launchRoot.phase === "error"
                ? appCore.installHint(launchRoot.message === "cage is not installed" ? "cage" : "browser") : ""
            visible: hint !== ""
            width: parent.width
            text: hint
            color: root.primaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.0291667 //14
            wrapMode: Text.WordWrap
        }
        Rectangle {
            visible: launchRoot.phase === "failed" && launchRoot.output !== ""
            width: parent.width
            height: root.sh * 0.25 + root.hintRoom //120
            color: "transparent"
            border.width: root.px
            border.color: root.primaryColor
            antialiasing: false
            clip: true
            Text {
                anchors.fill: parent
                anchors.margins: root.sw * 0.0125 //8
                text: launchRoot.output
                textFormat: Text.PlainText
                // The end of the output is where the error is.
                verticalAlignment: Text.AlignBottom
                color: root.primaryColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0291667 //14
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            }
        }
    }

    HintBar {
        text: launchRoot.phase === "ready" ? root.hints.back + ":BACK " + root.hints.select + ":SIGN IN"
            : launchRoot.phase === "error" || launchRoot.phase === "failed" || launchRoot.phase === "closed"
            ? root.hints.back + ":BACK"
            : root.hints.back + " HOLD:RETURN"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }
}
