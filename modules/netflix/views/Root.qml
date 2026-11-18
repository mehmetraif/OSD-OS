import QtQuick
import Components

// Netflix module router: Browse.qml lists the catalogue in the tree, and
// Launch.qml opens a title (or the home page) in the service's own player, or
// the sign-in page (SIGN IN in the module's settings: navParams.signIn).
ModuleRouter {
    id: moduleRoot
    moduleId: "com.osdos.netflix"
    viewsUrl: Qt.resolvedUrl(".")

    // Browse.qml's back at the top falls through the empty stack to goBack(),
    // so the user lands on the main menu; the sign-in page's, back on the
    // settings.
    Component.onCompleted: navigateToView(navParams.signIn ? "Launch.qml" : "Browse.qml", navParams)
}
