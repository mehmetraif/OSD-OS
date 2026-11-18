import QtQuick

// A view of the test's module: what its router hands it (navParams), what
// it reads of the Root (moduleRoot, by the Root's id), and the signals a
// module's views ask their router with.
FocusScope {
    property var navParams: ({})
    readonly property string name: navParams.name || ""
    readonly property bool inFrame: navParams.inFrame === true

    readonly property string seenModuleId: moduleRoot.moduleId
    readonly property string seenModuleName: moduleRoot.moduleName
    readonly property int seenCredits: moduleRoot.credits

    signal navigateTo(string path, var params, var listState)
    signal replaceWith(string path, var params)
    signal replaceCurrent(string path, var params)
    signal updateBackItem(var item)
    signal goBack()
}
