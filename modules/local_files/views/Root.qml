import QtQuick
import Components

ModuleRouter {
    id: moduleRoot
    moduleId: "com.osdos.local_files"
    viewsUrl: Qt.resolvedUrl(".")

    Component.onCompleted: {
        // The startup favourite, when the app opens on one (Main.qml), or the
        // file left playing behind the menus, from the main menu's row for it.
        navigateToView("Items.qml", { startupPlay: navParams.startupPlay,
                                      resumePlayer: navParams.resumePlayer })
    }
}
