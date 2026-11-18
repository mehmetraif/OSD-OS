import QtQuick
import Components

ModuleRouter {
    id: moduleRoot
    moduleId: "com.osdos.highscores"
    viewsUrl: Qt.resolvedUrl(".")

    Component.onCompleted: {
        // MAME's score files looked at again, and those changed since read,
        // before the list asks for what is in them.
        if (highScoresBackend)
            highScoresBackend.refresh()
        navigateToView("Scores.qml", {})
    }
}
