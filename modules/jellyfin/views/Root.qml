import QtQuick
import Components

ModuleRouter {
    id: moduleRoot
    moduleId: "com.osdos.jellyfin"
    viewsUrl: Qt.resolvedUrl(".")

    // Handle auth state and logout from backend
    Connections {
        target: jellyfinBackend
        // Sign-out and token revocation both clear the auth state, so
        // onAuthStateChanged below already routes back to Auth.qml for them.
        // Navigating here as well loaded that view twice, and pushed the view
        // signed out of for back to return to.
        function onAuthStateChanged() {
            var state = jellyfinBackend.get_auth_state()
            if (state === "authed") {
                // Single post-auth funnel for both the password and Quick
                // Connect sign-ins: persist the server URL the backend
                // authenticated against.
                appCore.save_setting(moduleId, "server_url", jellyfinBackend.get_server_url())
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
        // re-routes to Auth.qml if it was revoked.
        if (jellyfinBackend.get_auth_state() === "authed") {
            replaceWith("Libraries.qml", {})
            jellyfinBackend.check_auth()
        } else {
            replaceWith("Auth.qml", {})
        }
    }
}
