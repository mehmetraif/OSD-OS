import QtQuick
import QtTest
import Components
import OSDOS.Testing

// The two catalogue browsers, YouTube's (modules/youtube/views/Items.qml)
// and a web player module's (modules/netflix/views/Browse.qml, a
// WebPlayerBrowse), against stand-ins for their backends: the rows they
// list, what select does with each kind of row (a video or a title plays, a
// search opens the keyboard and its words' folder, MORE loads more of its
// folder, the service's home page opens), right on what plays (its info, or
// its options with INFO SCREEN off), the info filled as its details come,
// the hint bar and the problem line, back, a folder listed again, and the
// startup favourite played at once.
StandInRoot {
    id: root
    function dismissScreenSaver() {}

    property var navigations: []
    property int backs: 0

    QtObject {
        id: moduleRoot
        property string moduleId: "com.osdos.youtube"
        property string moduleName: "YouTube"
        property string moduleIcon: ""
        function leaveModule() {}
    }

    QtObject {
        id: appCore
        property var settings: ({})
        property var lists: ({})
        function get_setting(moduleId, key) { return settings[key] }
        function save_setting(moduleId, key, value) {}
        function is_module_enabled(id) { return false }
        function get_list(moduleId, list) { return lists[list] || [] }
        function list_contains(moduleId, list, path) {
            return (lists[list] || []).some(function(e) { return e.path === path })
        }
        function add_to_list(moduleId, list, entry, max) {
            var l = lists
            l[list] = (l[list] || []).concat([entry])
            lists = l
        }
        function remove_from_list(moduleId, list, path) {}
    }

    // What the views ask of the backends, in order.
    property var asked: []
    function ask(what) { asked = asked.concat([what]) }

    function folder(name, path) { return { name: name, path: path, isFolder: true } }
    function action(name, kind, path) { return { name: name, path: path, isFolder: false, kind: kind } }

    QtObject {
        id: youtubeBackend
        property string problem: ""
        property var favorites: []
        signal listingReady(string path)
        signal detailsReady(string path, var details)
        function entries(path, preview) {
            root.ask("entries " + path)
            if (path === "favorites")
                return favorites
            if (path === "home")
                return [root.folder("Recently Watched", "history"), root.folder("Favorites", "favorites"),
                        root.action("Search", "search", "search"),
                        { name: "A Talk", path: "video/a1", isFolder: false, kind: "video", videoId: "a1",
                          url: "https://www.youtube.com/watch?v=a1", title: "A Talk", channelName: "Chan",
                          description: "long", isShort: false },
                        root.action("More…", "more", "search/cats#more"),
                        root.folder("Subscriptions", "subscriptions")]
            if (path === "subscriptions")
                return undefined
            return []
        }
        function loadMore(path) { root.ask("loadMore " + path) }
        function forgetSearches() { root.ask("forgetSearches") }
        function loadDetails(item) { root.ask("loadDetails " + item.path) }
        function isInWatchLater(videoId) { return false }
        function addToWatchLater(videoId, title, channelName) { root.ask("watchLater " + videoId) }
        function removeFromWatchLater(videoId) {}
    }

    QtObject {
        id: catalog
        property string problem: ""
        signal listingReady(string path)
        signal detailsReady(string path, var details)
        function listing(path, preview) {
            root.ask("listing " + path)
            if (path === "home")
                return [root.action("Search", "search", "search"),
                        { name: "A Film", path: "title/movie/7", isFolder: false, kind: "title" },
                        root.action("More…", "more", "search/cats#more"),
                        root.action("Netflix", "home", "home-page"),
                        root.folder("Movies", "movies")]
            if (path === "movies")
                return undefined
            return []
        }
        function loadMore(path) { root.ask("loadMore " + path) }
        function loadDetails(item) { root.ask("loadDetails " + item.path) }
    }
    QtObject {
        id: netflixBackend
        property var catalog: catalog
    }

    Loader {
        id: loader
        anchors.fill: parent
        focus: true
    }
    Connections {
        target: loader.item
        ignoreUnknownSignals: true
        function onGoBack() { root.backs++ }
        function onNavigateTo(path, params, listState) {
            root.navigations = root.navigations.concat([{ path: path, params: params, listState: listState }])
        }
    }

    TestCase {
        name: "CatalogBrowsers"
        when: windowShown

        function init() {
            root.navigations = []
            root.backs = 0
            root.asked = []
            appCore.settings = { info_screen: "key" }
            appCore.lists = {}
            youtubeBackend.problem = ""
            youtubeBackend.favorites = []
            catalog.problem = ""
        }
        function cleanup() { loader.source = "" }

        function open(module, params) {
            moduleRoot.moduleId = "com.osdos." + module
            moduleRoot.moduleName = module === "youtube" ? "YouTube" : "Netflix"
            loader.setSource(Qt.resolvedUrl(module === "youtube" ? "../../modules/youtube/views/Items.qml"
                                                                 : "../../modules/netflix/views/Browse.qml"),
                             { navParams: params || {} })
            verify(loader.item !== null)
            loader.item.forceActiveFocus()
            waitForRendering(loader.item)
            return loader.item
        }

        // The first item under the view that answers to test, depth first.
        function find(item, test) {
            for (var i = 0; i < item.children.length; i++) {
                var child = item.children[i]
                if (test(child))
                    return child
                var deeper = find(child, test)
                if (deeper)
                    return deeper
            }
            return null
        }
        function treeOf(view) { return find(view, function(c) { return c.rootPath === "home" && c.trailState !== undefined }) }
        function keyboardOf(view) { return find(view, function(c) { return c.accepted !== undefined && c.title !== undefined && String(c.title).indexOf("Search") === 0 }) }
        function infoOf(view) { return find(view, function(c) { return c.playRequested !== undefined && c.moveRequested !== undefined }) }
        function optionsOf(view) { return find(view, function(c) { return c.favoritesEdited !== undefined }) }
        function hintOf(view) { return find(view, function(c) { return c.visible && typeof c.text === "string" && c.text.indexOf("[ESC]:BACK") === 0 }) }
        function problemOf(view) { return find(view, function(c) { return c.always !== undefined && c.shown !== undefined }) }

        function cursorTo(tree, name) {
            for (var i = 0; i < 12 && (!tree.currentEntry || tree.currentEntry.name !== name); i++)
                tree.move(1)
            compare(tree.currentEntry.name, name)
        }

        function test_views_data() {
            return [{ tag: "youtube", module: "youtube", backend: youtubeBackend, play: "A Talk", playPath: "video/a1",
                      opens: "Player.qml", listAsk: "entries ", search: "Search YouTube", folder: "Subscriptions",
                      folderPath: "subscriptions",
                      rows: ["Recently Watched", "Favorites", "Search", "A Talk", "More…", "Subscriptions"] },
                    { tag: "netflix", module: "netflix", backend: catalog, play: "A Film", playPath: "title/movie/7",
                      opens: "Launch.qml", listAsk: "listing ", search: "Search Netflix", folder: "Movies",
                      folderPath: "movies",
                      rows: ["Recently Watched", "Favorites", "Search", "A Film", "More…", "Netflix", "Movies"] }]
        }

        function test_views(data) {
            var view = open(data.module)
            var tree = treeOf(view)
            verify(tree)
            compare(tree.currentEntry.name, data.rows[0])
            var names = []
            for (var i = 0; i < data.rows.length; i++) {
                names.push(tree.currentEntry.name)
                tree.move(1)
            }
            compare(names, data.rows)
            verify(root.asked.indexOf(data.listAsk + "home") >= 0)

            var hint = hintOf(view)
            verify(hint)
            cursorTo(tree, "Search")
            compare(hint.text, "[ESC]:BACK [▲▼◄►]:NAVIGATE [ENTER]:SEARCH")
            cursorTo(tree, "More…")
            compare(hint.text, "[ESC]:BACK [▲▼◄►]:NAVIGATE [ENTER]:MORE")
            cursorTo(tree, data.folder)
            compare(hint.text, "[ESC]:BACK [▲▼◄►]:NAVIGATE [ENTER]:OPEN")
            cursorTo(tree, data.play)
            compare(hint.text, "[ESC]:BACK [▲▼◄]:NAVIGATE [►]:INFO [ENTER]:PLAY")

            // MORE: more of its folder, "#more" taken off.
            cursorTo(tree, "More…")
            keyClick(Qt.Key_Return)
            verify(root.asked.indexOf("loadMore search/cats") >= 0)

            // SEARCH: the keyboard, then the words' folder.
            cursorTo(tree, "Search")
            keyClick(Qt.Key_Return)
            var osk = keyboardOf(view)
            verify(osk)
            verify(osk.visible)
            compare(osk.title, data.search)
            verify(!hint.visible)
            // As its OK does: closed, then the words.
            osk.close()
            osk.accepted("cats")
            verify(root.asked.indexOf(data.listAsk + "search/cats") >= 0)
            // YouTube asks for its words afresh, before their folder opens.
            var forgot = root.asked.indexOf("forgetSearches")
            if (data.module === "youtube")
                verify(forgot >= 0 && forgot < root.asked.indexOf(data.listAsk + "search/cats"))
            else
                compare(forgot, -1)
            compare(tree.folderName, "Search: cats")
            keyClick(Qt.Key_Escape)
            compare(tree.trailState().length, 1)

            // A folder on its way, listed again as its listing lands.
            cursorTo(tree, data.folder)
            keyClick(Qt.Key_Return)
            var before = root.asked.filter(function(a) { return a === data.listAsk + data.folderPath }).length
            verify(before >= 1)
            data.backend.listingReady(data.folderPath)
            compare(root.asked.filter(function(a) { return a === data.listAsk + data.folderPath }).length, before + 1)
            keyClick(Qt.Key_Escape)

            // Right on what plays: its info, filled as its details come.
            cursorTo(tree, data.play)
            keyClick(Qt.Key_Right)
            var info = infoOf(view)
            verify(info.visible)
            compare(info.item.path, data.playPath)
            verify(info.loading)
            verify(root.asked.indexOf("loadDetails " + data.playPath) >= 0)
            verify(!hint.visible)
            data.backend.detailsReady("elsewhere", { summary: "no" })
            verify(info.loading)
            if (data.module === "youtube") {
                data.backend.detailsReady(data.playPath, { summary: "some", complete: false })
                compare(info.details.summary, "some")
                verify(info.loading)
                data.backend.detailsReady(data.playPath, { summary: "all", complete: true })
            } else {
                data.backend.detailsReady(data.playPath, { summary: "all" })
            }
            compare(info.details.summary, "all")
            verify(!info.loading)
            // Its options from there, and back to the info.
            info.optionsRequested(info.item)
            var options = optionsOf(view)
            verify(options.visible)
            compare(options.entry.path, data.playPath)
            options.close()
            verify(info.activeFocus)
            // Play from the info.
            info.playRequested(info.item)
            compare(root.navigations.length, 1)
            compare(root.navigations[0].path, data.opens)
            compare(root.navigations[0].params.item.path, data.playPath)
            info.close()
            verify(tree.activeFocus)

            // Select on it: it plays, coming back to the tree as it is.
            keyClick(Qt.Key_Return)
            compare(root.navigations.length, 2)
            var played = root.navigations[1]
            compare(played.path, data.opens)
            compare(played.params.item.path, data.playPath)
            compare(played.listState.trail.length, 1)
            compare(played.listState.trail[0].path, "home")
            compare(played.listState.trail[0].sel, 3)
            if (data.module === "netflix") {
                compare(played.params.name, "A Film")
                compare(appCore.lists.recent.length, 2)
                compare(appCore.lists.recent[1].path, data.playPath)
                // The service's home page.
                cursorTo(tree, "Netflix")
                keyClick(Qt.Key_Return)
                compare(root.navigations.length, 3)
                compare(root.navigations[2].path, "Launch.qml")
                compare(root.navigations[2].params, { name: "Netflix" })
            }

            // With INFO SCREEN off, right on it offers its options.
            appCore.settings = { info_screen: "off" }
            view = open(data.module)
            tree = treeOf(view)
            hint = hintOf(view)
            cursorTo(tree, data.play)
            compare(hint.text, "[ESC]:BACK [▲▼◄]:NAVIGATE [►]:OPTIONS [ENTER]:PLAY")
            keyClick(Qt.Key_Right)
            options = optionsOf(view)
            verify(options.visible)
            verify(!infoOf(view).visible)
            compare(options.entry.path, data.playPath)
            if (data.module === "youtube") {
                // As FAVORITES keeps it: without the list's description.
                compare(options.entry.description, undefined)
                compare(options.entry.channelName, "Chan")
                compare(options.moreChoices[0].label, "Save to Watch Later")
                options.activated("watchlater")
                verify(root.asked.indexOf("watchLater a1") >= 0)
            } else {
                compare(options.moreChoices, [])
            }
            options.close()
            verify(tree.activeFocus)

            // Back leaves.
            keyClick(Qt.Key_Escape)
            compare(root.backs, 1)
        }

        // What stands in the way of browsing, on the problem line; a web
        // player's even with Help Line off.
        function test_problem_data() {
            return [{ tag: "youtube", module: "youtube", backend: youtubeBackend, always: false },
                    { tag: "netflix", module: "netflix", backend: catalog, always: true }]
        }
        function test_problem(data) {
            data.backend.problem = "No network"
            var view = open(data.module)
            var line = problemOf(view)
            verify(line)
            compare(line.text, "No network")
            verify(line.visible)
            compare(line.always, data.always)
            data.backend.problem = ""
            verify(!line.visible)
        }

        // The startup favourite plays at once, as if chosen in FAVORITES.
        function test_startup_data() {
            return [{ tag: "youtube", module: "youtube", opens: "Player.qml" },
                    { tag: "netflix", module: "netflix", opens: "Launch.qml" }]
        }
        function test_startup(data) {
            var favourite = { name: "Fav", path: "fav/1", isFolder: false, kind: data.module === "youtube" ? "video" : "title" }
            youtubeBackend.favorites = [{ name: "Other", path: "fav/0" }, favourite]
            appCore.lists = { favorites: [{ name: "Other", path: "fav/0" }, favourite] }
            open(data.module, { startupPlay: { path: "fav/1" } })
            tryCompare(root.navigations, "length", 1)
            var played = root.navigations[0]
            compare(played.path, data.opens)
            compare(played.params.item.path, "fav/1")
            compare(played.listState.trail[1].path, "favorites")
            compare(played.listState.trail[1].sel, 1)
            if (data.module === "youtube")
                verify(played.params.startup)
            else
                compare(played.params.name, "Fav")
        }
    }
}
