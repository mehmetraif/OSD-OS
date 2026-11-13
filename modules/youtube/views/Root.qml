import QtQuick

FocusScope {
    id: moduleRoot

    signal goBack()

    property var navParams: ({})

    property string moduleId: "com.osdos.youtube"

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

    // The startup favourite, when the app opens on one (Main.qml), the video
    // left playing behind the menus, from the main menu's row for it
    // (resumePlayer), or the sign-in page (SIGN IN in the module's settings),
    // whose back returns there.
    Component.onCompleted: {
        if (typeof youtubeBackend !== "undefined" && youtubeBackend)
            youtubeBackend.opened()
        if (navParams.signIn)
            navigateTo("SignIn.qml", navParams)
        else
            navigateTo("Items.qml", { startupPlay: navParams.startupPlay,
                                      resumePlayer: navParams.resumePlayer })
    }
}
