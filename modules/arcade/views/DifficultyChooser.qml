import QtQuick
import Components

// A game's difficulty, from its options (Difficulty), where MAME plays it and
// its board sets the difficulty with a DIP switch: the switch's settings
// (ArcadeBackend's difficultyChoices()), the one in force first under the
// cursor. Saved for the game (setDifficulty()), which MAME starts it with,
// difficultyChosen() after.
//     DifficultyChooser { id: difficulty; anchors.fill: parent; onClosed: list.forceActiveFocus() }
//     … difficulty.offer(set, title)
ChoiceOverlay {
    id: chooser

    property string set: ""

    signal difficultyChosen()

    function offer(game, title) {
        set = game
        subtitleText = title || game
        var list = arcadeBackend ? arcadeBackend.difficultyChoices(game) : []
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
    // As the options' line names it: "Difficulty: Normal".
    function lineFor(game) {
        var list = arcadeBackend ? arcadeBackend.difficultyChoices(game) : []
        for (var i = 0; i < list.length; i++) {
            if (list[i].current)
                return "Difficulty: " + list[i].label
        }
        return "Difficulty"
    }
    // Whether a game has a difficulty to choose.
    function offered(game) {
        return !!arcadeBackend && arcadeBackend.difficultyChoices(game).length > 1
    }

    promptText: "Difficulty"
    onActivated: function(action) {
        if (arcadeBackend)
            arcadeBackend.setDifficulty(set, action)
        difficultyChosen()
    }
}
