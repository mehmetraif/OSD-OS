import QtQuick

// Prime Video module router: Browse.qml lists the catalogue in the tree, and
// Launch.qml opens a title (or the home page) in the service's own player, or
// the sign-in page (SIGN IN in the module's settings: navParams.signIn).
FocusScope {
    id: moduleRoot

    signal goBack()

    property var navParams: ({})

    // The module's manifest id — the single place it appears in this module's QML.
    // Child views reference it via moduleRoot.moduleId.
    property string moduleId: "com.osdos.prime_video"
    property var _moduleInfo: appCore ? appCore.get_module_info(moduleId) : ({})
    property string moduleName: _moduleInfo.name || ""
    property string moduleIcon: _moduleInfo.icon || ""

    // In the layout's frame (Main.qml) while the view shown takes its place
    // in it: a browser or a list does, a player does not.
    readonly property bool inFrame: !!internalLoader.item && internalLoader.item.inFrame === true

    property var navStack: []
    property var currentParams: ({})

    function navigateTo(viewPath, params, fromState) {
        var resolved = Qt.resolvedUrl(viewPath)
        navStack.push({ source: internalLoader.source, params: currentParams, listState: fromState || {} })
        currentParams = params || {}
        root.changeWindow(internalLoader, resolved, { "navParams": params || {} })
    }

    function navigateBack() {
        if (navStack.length === 0) {
            moduleRoot.goBack()
            return
        }
        var prev = navStack.pop()
        if (!prev.source || prev.source.toString() === "") {
            moduleRoot.goBack()
            return
        }
        var restored = Object.assign({}, prev.params)
        restored.navListState = prev.listState || {}
        currentParams = restored
        root.changeWindow(internalLoader, prev.source, { "navParams": restored })
    }

    Loader {
        id: internalLoader
        anchors.fill: parent
        focus: true
        onLoaded: { if (item) item.forceActiveFocus() }

        Connections {
            target: internalLoader.item
            ignoreUnknownSignals: true
            function onNavigateTo(path, params, listState) { moduleRoot.navigateTo(path, params, listState) }
            function onGoBack() { moduleRoot.navigateBack() }
        }
    }

    // Browse.qml's back at the top falls through the empty stack to goBack(),
    // so the user lands on the main menu; the sign-in page's, back on the
    // settings.
    Component.onCompleted: navigateTo(navParams.signIn ? "Launch.qml" : "Browse.qml", navParams)
}
