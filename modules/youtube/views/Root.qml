import QtQuick
import Components

ModuleRouter {
    id: moduleRoot
    moduleId: "com.osdos.youtube"
    viewsUrl: Qt.resolvedUrl(".")

    // The startup favourite, when the app opens on one (Main.qml), the video
    // left playing behind the menus, from the main menu's row for it
    // (resumePlayer), or the sign-in page (SIGN IN in the module's settings),
    // whose back returns there.
    Component.onCompleted: {
        if (typeof youtubeBackend !== "undefined" && youtubeBackend)
            youtubeBackend.opened()
        if (navParams.signIn)
            navigateToView("SignIn.qml", navParams)
        else
            navigateToView("Items.qml", { startupPlay: navParams.startupPlay,
                                          resumePlayer: navParams.resumePlayer })
    }
}
