import QtQuick
import Components

ModuleRouter {
    id: moduleRoot
    moduleId: "com.osdos.ambient_mode"
    viewsUrl: Qt.resolvedUrl(".")

    Component.onCompleted: navigateToView("Items.qml", navParams)
}
