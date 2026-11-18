import QtQuick
import Components

ModuleRouter {
    id: moduleRoot
    moduleId: "com.osdos.plex"
    viewsUrl: Qt.resolvedUrl(".")

    // Opened for an NFC card (navParams.cardRef): a card never prompts, so a
    // sign-in lost on the way is no PinAuth here. The views a card reaches
    // say so themselves: CardPlay and QueuePlay in their notice, a card's
    // Player in its own as a video it waited for won't come, and back from
    // any of them returns to the card reader.
    readonly property bool forCard: !!navParams.cardRef

    // Signed out (logout, or the token refused and not renewed): auth in
    // place of every view (replaceWith), so the view signed out of isn't
    // left on the stack for back to return to: back from PinAuth leaves the
    // module.
    function signedOut() {
        if (forCard)
            return
        replaceWith("PinAuth.qml", {})
    }
    Connections {
        target: plexBackend
        function onLogoutComplete() { moduleRoot.signedOut() }
        function onAuthRevoked() { moduleRoot.signedOut() }
    }

    Component.onCompleted: {
        plexBackend.reset_device_check()
        var state = plexBackend.get_auth_state()

        // NFC card deep-link. Deliberately ahead of — and exclusive of — the
        // auth/user gate below: a card must play as whoever is already the active
        // user, never prompt a profile switch. (Falling through to the gate would
        // land a card tap on UserSelect whenever auto_sign_in is off, and switching
        // profiles from a card would sidestep the profile PIN.) A pending PIN or a
        // missing sign-in is surfaced by CardPlay as an error, not a prompt.
        if (navParams.cardRef) {
            navigateToView("CardPlay.qml", {
                cardRef:   navParams.cardRef,
                cardMode:  navParams.cardMode  || "",
                cardTitle: navParams.cardTitle || "",
                authState: state,
                pendingPin: plexBackend.pending_pin_user()
            })
            return
        }

        // A user picked in the app Settings screen whose switch was refused for
        // want of a PIN. Settings has no Plex view to prompt from, so the prompt
        // is deferred to here — ahead of auto_sign_in, which would otherwise walk
        // straight past it into Libraries with the previous user still active.
        var pendingPin = plexBackend.pending_pin_user()
        if (state === "authed" && pendingPin !== "") {
            navigateToView("ProfilePin.qml", { userId: pendingPin, reauth: true })
        } else if (state === "authed") {
            // UserSelect reads the users kept itself, given none.
            var autoSignIn = appCore.get_setting(moduleId, "auto_sign_in")
            if (autoSignIn !== true && autoSignIn !== "ON")
                navigateToView("UserSelect.qml", { reauth: true })
            else
                navigateToView("Libraries.qml", {})
        } else if (state === "needs_user") {
            // Have token but no server selected yet — show user list
            navigateToView("UserSelect.qml", {})
        } else {
            navigateToView("PinAuth.qml", {})
        }
    }
}
