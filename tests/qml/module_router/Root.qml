import QtQuick
import Components

// A module's Root as the modules write theirs, for tst_module_router.qml: a
// ModuleRouter with the module's id and its views' folder, opening its
// first page; and a view it opens its own way (Own.qml), as the Arcade does
// its games.
ModuleRouter {
    id: moduleRoot
    moduleId: "com.osdos.router_test"
    viewsUrl: Qt.resolvedUrl(".")

    // The views opened its own way, and something of the Root's own that
    // changes (as the Arcade's credits do).
    property var ownOpened: []
    property int credits: 0
    opensOwn: function(path, params, listState) {
        if (path !== "Own.qml")
            return false
        moduleRoot.ownOpened = moduleRoot.ownOpened.concat([params.name])
        return true
    }

    Component.onCompleted: navigateToView("Page.qml", { name: "first", inFrame: true })
}
