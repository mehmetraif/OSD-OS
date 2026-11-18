import QtQuick
import Components

ModuleRouter {
    id: moduleRoot
    moduleId: "com.osdos.weather"
    viewsUrl: Qt.resolvedUrl(".")

    // The backend answers start() with either data or a location error, so the
    // display view is always the entry point and swaps itself out on failure.
    Component.onCompleted: replaceWith("Weather.qml", {})

    // Leaving the module stops the refresh timer — no point polling Open-Meteo
    // for a screen nobody is looking at — and silences the music. Both belong
    // here rather than in Weather.qml so audio survives a swap between screens
    // and stops exactly when the module exits.
    Component.onDestruction: {
        if (weatherBackend) {
            weatherBackend.stop()
            weatherBackend.stopMusic()
        }
    }
}
