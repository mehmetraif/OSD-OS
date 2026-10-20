import QtQuick
import Components

// A game's engine, from its options (Engine): Auto, which names the engine it
// picks, MAME, or FinalBurn Neo for a set it has (ArcadeBackend's
// engineChoices()), the game's own choice first. Saved for the game
// (setEngine()), engineChosen() after.
//     EngineChooser { id: engines; anchors.fill: parent; onClosed: list.forceActiveFocus() }
//     … engines.offer(set, title)
ChoiceOverlay {
    id: chooser

    property string set: ""

    signal engineChosen()

    function offer(game, title) {
        set = game
        subtitleText = title || game
        var list = arcadeBackend ? arcadeBackend.engineChoices(game) : []
        var made = []
        var current = 0
        for (var i = 0; i < list.length; i++) {
            if (list[i].current)
                current = i
            made.push({ label: list[i].label, action: list[i].id })
        }
        choices = made
        open()
        choiceIndex = current
    }
    // As the options' line names it: "Engine: Auto (MAME)".
    function lineFor(game) {
        var list = arcadeBackend ? arcadeBackend.engineChoices(game) : []
        for (var i = 0; i < list.length; i++) {
            if (list[i].current)
                return "Engine: " + list[i].label
        }
        return "Engine"
    }
    // Whether a game has an engine to choose: one FinalBurn Neo plays that
    // MAME has too.
    function offered(game) {
        return !!arcadeBackend && arcadeBackend.engineChoices(game).length > 2
    }

    promptText: "Engine"
    onActivated: function(action) {
        if (arcadeBackend)
            arcadeBackend.setEngine(set, action)
        engineChosen()
    }
}
