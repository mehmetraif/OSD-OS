import QtQuick
import Components

// Settings → Arcade → Online Name: the name the games hosted and joined online
// show the others (RetroArch's netplay nickname), typed on the on-screen
// keyboard; "" left as it was. Unset, it is the RetroAchievements account's,
// else OSD-OS.
FocusScope {
    id: nameRoot

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var navParams: ({})

    focus: true

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: "Online Name"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    OnScreenKeyboard {
        id: keyboard
        anchors.fill: parent
        title: "Online Name"
        caseSensitive: true
        maxLength: 32
        onAccepted: function(text) {
            appCore.save_setting(moduleRoot.moduleId, "online_name", text.trim())
            nameRoot.goBack()
        }
        onCanceled: nameRoot.goBack()
    }

    Component.onCompleted: keyboard.open(appCore.get_setting(moduleRoot.moduleId, "online_name") || "")
}
