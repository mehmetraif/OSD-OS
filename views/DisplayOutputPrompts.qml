import QtQuick
import Components

// Settings → Screen → Display Output's questions, over the page: the outputs
// this board has (open(), the cursor on the one in force), then how to
// switch to the one chosen, or, on the one in force with another written
// for the next start, whether to stay; and why At Next Start didn't happen.
// What they say is displayOutput's: the outputs, the one in force, the one
// written for the next start and the ways to switch the launcher knows.
// closed() as each goes, for the page to take the keys back.
Item {
    id: prompts

    signal closed()

    readonly property bool available: typeof displayOutput !== "undefined" && !!displayOutput && displayOutput.available
    readonly property string boardModel: available ? displayOutput.boardModel : ""
    readonly property var outputOptions: available ? displayOutput.options : []
    readonly property string outputCurrent: available ? displayOutput.current : ""
    readonly property string outputCurrentLabel: available ? displayOutput.currentLabel : ""
    readonly property string outputNext: available ? displayOutput.next : ""
    readonly property string outputNextLabel: available ? displayOutput.nextLabel : ""
    readonly property bool outputAtNextStart: available && displayOutput.nextStartAvailable
    readonly property bool outputPowerOff: available && displayOutput.powerOffAvailable
    // The one chosen to switch to, and why it couldn't be written for the
    // next start.
    property var outputChoice: ({})
    property string outputFailure: ""

    // The keys to the one of them still open, the topmost first, as another
    // closes over it (why At Next Start didn't happen opens by itself, over
    // the outputs opened again meanwhile): false with none open.
    function takeKeys() {
        var open = [outputFailed, outputStay, outputConfirm, outputPrompt].filter(function(p) { return p.visible })
        if (open.length > 0)
            open[0].forceActiveFocus()
        return open.length > 0
    }

    // The outputs, the cursor on the one in force.
    function open() {
        outputPrompt.open()
        for (var i = 0; i < outputOptions.length; i++) {
            if (outputOptions[i].id === outputCurrent)
                outputPrompt.choiceIndex = i
        }
    }

    // --- DISPLAY OUTPUT: the outputs this board has, then how to switch ---
    ChoiceOverlay {
        id: outputPrompt
        anchors.fill: parent
        promptText: "Display Output"
        subtitleText: prompts.boardModel + "\nNow: " + prompts.outputCurrentLabel
                      + (prompts.outputNext !== "" ? "\nNext start: " + prompts.outputNextLabel : "")
        choices: prompts.outputOptions.map(function(o) { return { label: o.label, action: o.id } })
        onActivated: function(id) {
            for (var i = 0; i < prompts.outputOptions.length; i++) {
                if (prompts.outputOptions[i].id === id)
                    prompts.outputChoice = prompts.outputOptions[i]
            }
            // The one in force: nothing to do, or what waits for the next
            // start taken back.
            if (id === prompts.outputCurrent) {
                if (prompts.outputNext !== "")
                    outputStay.open()
                return
            }
            outputConfirm.open()
        }
        onClosed: prompts.closed()
    }
    // The line for the output under the cursor (display-presets.json's
    // help): what it is for, its lines and rate, what its name leaves out.
    // In the chooser's own window (the PromptScreen it draws), over its
    // ground. A question has no help line in a monitor layout's frame,
    // whose own is under the question's window: this one draws itself
    // where the frame has its own, as the frame's would be drawn.
    readonly property var outputUnder: outputOptions[outputPrompt.choiceIndex] || ({})
    function promptWindow(overlay) {
        for (var i = 0; i < overlay.children.length; i++) {
            if (overlay.children[i].item)
                return overlay.children[i].item
        }
        return null
    }
    HelpLine {
        readonly property var frame: root.monitorInk === true && root.chrome ? root.chrome : null
        objectName: "outputHelp"
        parent: prompts.promptWindow(outputPrompt) || prompts
        z: 1
        visible: outputPrompt.visible && text !== ""
        text: prompts.outputUnder.help || ""
        ofFrame: frame !== null
        x: frame ? frame.helpX : root.sw * 0.125 //80
        y: frame ? frame.helpY : parent ? parent.height - height - root.helpLineMargin : 0
        width: frame ? frame.helpW : root.sw * 0.75 //480
        ink: frame ? (root.studioLayout || root.flatLayout ? frame.pal.soft : frame.pal.ink) : root.primaryColor
        framed: frame ? root.studioLayout : true
    }

    // Now, with a restart; written now for the next start, the Pi left on;
    // or written, then off: on the new output the next time it is switched
    // on, as when it goes into a cabinet whose screen this one isn't.
    ChoiceOverlay {
        id: outputConfirm
        anchors.fill: parent
        promptText: "Switch to " + (prompts.outputChoice.label || "") + "?"
        // A scan rate only some monitors take says so first.
        subtitleText: (prompts.outputChoice.warning ? prompts.outputChoice.warning + "\n" : "")
                      + (prompts.outputAtNextStart || prompts.outputPowerOff
                         ? "Now, or from the next time the Pi starts. " : "OSD/OS restarts on it. ")
                      + "Keep it there, or in 30 seconds it comes back to " + prompts.outputCurrentLabel
        choices: [{ label: "Switch and Restart", action: "switch" }]
                 .concat(prompts.outputAtNextStart && prompts.outputChoice.id !== prompts.outputNext
                         ? [{ label: "At Next Start", action: "next" }] : [])
                 .concat(prompts.outputPowerOff ? [{ label: "Switch and Power Off", action: "poweroff" }] : [])
                 .concat([{ label: "Cancel", action: "cancel" }])
        onActivated: function(act) {
            if (act === "switch")
                displayOutput.apply(prompts.outputChoice.id)
            else if (act === "next")
                displayOutput.applyAtNextStart(prompts.outputChoice.id)
            else if (act === "poweroff")
                displayOutput.applyAndPowerOff(prompts.outputChoice.id)
        }
        onClosed: prompts.closed()
    }

    ChoiceOverlay {
        id: outputStay
        anchors.fill: parent
        promptText: "Stay on " + prompts.outputCurrentLabel + "?"
        subtitleText: prompts.outputNextLabel + " was to come at the next start"
        // The title names the output: a long one wouldn't fit an answer.
        choices: [{ label: "Stay", action: "stay" },
                  { label: "Cancel", action: "cancel" }]
        onActivated: function(act) {
            if (act === "stay")
                displayOutput.applyAtNextStart(prompts.outputCurrent)
        }
        onClosed: prompts.closed()
    }

    // At Next Start's answer, where it didn't happen: why.
    Connections {
        target: prompts.available ? displayOutput : null
        function onNextStartFinished(ok, reason) {
            if (ok)
                return
            prompts.outputFailure = reason
            outputFailed.open()
        }
    }
    ChoiceOverlay {
        id: outputFailed
        anchors.fill: parent
        promptKind: "notice"
        promptText: "Not written for the next start"
        subtitleText: (prompts.outputChoice.label || "") + " couldn't be written: " + prompts.outputFailure
        choices: []
        onClosed: prompts.closed()
    }
}
