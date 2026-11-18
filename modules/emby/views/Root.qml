import QtQuick
import Components

ModuleRouter {
    id: moduleRoot
    moduleId: "com.osdos.emby"
    viewsUrl: Qt.resolvedUrl(".")

    // Handle auth state and logout from backend
    Connections {
        target: embyBackend
        // Sign-out and token revocation both clear the auth state, so
        // onAuthStateChanged below already routes back to Auth.qml for them.
        // Navigating here as well loaded that view twice.
        function onAuthStateChanged() {
            var state = embyBackend.get_auth_state()
            if (state === "authed") {
                // Single post-auth funnel for both direct sign-in and Emby Connect:
                // persist the server URL the backend actually authenticated against
                // (the Connect flow picks it from the account, not the Auth field).
                appCore.save_setting(moduleId, "server_url", embyBackend.get_server_url())
                moduleRoot.replaceWith("Libraries.qml", {})
            } else {
                moduleRoot.replaceWith("Auth.qml", {})
            }
        }
    }

    Component.onCompleted: {
        // Show an initial view synchronously so the module always renders a
        // screen, even if the background token check stalls on an unreachable
        // server. check_auth() then validates the stored token; onAuthStateChanged
        // (and onAuthRevoked) re-route to Auth.qml if it was revoked.
        if (embyBackend.get_auth_state() === "authed") {
            replaceWith("Libraries.qml", {})
            embyBackend.check_auth()
        } else {
            replaceWith("Auth.qml", {})
        }
    }
}
