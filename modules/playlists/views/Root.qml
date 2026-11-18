import QtQuick
import Components

ModuleRouter {
    id: moduleRoot
    moduleId: "com.osdos.playlists"
    viewsUrl: Qt.resolvedUrl(".")

    Component.onCompleted: {
        // The list left playing behind the menus, from the main menu's row
        // for it: its page, then its player.
        navigateToView("Playlists.qml", { resumePlayer: navParams.resumePlayer })
    }
}
