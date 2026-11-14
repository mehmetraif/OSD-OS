import QtQuick

// One of the deck's on-screen symbols (▶ ◀◀ ‖ ■ ● ⏏ …), drawn from a small
// bitmap at a whole number of screen pixels per art pixel, so it stays as crisp
// as the rest of the OSD. Set cells take `color`; the rest stay clear.
//
// It is a picture OsdPixelProvider (src/util/OsdIconProvider) makes of the
// cells at this size, in this colour: one for every PixelIcon that shows the
// symbol so, kept by Qt Quick under its address, and one texture. Each used
// to be a Canvas of its own, painted by a script as it was made and again at
// every change of colour, a dozen and more of them on a settings page.
Image {
    id: icon

    property string name: "play"
    property color color: root.primaryColor
    property int pixel: root.px

    // "#" is a set cell. Symbols are seven cells tall where they sit in a line
    // of text, so they line up with each other.
    readonly property var shapes: ({
        "play":    ["#...", "##..", "###.", "####", "###.", "##..", "#..."],
        "left":    ["...#", "..##", ".###", "####", ".###", "..##", "...#"],
        "up":      ["...#...", "..###..", ".#####.", "#######"],
        "down":    ["#######", ".#####.", "..###..", "...#..."],
        "ff":      ["#...#...", "##..##..", "###.###.", "########", "###.###.", "##..##..", "#...#..."],
        "rew":     ["...#...#", "..##..##", ".###.###", "########", ".###.###", "..##..##", "...#...#"],
        "pause":   ["##.##", "##.##", "##.##", "##.##", "##.##", "##.##", "##.##"],
        "stop":    ["######", "######", "######", "######", "######", "######"],
        "rec":     ["..###..", ".#####.", "#######", "#######", "#######", ".#####.", "..###.."],
        "eject":   ["...#...", "..###..", ".#####.", "#######", ".......", "#######", "#######"],
        // The OK key and a cassette, each a solid badge with its marks cut out.
        "ok":      [".#########.", "##...#.#.##", "##.#.#.#.##", "##.#.#..###",
                    "##.#.#.#.##", "##...#.#.##", ".#########."],
        // ok's opposite: what isn't so (a lobby's room against yours).
        "no":      [".#########.", "###.###.###", "####.#.####", "#####.#####",
                    "####.#.####", "###.###.###", ".#########."],
        "tape":    [".#########.", "###########", "##..###..##", "##..###..##",
                    "##..###..##", "###########", ".#########."],
        // A favourite, as the Arcade screen marks one; and its Search's own
        // row, a glass.
        "heart":   [".##.##.", "#######", "#######", ".#####.", "..###..", "...#..."],
        "search":  [".###...", "#...#..", "#...#..", "#...#..", ".###...", "....##.", ".....##"],
        // A Wi-Fi network's: a password wanted, and the signal in bars, each
        // bar it hasn't left as its foot (signal0: none, no network joined).
        "lock":    [".###.", "#...#", "#...#", "#####", "##.##", "##.##", "#####"],
        "signal0": ["...........", "...........", "...........", "...........", "...........",
                    "...........", "##.##.##.##"],
        "signal1": ["...........", "...........", "...........", "...........", "...........",
                    "##.........", "##.##.##.##"],
        "signal2": ["...........", "...........", "...........", "...........", "...##......",
                    "##.##......", "##.##.##.##"],
        "signal3": ["...........", "...........", "......##...", "......##...", "...##.##...",
                    "##.##.##...", "##.##.##.##"],
        "signal4": [".........##", ".........##", "......##.##", "......##.##", "...##.##.##",
                    "##.##.##.##", "##.##.##.##"],
        // The status icons' (StatusIcons): Bluetooth's rune, with a dot
        // either side while a device is connected; the Arcade's HI (Save
        // High Scores), trophy (RetroAchievements), globe (Online Play) and
        // network, two machines joined to a third (LAN Play).
        "bluetooth":       ["..#..", "..##.", "#.#.#", ".###.", "#.#.#", "..##.", "..#.."],
        "bluetoothLinked": ["....#....", "....##...", "..#.#.#..", "#..###..#", "..#.#.#..", "....##...",
                            "....#...."],
        "hiscore": ["#..#.###", "#..#..#.", "#..#..#.", "####..#.", "#..#..#.", "#..#..#.", "#..#.###"],
        "trophy":  ["#######", "#.###.#", ".#####.", "..###..", "...#...", "..###..", ".#####."],
        "globe":   ["..###..", ".#.#.#.", "#..#..#", "#######", "#..#..#", ".#.#.#.", "..###.."],
        "network": ["..###..", "..###..", "...#...", ".#####.", ".#...#.", "###.###", "###.###"],
        // Menu Layout's Camcorder column (IconColumn): Settings' pages, a
        // set taller than a line of text, as a camcorder's menu draws them:
        // TV / Movie, Arcade, Look, Screen, Sound, Controls, Network,
        // Startup, System (Quit is the eject above); and the main menu's
        // groups, the modules of none as More.
        "tv":       ["..#.....#..", "...#...#...", "....#.#....", "###########", "#.........#", "#.........#",
                     "#.........#", "#.........#", "#.........#", "###########", ".#.......#."],
        "joystick": ["....###....", "...#####...", "...#####...", "....###....", ".....#.....", ".....#.....",
                     ".....#.....", "..#######..", ".#########.", ".#########."],
        "sparkle":  [".....#.....", ".....#.....", "....###....", "..#######..", "###########", "..#######..",
                     "....###....", ".....#.....", ".....#....."],
        "monitor":  ["###########", "#.........#", "#.........#", "#.........#", "#.........#", "#.........#",
                     "###########", ".....#.....", ".....#.....", "...#####..."],
        "speaker":  ["....#......", "...##...#..", "..###....#.", "####..#..#.", "####...#.#.", "####...#.#.",
                     "####..#..#.", "..###....#.", "...##...#..", "....#......"],
        "gamepad":  ["..#######..", ".#########.", "##.######.#", "#...####.##", "##.######.#", "###########",
                     "###.....###", "##.......##"],
        "antenna":  ["..#######..", ".#.......#.", "#..#####..#", "..#.....#..", ".#..###..#.", "...#...#...",
                     ".....#.....", "....###....", ".....#....."],
        "power":    [".....#.....", "..#..#..#..", ".#...#...#.", "#....#....#", "#....#....#", "#.........#",
                     "#.........#", ".#.......#.", "..#.....#..", "...#####..."],
        "gear":     ["....#.#....", ".#.#####.#.", "..#######..", ".###...###.", "####...####", ".###...###.",
                     "..#######..", ".#.#####.#.", "....#.#...."],
        "apps":     ["####.####", "####.####", "####.####", "####.####", ".........", "####.####", "####.####",
                     "####.####", "####.####"],
        // Studio Monitor's STATUS page: a sheet of lines, what is set.
        "status":   ["###########", "#.........#", "#.##.####.#", "#.........#", "#.##.####.#", "#.........#",
                     "#.##.####.#", "#.........#", "###########"]
    })
    readonly property var rows: shapes[name] || []

    width: (rows.length ? rows[0].length : 0) * pixel
    height: rows.length * pixel
    antialiasing: false
    smooth: false

    // The picture at the symbol's own size, a cell `pixel` pixels square, so
    // it is drawn pixel for pixel. Its cells go in the address as rows of 1
    // and 0: a "#" would end a URL's path.
    sourceSize: Qt.size(width, height)
    source: width > 0 && height > 0
            ? "image://osdpixel/" + String(color).replace("#", "") + "/"
              + rows.join(",").replace(/#/g, "1").replace(/\./g, "0")
            : ""
}
