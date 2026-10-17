import QtQuick
import Components

// Settings → Arcade → RetroAchievements Account: the account RetroArch plays
// FinalBurn Neo's games under for Online High Scores. Signing in hands the
// password to ArcadeBackend for RetroArch to sign in with at the next game,
// which swaps it for a token (RetroAchievements lets an emulator sign in, not
// a launcher); the page says how that went (achievementsState).
FocusScope {
    id: accountRoot

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var navParams: ({})

    focus: true

    readonly property string user: arcadeBackend ? arcadeBackend.achievementsUser : ""
    readonly property string accountState: arcadeBackend ? arcadeBackend.achievementsState : "none"
    readonly property string message: arcadeBackend ? arcadeBackend.achievementsMessage : ""
    // What is typed for the next sign-in.
    property string typedUser: ""
    property string typedPassword: ""
    // The keyboard is for: "user" or "password".
    property string typing: ""

    readonly property string stateText: accountState === "signedIn" ? "Signed In"
                                      : accountState === "pending" ? "At the Next Game"
                                      : accountState === "failed" ? "Not Signed In" : "None"
    // The page's lines, by key: the list is made again only when a line
    // comes or goes (signed in), the cursor kept on its line.
    property var keys: []
    function updateKeys() {
        var made = ["state", "status"]
        if (accountState === "signedIn" || accountState === "pending")
            made.push("signOut")
        else
            made.push("user", "password", "signIn")
        if (JSON.stringify(made) === JSON.stringify(keys))
            return
        var at = keys[list.currentIndex]
        keys = made
        // The list made again, on the line it was on, or the first (a line
        // gone: signed in).
        list.currentIndex = Math.max(0, made.indexOf(at))
    }
    onAccountStateChanged: updateKeys()
    Component.onCompleted: updateKeys()

    function labelOf(key) {
        switch (key) {
        case "state": return "Account"
        case "status": return "Status"
        case "signOut": return "Sign Out"
        case "user": return "User"
        case "password": return "Password"
        case "signIn": return "Sign In"
        }
        return ""
    }
    function valueOf(key) {
        switch (key) {
        case "state": return user !== "" ? user : "None"
        case "status": return stateText
        case "user": return typedUser !== "" ? typedUser : "None"
        case "password": return typedPassword !== "" ? "*".repeat(typedPassword.length) : "None"
        }
        return ""
    }
    function descriptionOf(key) {
        switch (key) {
        case "state":
            return accountState === "signedIn" ? "Signed in as " + user + ": the scores of FinalBurn Neo's games go to RetroAchievements"
                 : accountState === "pending" ? "RetroArch signs in as " + user + " at the next game FinalBurn Neo plays"
                 : accountState === "failed" ? message
                 : "No account: the games play without RetroAchievements"
        case "status":
            return "Online High Scores plays FinalBurn Neo's games in hardcore mode, as RetroAchievements' leaderboards take only those: no save states"
        case "signOut": return "Forget the account: the games play without RetroAchievements"
        case "user": return "Your RetroAchievements user name"
        case "password":
            return "Used once, by RetroArch, which keeps a token in its place. Debian's RetroArch sends it unencrypted: use one you use nowhere else"
        case "signIn":
            return typedUser !== "" && typedPassword !== "" ? "RetroArch signs in at the next game FinalBurn Neo plays"
                                                            : "Type your user name and password first"
        }
        return ""
    }

    function choose(key) {
        if (!arcadeBackend)
            return
        switch (key) {
        case "user":
            typing = "user"
            keyboard.title = "RetroAchievements User"
            keyboard.masked = false
            keyboard.open(typedUser)
            break
        case "password":
            typing = "password"
            keyboard.title = "RetroAchievements Password"
            keyboard.masked = true
            keyboard.open("")
            break
        case "signIn":
            if (typedUser !== "" && typedPassword !== "") {
                arcadeBackend.signInAchievements(typedUser, typedPassword)
                typedPassword = ""
            }
            break
        case "signOut":
            arcadeBackend.signOutAchievements()
            break
        }
    }

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: "RetroAchievements"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    MenuList {
        id: list
        model: accountRoot.keys
        visible: !keyboard.visible
        focus: !keyboard.visible

        delegate: MenuRow {
            width: list.width
            height: root.sh * 0.0583333 //28
            label: accountRoot.labelOf(modelData)
            value: accountRoot.valueOf(modelData)
            selected: list.currentIndex === index
            keepValue: true
        }

        Keys.onReturnPressed: accountRoot.choose(accountRoot.keys[currentIndex])
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                accountRoot.goBack()
                event.accepted = true
            }
        }
    }

    HelpLine {
        always: true
        visible: !keyboard.visible
        text: accountRoot.descriptionOf(accountRoot.keys[list.currentIndex])
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.helpLineMargin
        anchors.leftMargin: root.sw * 0.125 //80
    }

    HintBar {
        readonly property string key: accountRoot.keys[list.currentIndex] || ""
        visible: !keyboard.visible
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE"
              + (key !== "" && key !== "state" && key !== "status" ? " " + root.hints.select + ":"
                 + (key === "user" || key === "password" ? "TYPE" : "SELECT") : "")
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }

    OnScreenKeyboard {
        id: keyboard
        anchors.fill: parent
        caseSensitive: true
        maxLength: 64
        onAccepted: function(text) {
            if (accountRoot.typing === "user")
                accountRoot.typedUser = text.trim()
            else if (accountRoot.typing === "password")
                accountRoot.typedPassword = text
            accountRoot.typing = ""
            list.forceActiveFocus()
        }
        onCanceled: {
            accountRoot.typing = ""
            list.forceActiveFocus()
        }
    }
}
