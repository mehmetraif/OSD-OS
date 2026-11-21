import QtQuick
import Components

// Something set for a game, from its options, chosen among its settings: a
// vertical game's turn (Vertical Games), a MAME game's Difficulty, the Engine that plays
// it (Auto, which names the engine it picks, MAME, or FinalBurn Neo for a
// set it has: ArcadeBackend's engineChoices()). The host says where the
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
    // How many settings a game needs for there to be a choice: the Engine's
    // Auto and MAME are there for every game, so it takes FinalBurn Neo too.
    property int least: 2
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
    // Whether the options' line leaves out what a setting's name says in
    // brackets ("Default (Horizontal)" as Default), for a long name that
    // would be cut there; the choices still say it.
    property bool shortLine: false
    // As the options' line names it, the one in force after the name.
    function lineFor(game) {
        var list = choicesFor(game)
        for (var i = 0; i < list.length; i++) {
            if (list[i].current)
                return name + ": " + (shortLine ? String(list[i].label).replace(/ \(.*\)$/, "") : list[i].label)
        }
        return name
    }
    // Whether a game has anything to choose.
    function offered(game) {
        return choicesFor(game).length >= least
    }

    promptText: name
    onActivated: function(action) {
        keep(set, action)
        chosen()
    }
}
