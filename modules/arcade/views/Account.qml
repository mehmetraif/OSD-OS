import QtQuick
import Components

// Settings → Arcade → RetroAchievements Account: the account RetroArch plays
// FinalBurn Neo's games under for Online High Scores, and the Web API key the
// Arcade screen reads their leaderboards and pictures with
// (ArcadeBackend::leaderboard(), gameDetails()), and Game Details their
// achievements (gameAchievements(), which of them the account's user has
// earned).
// Signing in hands the password to ArcadeBackend for RetroArch to sign in
// with, which swaps it for a token (RetroAchievements lets an emulator sign
// in, not a launcher): at once, RetroArch running unseen for the moment it
// takes, or else at the next game. The page says how that went
// (achievementsState), and whether RetroAchievements took the key, which it
// is asked about as it is typed and as the page opens.
KeyedPage {
    id: accountRoot
    subtitle: "RetroAchievements"

    readonly property string user: arcadeBackend ? arcadeBackend.achievementsUser : ""
    readonly property string accountState: arcadeBackend ? arcadeBackend.achievementsState : "none"
    readonly property string message: arcadeBackend ? arcadeBackend.achievementsMessage : ""
    readonly property bool keyKept: arcadeBackend ? arcadeBackend.achievementsKey : false
    readonly property bool keyRefused: arcadeBackend ? arcadeBackend.achievementsKeyRefused : false
    readonly property bool keyWorks: arcadeBackend ? arcadeBackend.achievementsKeyWorks : false
    // What is typed for the next sign-in.
    property string typedUser: ""
    property string typedPassword: ""
    // The keyboard is for: "user", "password" or "key".
    property string typing: ""

    readonly property string stateText: accountState === "signedIn" ? "Signed In"
                                      : accountState === "checking" ? "Checking"
                                      : accountState === "pending" ? "At the Next Game"
                                      : accountState === "failed" ? "Not Signed In" : "None"
    covered: keyboard.visible
    // The page's lines, by key: signed in or not, a key kept or not.
    function updateKeys() {
        var made = ["state", "status"]
        if (accountState === "signedIn" || accountState === "pending" || accountState === "checking")
            made.push("signOut")
        else
            made.push("user", "password", "signIn")
        made.push("key")
        if (keyKept)
            made.push("forgetKey")
        setKeys(made)
    }
    onAccountStateChanged: {
        updateKeys()
        // Refused: the user as it was, the cursor on the password to type
        // again.
        if (accountState === "failed") {
            if (typedUser === "")
                typedUser = user
            currentIndex = Math.max(0, keys.indexOf("password"))
        }
    }
    onKeyKeptChanged: updateKeys()
    Component.onCompleted: {
        updateKeys()
        if (arcadeBackend)
            arcadeBackend.checkAchievementsKey()
    }

    function labelOf(key) {
        switch (key) {
        case "state": return "Account"
        case "status": return "Status"
        case "signOut": return "Sign Out"
        case "user": return "User"
        case "password": return "Password"
        case "signIn": return "Sign In"
        case "key": return "Web API Key"
        case "forgetKey": return "Forget Web API Key"
        }
        return ""
    }
    function valueOf(key) {
        switch (key) {
        case "state": return user !== "" ? user : "None"
        case "status": return stateText
        case "user": return typedUser !== "" ? typedUser : "None"
        case "password": return typedPassword !== "" ? "*".repeat(typedPassword.length) : "None"
        case "key": return !keyKept ? "None" : keyRefused ? "Refused" : keyWorks ? "Working" : "Kept"
        }
        return ""
    }
    function descriptionOf(key) {
        switch (key) {
        case "state":
            return accountState === "signedIn"
                       ? "Signed in as " + user + ": the scores of FinalBurn Neo's games go to RetroAchievements"
                         + (keyKept ? "" : ". The pictures and achievements need the Web API Key too")
                 : accountState === "checking" ? "RetroArch is signing in as " + user + ", out of sight"
                 : accountState === "pending" ? (message !== "" ? message
                                                                : "RetroArch signs in as " + user + " at the next game FinalBurn Neo plays")
                 : accountState === "failed" ? message
                 : "No account: the games play without RetroAchievements"
        case "status":
            return "Online High Scores plays FinalBurn Neo's games in hardcore mode, as RetroAchievements' leaderboards take only those: no save states"
        case "signOut": return "Forget the account: the games play without RetroAchievements"
        case "user": return "Your RetroAchievements user name"
        case "password":
            // Refused: why, where the cursor lands to type it again.
            if (accountState === "failed" && message !== "" && typedPassword === "")
                return message
            return "Used once, by RetroArch, which keeps a token in its place. Debian's RetroArch sends it unencrypted: use one you use nowhere else"
        case "signIn":
            return typedUser !== "" && typedPassword !== "" ? "RetroArch signs in now, out of sight, and the Status line says how it went"
                                                            : "Type your user name and password first"
        case "key":
            return keyRefused ? "RetroAchievements refused the key: type it again from retroachievements.org, Settings, Keys"
                 : keyWorks ? "RetroAchievements takes the key: the games' pictures and leaderboards on the Arcade screen, and the achievements in Game Details"
                 : "Needed for the games' pictures and leaderboards on the Arcade screen, and the achievements in Game Details: retroachievements.org, Settings, Keys"
        case "forgetKey": return "The Arcade screen shows this machine's own high scores again, and no more pictures are downloaded"
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
        case "key":
            typing = "key"
            keyboard.title = "Web API Key"
            keyboard.masked = false
            keyboard.open("")
            break
        case "forgetKey":
            arcadeBackend.setAchievementsKey("")
            break
        }
    }

    function selectWord(key) {
        return key !== "" && key !== "state" && key !== "status"
               ? (key === "user" || key === "password" || key === "key" ? "TYPE" : "SELECT") : ""
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
            else if (accountRoot.typing === "key" && arcadeBackend)
                arcadeBackend.setAchievementsKey(text.trim())
            accountRoot.typing = ""
            accountRoot.focusList()
        }
        onCanceled: {
            accountRoot.typing = ""
            accountRoot.focusList()
        }
    }
}
