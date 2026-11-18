import QtQuick
import Components

ModuleRouter {
    id: moduleRoot
    moduleId: "com.osdos.scripts"
    viewsUrl: Qt.resolvedUrl(".")

    Component.onCompleted: {
        // Entered from a main-menu favorite (AppCore passed the sidecar's basename
        // through as navParams.script): go straight to the runner, skipping the
        // list entirely.
        //
        // No auto-relaunch guard is needed. The runner's exit calls navigateBack(),
        // whose stack holds only the empty initial source, so it falls through to
        // goBack() and the user lands on the main menu — never back on a view that
        // would start the script again.
        if (navParams.script) {
            if (scriptsBackend.confirmFor(navParams.script)) {
                // A favorite must not bypass a gate the sidecar asked for. The
                // prompt already lives in Items.qml, so hand the script to it
                // rather than growing a second copy of that overlay.
                navigateToView("Items.qml", { confirmScript: navParams.script })
            } else {
                var mode = scriptsBackend.modeFor(navParams.script)
                // Translate to the runner views' own param names — they expect
                // "basename", and passing navParams straight through would hand them
                // an empty one.
                navigateToView(mode === "takeover" ? "Takeover.qml" : "Console.qml",
                               { basename: navParams.script, name: navParams.name })
            }
        } else if (navParams.fromAppStartup && startupScript !== "" && startupScript !== "None") {
            startupTimer.start()
        } else {
            navigateToView("Items.qml", navParams)
        }
    }

    // Auto-run on startup, when OSD/OS booted straight into this module.
    readonly property string startupScript:
        appCore ? (appCore.get_setting(moduleId, "startup_script") || "") : ""

    // 750 ms rather than Qt.callLater. On a headless Pi a takeover hand-off has to
    // save Qt's CRTC state, and at first load Qt may not have completed a frame —
    // capturing then can yield an invalid state, which means the display could not
    // be restored afterwards. DisplayHandoff refuses the hand-off in that case
    // (falling back to console mode), but it is better not to provoke it.
    Timer {
        id: startupTimer
        interval: 750
        repeat: false
        onTriggered: {
            var mode = scriptsBackend.modeFor(moduleRoot.startupScript)
            if (mode === "") {
                console.log("[Scripts] startup_script no longer exists: "
                            + moduleRoot.startupScript)
                navigateToView("Items.qml", moduleRoot.navParams)
                return
            }
            // Deliberately bypasses "confirm": nobody is there to answer a prompt
            // on a machine that just booted into this script on purpose.
            navigateToView(mode === "takeover" ? "Takeover.qml" : "Console.qml",
                           { basename: moduleRoot.startupScript,
                             name: scriptsBackend.nameFor(moduleRoot.startupScript) })
        }
    }
}
