import QtQuick
import Components

// Something set for a game, from its options, chosen among its settings: a
// vertical game's Monitor, a MAME game's Difficulty. The host says where the
// settings come from (choicesFor(game): [{ id, label, current }], the
// backend's) and how one is kept (keep(game, id)); the one in force is
// under the cursor as it opens, chosen() after one is kept.
//     GameChoice {
//         id: difficulty
//         anchors.fill: parent
//         name: "Difficulty"
//         choicesFor: function(game) { return arcadeBackend.difficultyChoices(game) }
//         keep: function(game, id) { arcadeBackend.setDifficulty(game, id) }
//         onClosed: list.forceActiveFocus()
//     }
//     … difficulty.offer(set, title)
ChoiceOverlay {
    id: chooser

    // What is set: the title bar's, and the options' line's ("Difficulty:
    // Normal").
    property string name: ""
    property var choicesFor: function(game) { return [] }
    property var keep: function(game, id) {}
    property string set: ""

    signal chosen()

    function offer(game, title) {
        set = game
        subtitleText = title || game
        var list = choicesFor(game)
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
    // As the options' line names it, the one in force after the name.
    function lineFor(game) {
        var list = choicesFor(game)
        for (var i = 0; i < list.length; i++) {
            if (list[i].current)
                return name + ": " + list[i].label
        }
        return name
    }
    // Whether a game has anything to choose.
    function offered(game) {
        return choicesFor(game).length > 1
    }

    promptText: name
    onActivated: function(action) {
        keep(set, action)
        chosen()
    }
}
