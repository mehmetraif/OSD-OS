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
// takes, or else at the next game. The Account line says how that went
// (achievementsState), and the key's whether RetroAchievements took it,
// which it is asked about as it is typed and as the page opens. Sign Out and
// Forget Web API Key ask first.
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

    // The account in a line: who is signed in, or how far signing in has
    // got.
    readonly property string accountText: accountState === "signedIn" ? user
                                        : accountState === "checking" ? "Signing In"
                                        : accountState === "pending" ? "At the Next Game"
                                        : "Not Signed In"
    covered: keyboard.visible
    // The page's lines, by key: signed in or not, a key kept or not. As the
    // state changes, read from it then, not from a binding on it that may
    // not have its new value yet.
    function updateKeys() {
        var made = ["state"]
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
        case "signOut": return "Sign Out"
        case "user": return "User Name"
        case "password": return "Password"
        case "signIn": return "Sign In"
        case "key": return "Web API Key"
        case "forgetKey": return "Forget Web API Key"
        }
        return ""
    }
    function valueOf(key) {
        switch (key) {
        case "state": return accountText
        case "user": return typedUser !== "" ? typedUser : "Not Set"
        case "password": return typedPassword !== "" ? "*".repeat(typedPassword.length) : "Not Set"
        case "key": return !keyKept ? "Not Set" : keyRefused ? "Refused" : keyWorks ? "Working" : "Set"
        }
        return ""
    }
    // Sign In waits for both.
    function offOf(key) {
        return key === "signIn" && (typedUser === "" || typedPassword === "") ? "Type your user name and password first" : ""
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
                 : accountState === "failed" && message !== "" ? message
                 : "No account: the games play without RetroAchievements"
        case "signOut": return "Forget the account and its token: the games play without RetroAchievements"
        case "user": return "Your RetroAchievements user name, as you sign in on retroachievements.org"
        case "password":
            // Refused: why, where the cursor lands to type it again.
            if (accountState === "failed" && message !== "" && typedPassword === "")
                return message
            return "Used once, by RetroArch, which keeps a token in its place. Debian's RetroArch sends it unencrypted: use one you use nowhere else"
        case "signIn":
            return "RetroArch signs in now, out of sight, and the Account line says how it went"
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
            keyboard.title = "RetroAchievements User Name"
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
            ask("Sign out of RetroAchievements?", user, "Sign Out", "signOut")
            break
        case "key":
            typing = "key"
            keyboard.title = "Web API Key"
            keyboard.masked = false
            keyboard.open("")
            break
        case "forgetKey":
            ask("Forget the Web API key?", "It is typed again from retroachievements.org, Settings, Keys",
                "Forget", "forgetKey")
            break
        }
    }
    // The answer to Sign Out's or Forget Web API Key's question.
    function confirmed(action) {
        if (!arcadeBackend)
            return
        if (action === "signOut")
            arcadeBackend.signOutAchievements()
        else if (action === "forgetKey")
            arcadeBackend.setAchievementsKey("")
    }

    function selectWord(key) {
        return key === "user" || key === "password" || key === "key" ? "TYPE"
             : key === "signIn" ? "SIGN IN" : key === "signOut" ? "SIGN OUT" : key === "forgetKey" ? "FORGET" : ""
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
