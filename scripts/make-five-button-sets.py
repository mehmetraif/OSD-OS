#!/usr/bin/env python3
"""The Arcade module's list of the games of five buttons or more
(modules/arcade/five-button-sets.txt), made here from MAME's own list.

A pad on Game Buttons' Auto layout plays a game of up to four buttons on
Neo Geo 4's inputs and one of five or more on Fighting 6's: this list is
how the Arcade module tells the two apart. It holds every machine's set,
parent or clone, whose first player has a joystick (MAME's control types "joy",
"doublejoy" and "stick") with five buttons or more, as MAME's -listxml
describes the machine; one name a line, sorted, after a comment line that
names the MAME version it was made from. The data is MAME's (its -listxml
output, facts about each machine): run it again with a newer MAME's list.

    mame -listxml > listxml.xml
    python3 -I scripts/make-five-button-sets.py listxml.xml

It needs Python 3 alone, reads the list as it goes (MAME's is some 280 MB)
and writes the file in place.
"""
import os
import sys
import xml.etree.ElementTree as ET

MINIMUM = 5
JOYSTICKS = {"joy", "doublejoy", "stick"}
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "modules", "arcade", "five-button-sets.txt")


def buttons(machine):
    """The most buttons player 1's joystick has, 0 for none."""
    most = 0
    for control in machine.iterfind("input/control"):
        if control.get("type") not in JOYSTICKS or control.get("player", "1") != "1":
            continue
        try:
            most = max(most, int(control.get("buttons", "0")))
        except ValueError:
            pass
    return most


def main(path):
    version = ""
    names = set()
    for event, element in ET.iterparse(path, events=("start", "end")):
        if event == "start":
            if element.tag == "mame":
                version = element.get("build", "").split(" ")[0]
            continue
        if element.tag != "machine":
            continue
        # A machine to play, not a part of one (a console's pad is a device).
        if element.get("isdevice") != "yes" and buttons(element) >= MINIMUM:
            names.add(element.get("name"))
        element.clear()
    if not version or not names:
        sys.exit("make-five-button-sets: no MAME list in " + path)
    with open(OUT, "w", encoding="utf-8", newline="\n") as out:
        out.write("# MAME %s: the sets whose player 1 has a joystick and %d buttons or more "
                  "(scripts/make-five-button-sets.py)\n" % (version, MINIMUM))
        for name in sorted(names):
            out.write(name + "\n")
    print("%d sets, from MAME %s" % (len(names), version))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: make-five-button-sets.py <mame -listxml output>")
    main(sys.argv[1])
