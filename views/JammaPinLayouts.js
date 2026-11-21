.pragma library

// Which of the JAMMA board's expander pins is which control, by layout, in
// the order a JAMMA stick's Pin Names goes round them (ControllerSettings.qml,
// which keeps the name in config.json as app.jamma_pin_layout) and its test
// draws the pins by (JammaPinGrid.qml): OSD/OS's own (what osdos-jamma's
// mapping makes of each pin, its mapping file included), the Crown board's
// (mk_arcade_joystick_rpi's order, button 6 on pin 27, a Chinese harness's),
// and JammaPi's. By the expander pin, the same on both players' expanders, or
// by the input the mapping makes of it (byInput).
var layouts = [
    { name: "OSD/OS", byInput: true },
    { name: "Crown, CHAMMA",
      pins: { A0: "up", A1: "down", A2: "left", A3: "right", A4: "start", A5: "coin",
              A6: "b1", A7: "b2", B0: "b3", B1: "b4", B2: "b5", B3: "b6" } },
    { name: "JammaPi",
      pins: { A0: "up", A1: "down", A2: "left", A3: "right", A5: "start", A6: "coin",
              B7: "b1", B6: "b2", B5: "b3", B4: "b4", B3: "b5", B2: "b6" } }
]
