import QtQuick

// A module's router, the root of its Root.qml: the module's own stack of
// views, the view shown in a Loader, and the way back through them. The
// shell loads Root.qml (the manifest's entry_point_qml) and listens for
// goBack(), which leaves the module. A Root is this with the module's id,
// its views' folder, and its own start and sign-in:
//
//     ModuleRouter {
//         id: moduleRoot
//         moduleId: "com.osdos.<name>"
//         viewsUrl: Qt.resolvedUrl(".")
//         Component.onCompleted: navigateToView("Items.qml", {})
//     }
//
// The views reach it by the Root's id, moduleRoot (this file's own is
// moduleRouter, and it never names the Root's): moduleRoot.moduleId,
// moduleName, moduleIcon, and whatever else the Root declares. They ask for
// a view with signals of their own, which the Loader's Connections below
// hand to the function of the same name: navigateTo(path, params,
// listState) to navigateToView(), replaceWith(path, params),
// replaceCurrent(path, params), updateBackItem(item), and goBack() to
// navigateBack().
FocusScope {
    id: moduleRouter

    // Leaves the module, back to what opened it (Main.qml's own stack): back
    // from the module's first view, and leaveModule().
    signal goBack()
    // Another module's view, opened on the app's own stack: Main.qml listens
    // for a signal of this name on the module (the NFC Reader's card handed
    // off to the module that plays it). The module's own views are
    // navigateToView()'s. It has to be a signal: Main.qml's Connections would
    // take a function of this name here for one, and Qt binds that to
    // whichever signal of the Root's own has its number, so a property of
    // the Root's changing would open a view on the app's stack.
    signal navigateTo(string path, var params, var listState)

    property var navParams: ({})

    // The module's manifest id: the single place it appears in the module's
    // QML, its Root. The views read it as moduleRoot.moduleId.
    property string moduleId: ""
    property var _moduleInfo: appCore ? appCore.get_module_info(moduleId) : ({})
    property string moduleName: _moduleInfo.name || ""
    property string moduleIcon: _moduleInfo.icon || ""

    // The folder of the module's views: Qt.resolvedUrl(".") in its Root. A
    // view's name is found there; Qt.resolvedUrl() here would find it in
    // Components.
    property url viewsUrl

    // A layer of the Root's own over the view (the Arcade's game or page,
    // over its list): the view is kept, but not drawn, and not in the frame.
    property bool viewCovered: false

    // In the layout's frame (Main.qml) while the view shown takes its place
    // in it: a browser or a list does, a player does not.
    readonly property bool inFrame: !viewCovered && !!internalLoader.item && internalLoader.item.inFrame === true

    // The view shown, for the Root to give the keys back to.
    readonly property Item currentView: internalLoader.item

    // A Root that opens some of its views its own way, not on the stack,
    // gives a function here: a view's navigateTo comes to it first, and it
    // answers true for one it opened (the Arcade's games and pages, over its
    // list).
    property var opensOwn: null

    // The way back: each view left for another, with its params and where
    // its list was (listState), the latest last.
    property var navStack: []
    property var currentParams: ({})

    // A view of the module's, by its file name.
    function viewUrl(viewPath) {
        return String(viewsUrl) + viewPath
    }

    // The view over the one shown, which back returns to.
    function navigateToView(viewPath, params, fromState) {
        navStack.push({ source: internalLoader.source, params: currentParams, listState: fromState || {} })
        currentParams = params || {}
        root.changeWindow(internalLoader, viewUrl(viewPath), { "navParams": params || {} })
    }

    // The view in place of every one before it: back from it leaves the
    // module. Signed in or out, back never returns to a view signed out of,
    // or to the sign-in page; and a card's player replaces the view that
    // looked its video up, so back from it returns to the card reader.
    function replaceWith(viewPath, params) {
        currentParams = params || {}
        navStack = []
        root.changeWindow(internalLoader, viewUrl(viewPath), { "navParams": params || {} })
    }

    // The view in place of the one shown, the stack as it was: back from it
    // skips the view it replaced, to whatever opened that one (Boxset.qml
    // with a single content type handing off to its Items.qml).
    function replaceCurrent(viewPath, params) {
        currentParams = params || {}
        root.changeWindow(internalLoader, viewUrl(viewPath), { "navParams": params || {} })
    }

    // Repoint the BACK target after autoplay advances in place. The top of the
    // stack is the detail view the player was launched from; swap its item so
    // exiting the player returns to the now-playing episode's detail screen.
    function updateBackItem(item) {
        if (navStack.length === 0) return
        var top = navStack[navStack.length - 1]
        top.params = Object.assign({}, top.params, { item: item })
    }

    function navigateBack() {
        if (navStack.length === 0) {
            moduleRouter.goBack()
            return
        }
        var prev = navStack.pop()
        if (!prev.source || prev.source.toString() === "") {
            moduleRouter.goBack()
            return
        }
        var restored = Object.assign({}, prev.params)
        restored.navListState = prev.listState || {}
        currentParams = restored
        root.changeWindow(internalLoader, prev.source, { "navParams": restored })
    }

    // The module's settings page over the module, on the app's own stack
    // (the shell's navigateTo), as Settings opens it, the cursor on the row
    // of this key: a page that names a setting takes you to it. Back comes
    // back to the module, which starts again with the setting as it is now.
    // The row is found by its place among the manifest's rows, which the
    // page shows one for one; focusKey names it too.
    function openSettings(key) {
        var schema = appCore ? (appCore.get_module_settings_schema(moduleId) || []) : []
        var at = 0
        for (var i = 0; i < schema.length; i++) {
            if (schema[i].key === key) {
                at = i
                break
            }
        }
        navigateTo("views/ModuleSettings.qml", { moduleId: moduleId, alsoModules: [], focusKey: key || "",
                                                 navListState: { currentIndex: at } }, {})
    }

    // Leaves the module at once, whatever views are on the stack: a
    // player's STOP, TO MAIN MENU, to the main menu.
    function leaveModule() {
        moduleRouter.goBack()
    }

    Loader {
        id: internalLoader
        anchors.fill: parent
        focus: true
        // Under a layer of the Root's, kept but not drawn: the views draw no
        // ground of their own, so it would show through.
        visible: !moduleRouter.viewCovered
        onLoaded: { if (item) item.forceActiveFocus() }

        Connections {
            target: internalLoader.item
            ignoreUnknownSignals: true
            // Not while the old window is being caught (root.windowCatching):
            // the view asking is the one leaving, and a second select would
            // put it on the way back twice.
            function onNavigateTo(path, params, listState) {
                if (root.windowCatching)
                    return
                if (!moduleRouter.opensOwn || !moduleRouter.opensOwn(path, params, listState))
                    moduleRouter.navigateToView(path, params, listState)
            }
            function onReplaceWith(path, params) { moduleRouter.replaceWith(path, params) }
            function onReplaceCurrent(path, params) { moduleRouter.replaceCurrent(path, params) }
            function onGoBack() { if (!root.windowCatching) moduleRouter.navigateBack() }
            function onUpdateBackItem(item) { moduleRouter.updateBackItem(item) }
        }
    }
}
