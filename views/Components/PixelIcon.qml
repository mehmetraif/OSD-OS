import QtQuick

// One of the deck's on-screen symbols (▶ ◀◀ ‖ ■ ● ⏏ …), drawn from a small
// bitmap at a whole number of screen pixels per art pixel, so it stays as crisp
// as the rest of the OSD. Set cells take `color`; the rest stay clear.
Canvas {
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
        "tape":    [".#########.", "###########", "##..###..##", "##..###..##",
                    "##..###..##", "###########", ".#########."],
        // A favourite, as the Arcade screen marks one.
        "heart":   [".##.##.", "#######", "#######", ".#####.", "..###..", "...#..."],
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
        // High Scores), trophy (RetroAchievements) and globe (Online Play).
        "bluetooth":       ["..#..", "..##.", "#.#.#", ".###.", "#.#.#", "..##.", "..#.."],
        "bluetoothLinked": ["....#....", "....##...", "..#.#.#..", "#..###..#", "..#.#.#..", "....##...",
                            "....#...."],
        "hiscore": ["#..#.###", "#..#..#.", "#..#..#.", "####..#.", "#..#..#.", "#..#..#.", "#..#.###"],
        "trophy":  ["#######", "#.###.#", ".#####.", "..###..", "...#...", "..###..", ".#####."],
        "globe":   ["..###..", ".#.#.#.", "#..#..#", "#######", "#..#..#", ".#.#.#.", "..###.."]
    })
    readonly property var rows: shapes[name] || []

    width: (rows.length ? rows[0].length : 0) * pixel
    height: rows.length * pixel
    antialiasing: false
    smooth: false

    onPaint: {
        var ctx = getContext("2d")
        ctx.reset()
        ctx.fillStyle = icon.color
        for (var y = 0; y < rows.length; ++y) {
            var row = rows[y]
            var x = 0
            while (x < row.length) {
                if (row[x] !== "#") {
                    ++x
                    continue
                }
                var start = x
                while (x < row.length && row[x] === "#")
                    ++x
                ctx.fillRect(start * pixel, y * pixel, (x - start) * pixel, pixel)
            }
        }
    }
    onNameChanged: requestPaint()
    onColorChanged: requestPaint()
    onPixelChanged: requestPaint()
}
