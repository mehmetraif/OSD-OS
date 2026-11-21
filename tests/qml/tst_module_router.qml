import QtQuick
import QtTest
import OSDOS.Testing

// A module's router (views/Components/ModuleRouter.qml), as a module's
// Root.qml is one (module_router/Root.qml, with its views beside it),
// against a stand-in for Main.qml: the first view found in the module's own
// folder and reading the Root by its id; forward and back, with where a
// list was; replaceWith clearing the way back and replaceCurrent keeping
// it; the view back returns to given another item (autoplay); a view the
// Root opens its own way (the Arcade's games); the view's inFrame handed
// on, unless a layer of the Root's covers it; leaving the module at once;
// and the shell's own navigateTo, heard only when it is asked for.
StandInRoot {
    id: root

    // AppCore: the module's name and logo.
    QtObject {
        id: appCore
        function get_module_info(id) {
            return id === "com.osdos.router_test" ? { name: "Router Test", icon: "logo.svg" } : {}
        }
        function get_module_settings_schema(id) {
            return id === "com.osdos.router_test"
                ? [{ key: "enabled", type: "toggle" }, { key: "folder", type: "text" }, { key: "location", type: "text" }]
                : []
        }
    }

    Loader {
        id: loader
        anchors.fill: parent
        focus: true
    }

    // What the module asked of the shell, which listens for both on the
    // module by these names (Main.qml): another module's view, and leaving.
    property var shellNavigations: []
    property var shellParams: []
    property int backs: 0
    Connections {
        target: loader.item
        ignoreUnknownSignals: true
        function onNavigateTo(path, params, listState) {
            root.shellNavigations = root.shellNavigations.concat([path])
            root.shellParams = root.shellParams.concat([params])
        }
        function onGoBack() { root.backs++ }
    }

    TestCase {
        name: "ModuleRouter"
        when: windowShown

        function init() {
            root.shellNavigations = []
            root.shellParams = []
            root.backs = 0
            loader.source = Qt.resolvedUrl("module_router/Root.qml")
            verify(loader.item !== null)
        }

        function cleanup() {
            loader.source = ""
        }

        function router() { return loader.item }
        function view() { return router().currentView }
        function shown() { return view() ? view().name : "" }

        // A page that names a setting opens the module's settings page over
        // the module, on the shell's stack, the cursor on that row.
        function test_openSettings() {
            router().openSettings("location")
            compare(root.shellNavigations, ["views/ModuleSettings.qml"])
            compare(root.shellParams[0].moduleId, "com.osdos.router_test")
            compare(root.shellParams[0].focusKey, "location")
            // The page finds the row by its key, not by a place the router
            // works out.
            compare(root.shellParams[0].navListState, undefined)
        }

        // The first view, from the module's own folder (viewsUrl), not from
        // Components where the router is; it reads the Root by the Root's
        // id, and the module's name and logo are AppCore's.
        function test_firstView() {
            compare(shown(), "first")
            var source = String(router().children[0].source)
            verify(/\/tests\/qml\/module_router\/Page\.qml$/.test(source), source)
            compare(view().seenModuleId, "com.osdos.router_test")
            compare(view().seenModuleName, "Router Test")
            compare(router().moduleName, "Router Test")
            compare(router().moduleIcon, "logo.svg")
            verify(view().activeFocus)
        }

        // A view asked for goes over the one shown; back returns to that one
        // with its params and where its list was (navListState), and back
        // from the first view leaves the module.
        function test_forwardAndBack() {
            view().navigateTo("Page.qml", { name: "second" }, { currentIndex: 3 })
            compare(shown(), "second")
            view().goBack()
            compare(shown(), "first")
            compare(view().navParams.navListState, { currentIndex: 3 })
            verify(view().inFrame)
            compare(root.backs, 0)
            view().goBack()
            compare(root.backs, 1)
            compare(root.shellNavigations, [])
        }

        // replaceWith: in place of every view before it, so back from it
        // leaves the module (signed in or out), the view's or the Root's own.
        function test_replaceWithClearsTheWayBack() {
            view().navigateTo("Page.qml", { name: "second" }, {})
            view().replaceWith("Page.qml", { name: "signed in" })
            compare(shown(), "signed in")
            compare(router().navStack.length, 0)
            view().goBack()
            compare(root.backs, 1)
            view().navigateTo("Page.qml", { name: "second" }, {})
            router().replaceWith("Page.qml", { name: "signed out" })
            compare(shown(), "signed out")
            view().goBack()
            compare(root.backs, 2)
        }

        // replaceCurrent: in place of the one shown, the way back as it was:
        // back skips it, to what opened the view it replaced.
        function test_replaceCurrentKeepsTheWayBack() {
            view().navigateTo("Page.qml", { name: "second" }, { currentIndex: 1 })
            view().replaceCurrent("Page.qml", { name: "third" })
            compare(shown(), "third")
            view().goBack()
            compare(shown(), "first")
            compare(view().navParams.navListState, { currentIndex: 1 })
            compare(root.backs, 0)
        }

        // Autoplay moved on in place: back returns to the view the player was
        // opened from, with the item it moved on to.
        function test_updateBackItem() {
            view().navigateTo("Page.qml", { name: "player" }, {})
            view().updateBackItem("episode 2")
            view().goBack()
            compare(shown(), "first")
            compare(view().navParams.item, "episode 2")
            verify(view().inFrame)
        }

        // A view the Root opens its own way: not on the stack, and the view
        // shown stays.
        function test_opensOwn() {
            var first = view()
            first.navigateTo("Own.qml", { name: "game" }, {})
            compare(router().ownOpened, ["game"])
            compare(view(), first)
            view().goBack()
            compare(root.backs, 1)
        }

        // The view's inFrame, handed on: a view out of the frame (a player)
        // takes the whole screen; a layer of the Root's over the view takes
        // it out of the frame, and the view isn't drawn under it.
        function test_inFrame() {
            verify(router().inFrame)
            router().viewCovered = true
            verify(!router().inFrame)
            verify(!router().children[0].visible)
            router().viewCovered = false
            verify(router().inFrame)
            verify(router().children[0].visible)
            view().navigateTo("Page.qml", { name: "player" }, {})
            verify(!router().inFrame)
        }

        // CLOSE VIDEO leaves the module at once, whatever is on the stack.
        function test_leaveModule() {
            view().navigateTo("Page.qml", { name: "second" }, {})
            view().navigateTo("Page.qml", { name: "player" }, {})
            router().leaveModule()
            compare(root.backs, 1)
        }

        // The shell's navigateTo, another module's view (an NFC card's
        // hand-off): the shell hears it, the module loads nothing. Nothing
        // else of the Root's says it, its own properties changing least of
        // all.
        function test_shellNavigateTo() {
            router().credits = 2
            router().ownOpened = ["x"]
            router().viewCovered = true
            router().viewCovered = false
            compare(view().seenCredits, 2)
            compare(root.shellNavigations, [])
            router().navigateTo("file:///modules/plex/views/Root.qml", { cardRef: "plex://movie/a" }, {})
            compare(root.shellNavigations, ["file:///modules/plex/views/Root.qml"])
            compare(shown(), "first")
            compare(root.backs, 0)
        }
    }
}
