import QtQuick

// A stand-in for the app's VhsNoise (src/player/VhsNoise.h, registered in
// main.cpp), for the views' tests: the loading screen's noise with nothing
// drawn.
Item {
    property color color
    property color shade
    property real pixel: 1
    property int streaks: 0
    property bool bands: false
    property bool running: false
}
