# Shaders

A [theme](https://github.com/mehmetraif/OSD-OS/wiki/Themes) can bring two kinds of shader of its own: a **screen effect's**, which draws the whole screen from a picture of it, and a **background effect's**, which draws behind the menus. The selector effects and the transitions are OSD/OS's own: a theme names them, but can't replace them.

## Two kinds

| | A screen effect's shader | A background effect's shader |
|---|---|---|
| In `theme.json` | `"screen": { "shader": "tube.frag.qsb", "animate": true }` | `"background": { "shader": "plasma.frag.qsb", "area": "foot" }` |
| What it draws | The whole screen, from a picture of it given as `source` | Itself only, over the window's ground and under the menus |
| How often it runs | For every screen pixel, each time the screen is drawn | For every art pixel of its area, each time the screen is drawn |
| What it is given | `source`, `resolution`, `px`, `time`, the six screen numbers, the four text numbers, `ink`, `paper` | `size`, `origin`, `time`, `ink`, `paper` |
| Its clock | Counts with `"animate": true` | Always counts while it shows |
| What it replaces | OSD/OS's screen shader, which also draws the text effects | OSD/OS's background effect |

Settings can overrule either: Screen Effect or Background Effect set to a name replaces the theme's, and Off turns it off ([Themes](https://github.com/mehmetraif/OSD-OS/wiki/Themes#how-a-theme-is-applied)).

## How OSD/OS runs a shader

- **The screen shader**: everything OSD/OS draws goes into a picture, which is drawn through the shader. A video never passes through it.
- **The background shader** is drawn one pixel per art pixel and scaled up without smoothing, for the window's ground and for each dialog's.
- **Each uniform** takes the value OSD/OS sets under its name.

## The rules every shader follows

1. **It is written in GLSL 4.40 for Qt Shader Tools**: it starts with `#version 440`. `qsb` translates it for each graphics API.
2. **It reads the point it runs for**, `layout(location = 0) in vec2 qt_TexCoord0;`, from (0, 0) at the top left of what it draws to (1, 1) at the bottom right, **and writes its colour**, `layout(location = 0) out vec4 fragColor;`.
3. **Its uniforms are one block at binding 0 that starts with `mat4 qt_Matrix` and `float qt_Opacity`, in that order:**

   ```glsl
   layout(std140, binding = 0) uniform buf {
       mat4 qt_Matrix;
       float qt_Opacity;
       // then any of OSD/OS's names, in any order: only those it uses
   };
   ```

   With anything before them, the effect draws nothing, and nothing in the log says why.
4. **After those two come any of the names OSD/OS sets**, each with its type (below). A name OSD/OS doesn't set stays 0, and Qt logs `ShaderEffect: 'speed' does not have a matching property`.
5. **A screen shader reads the screen** from `layout(binding = 1) uniform sampler2D source;`. A background shader has no picture to read: don't declare one.
6. **Its colour is premultiplied**: `vec4(color * alpha, alpha)`, multiplied by `qt_Opacity`.
7. **It is compiled into a `.qsb`**, which the theme names ([below](#compiling-with-qsb)).

| QML property | GLSL type | The names |
|---|---|---|
| `size`, `point` | `vec2` | `resolution`, `size`, `origin` |
| `real` | `float` | `px`, `time` and every number |
| `color` | `vec4`: red, green, blue, alpha, each 0 to 1 | `ink`, `paper` |
| the screen's picture | `sampler2D` | `source` |

## A screen effect's shader

| Name | Type | What it is |
|---|---|---|
| `source` | `sampler2D`, binding 1 | The screen as OSD/OS drew it: the menus with the background and selector effects, the boot screen, the screen saver, the pointer. Never a video |
| `resolution` | `vec2` | The screen, in its pixels: 640 × 480, 720 × 576, 1920 × 1080 |
| `px` | `float` | Screen pixels per art pixel, a pixel of a 240-line picture: the screen's height ÷ 240, rounded down (2 at 480 and 576 lines, 3 at 720, 4 at 1080) |
| `time` | `float` | Seconds. It counts with `"animate": true`, and otherwise only while a background effect, `noise` or a moving text effect runs. It starts over at 3600 |
| `scanlines`, `curvature`, `glow`, `bleed`, `noise`, `vignette` | `float` | The theme's screen numbers, 0 to 1, 0 for one left out |
| `rainbow`, `shimmer`, `flicker`, `inkGlow` | `float` | The text effect's numbers, Settings' or the theme's (`inkGlow` is the text's `glow`), 0 for none |
| `ink`, `paper` | `vec4` | The scheme's colour (`primary`) and its background (`surface`). With OSD Background: Off, the lighter of the two and black |

- **The numbers are yours to use**, or to give meanings of their own.
- **It runs** whenever the theme's screen effect has a shader, even with every number 0, where there is a GPU and the effects aren't resting.
- **It draws the whole screen**: write an opaque colour, alpha 1.
- **Pixels and art pixels**: `qt_TexCoord0 * resolution` is the point in screen pixels, and that `/ px` in art pixels. Work in whole art pixels (`floor()`) and the menus stay crisp.
- **It replaces OSD/OS's screen shader, text effects and all.** Settings → Look → Effects → Text Effect's numbers still reach it, but show only if it draws them ([example 4](#4-keeping-the-text-effects)).

## A background effect's shader

| Name | Type | What it is |
|---|---|---|
| `size` | `vec2` | The area it draws, in art pixels |
| `origin` | `vec2` | Where that area's top left is on the screen, in art pixels |
| `time` | `float` | Seconds, counting while it shows. It starts over at 3600 |
| `ink`, `paper` | `vec4` | The colours in force, as for the screen |

- **One pixel per art pixel.** `floor(qt_TexCoord0 * size)` is the art pixel in the area, and adding `origin` makes it the art pixel on the screen.
- **The area** is the window inside its frame, or, with `"area": "foot"`, from the window's top down to the screen's foot. With OSD Background Full or Off it is the whole screen ([Effects](https://github.com/mehmetraif/OSD-OS/wiki/Effects#background-effects)).
- **Every ground draws it again**, the window's and each dialog's, with the same `size` and `origin`. A shader that works from `floor(qt_TexCoord0 * size) + origin` draws the same picture under a dialog.
- **Clear where it draws nothing**: `fragColor = vec4(0.0)`. Keep what it draws faint, an alpha of 0.3 to 0.5.

## Compiling with qsb

### The command

After every change, compile the shader into the `.qsb` that `theme.json` names:

```sh
qsb --glsl "100 es,120,150" --hlsl 50 --msl 12 -o plasma.frag.qsb plasma.frag
```

| Option | Makes the shader for |
|---|---|
| `--glsl "100 es,120,150"` | OpenGL ES (the Raspberry Pi) and desktop OpenGL (Linux, SteamOS) |
| `--msl 12` | Metal (macOS) |
| `--hlsl 50` | Direct3D (Windows) |
| always, without an option | SPIR-V, for Vulkan |

A `.qsb` holds them all, so it can be made on any computer and copied to any other.

On an error, `qsb` prints the compiler's message with the line, exits with 1, and writes nothing:

```text
QSpirvCompiler: Failed to parse shader
Shader baking failed: ERROR: warp.frag:38: '' :  syntax error, unexpected FOR, expecting COMMA or SEMICOLON
ERROR: 1 compilation errors.  No code generated.
```

### Where qsb comes from

`qsb` comes with Qt Shader Tools.

| On | Get it | Run it as |
|---|---|---|
| Raspberry Pi OS, Debian, Ubuntu | `sudo apt install qt6-shader-baker` | `/usr/lib/qt6/bin/qsb` |
| macOS, Qt from Qt's online installer | Add Qt Shader Tools (under Additional Libraries) | `~/Qt/<version>/macos/bin/qsb` |
| macOS, Homebrew | `brew install qt`, which includes Qt Shader Tools | find it with `find "$(brew --prefix qt)/" -name qsb` |
| SteamOS | — | Compile on another computer and copy the `.qsb` |

### Which Qt's qsb

- Qt reads a `.qsb` of its own format and older ones, but not newer: compile with the Qt OSD/OS runs on, or an older one.
- The template's shaders are compiled with `qsb` 6.4 (format 6), which every Qt OSD/OS runs on reads.
- Debian 12's and Ubuntu 24.04's `qt6-shader-baker` is `qsb` 6.4.

`qsb -d warp.frag.qsb` shows a file's format (`QSB_VERSION: 6`), its variants (`GLSL 120`, `GLSL 150`, `GLSL 100 es`, `SPIR-V 100`, `MSL 12`, `HLSL 50`) and its uniform block.

## Loading, fallback and the log

| What is wrong | What the log says | What is drawn |
|---|---|---|
| The file isn't there, isn't inside the theme's folder, or isn't a `.qsb` | `[AppCore] theme my-theme: its screen shader, "tube.frag.qsb", is not a qsb file in its folder` (or `background shader`) | Screen: OSD/OS's shader, with the theme's numbers. Background: nothing |
| The file isn't a compiled shader, or comes from a newer Qt | Qt's `ShaderEffect: Failed to deserialize QShader from …`, then `qml: [Effect] file:///…/tube.frag.qsb can't be used, OSD/OS's own in its place` (a background's: `… can't be used`) | The same |
| It was compiled without the variant this Qt draws with (no `--glsl` on the Pi, no `--msl` on a Mac) | Qt's own complaint (with OpenGL, `No GLSL shader code found …`), then `Failed to build graphics pipeline state`; no `[Effect]` line | Nothing. A screen shader takes the menus with it |
| Its uniform block doesn't start with `qt_Matrix` and `qt_Opacity` | Nothing | Nothing. A screen shader takes the menus with it |
| It declares a name OSD/OS doesn't set | `ShaderEffect: 'speed' does not have a matching property` | It works, that value 0 |

- **A screen shader that failed** is tried again when the theme is chosen again, after another.
- **A screen shader that leaves the menus unreadable**: move the theme out of `themes` over SSH and restart OSD/OS ([Themes](https://github.com/mehmetraif/OSD-OS/wiki/Themes#getting-back-from-a-theme-that-hides-the-menus)).
- **A shader changed under the same file name** stays as it was until OSD/OS restarts. To try changes without restarting, compile to a new file name (`warp2.frag.qsb`), change `theme.json` to match, and choose the theme again.

Where to read the log: [Themes](https://github.com/mehmetraif/OSD-OS/wiki/Themes#reading-the-log).

## Worked examples

Each comes with what it takes in its theme's `effects`. Save the source in the theme's folder, compile it there with the [command above](#the-command), and choose the theme again.

### 1. A hold roll over the screen

A screen effect: every twelve seconds a band rolls down the picture, its lines pulled sideways, with the theme's scanlines all the time.

```json
{ "screen": { "shader": "holdroll.frag.qsb", "animate": true, "scanlines": 0.3 } }
```

`holdroll.frag`:

```glsl
#version 440
// A screen effect: every twelve seconds a band rolls down the picture, the
// lines in it pulled sideways like a set losing its horizontal hold, and the
// theme's scanlines all the time. It moves: its theme.json says "animate": true.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 resolution;  // the screen, in its pixels
    float px;         // screen pixels to an art pixel
    float time;       // seconds, counting while "animate" is true
    float scanlines;  // the theme's "scanlines", 0 to 1
};

layout(binding = 1) uniform sampler2D source;

void main()
{
    vec2 uv = qt_TexCoord0;
    // Which line of art pixels this is: a whole line moves together.
    float line = floor(uv.y * resolution.y / px);

    // The band, an eighth of the screen either side of its middle, rolls from
    // above the top to below the foot in the first 3 seconds of every 12.
    float cycle = mod(time, 12.0);
    float middle = -0.25 + 1.5 * cycle / 3.0;
    float band = (1.0 - smoothstep(0.0, 0.125, abs(uv.y - middle))) * step(cycle, 3.0);

    // Each line in the band pulled its own way, up to 6 art pixels, in whole
    // art pixels so the letters stay crisp.
    float pull = floor(sin(line * 0.9 + time * 20.0) * 6.0 * band + 0.5);
    uv.x += pull * px / resolution.x;

    // Off the picture's edge, black, as on a tube.
    float inside = step(0.0, uv.x) * step(uv.x, 1.0);
    vec3 color = texture(source, uv).rgb * inside;

    // The torn band a little brighter.
    color *= 1.0 + 0.15 * band;

    // Scanlines: the foot of every line of art pixels darker.
    color *= 1.0 - scanlines * 0.7 * step(0.5, fract(qt_TexCoord0.y * resolution.y / px));

    fragColor = vec4(clamp(color, 0.0, 1.0), 1.0) * qt_Opacity;
}
```

- `line` numbers the lines of art pixels, so that a whole line moves as one.
- The cycle is 12 seconds, which divides 3600: the roll keeps its beat when the clock starts over.
- It reads `source` once per pixel, so it costs about as much as plain scanlines.

### 2. A warp starfield

A background effect: stars streaming out of the middle of the window, faster, brighter and drawn out into streaks near the edges.

```json
{ "background": { "shader": "warp.frag.qsb" } }
```

`warp.frag`:

```glsl
#version 440
// A background effect: stars streaming out of the middle of the window as if
// it flew through them, slow and dim at first, then faster, brighter and
// drawn out into streaks near its edges. On art pixels, in the scheme's colour.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 size;    // the area it draws, in art pixels
    vec2 origin;  // where that is on the screen, in art pixels
    float time;   // seconds
    vec4 ink;     // the colour scheme's colour
};

// A number from 0 to 1 for a point, without sin(): the Pi's GPU keeps its
// precision.
float grain(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main()
{
    // This art pixel's middle, from the area's middle (every copy of the
    // effect, a dialog's too, has the same area, so the same middle).
    vec2 p = floor(qt_TexCoord0 * size) + origin;
    vec2 d = p + 0.5 - (origin + size * 0.5);
    float r = length(d);
    float a = atan(d.y, d.x);
    float reach = length(size) * 0.5;

    float light = 0.0;
    for (int i = 0; i < 2; ++i) {
        float layer = float(i);
        // The circle cut into slices, one star flying out along each.
        float slices = 72.0 + 48.0 * layer;
        float k = floor((a / 6.2831853 + 0.5) * slices);
        float along = (k + 0.5) / slices * 6.2831853 - 3.1415927;
        // How far this pixel is from the slice's middle line, in art pixels.
        float off = abs(sin(a - along)) * r;

        // How far out the star is: 0 at the middle, 1 at the edge, a trip
        // every 3 to 7 seconds; faster the further out, as stars rush past.
        float rate = 0.14 + 0.2 * grain(vec2(k, 7.0 + layer));
        float f = fract(time * rate + grain(vec2(k, 3.0 + layer)));
        float head = f * f * reach;
        float tail = 0.5 + 7.0 * f * f;

        if (off < 0.5 && r <= head && r > head - tail)
            light = max(light, 0.2 + 0.7 * f);
    }
    vec3 color = mix(ink.rgb, vec3(1.0), 0.3);
    fragColor = vec4(color * light, light) * qt_Opacity;
}
```

`grain()` makes a number from 0 to 1 with arithmetic only: hashes built on `sin()` lose their precision on some GPUs, the Pi's among them.

### 3. A synthwave grid along the foot

A background effect for `"area": "foot"`: a floor of grid lines running to a horizon two fifths of the way up, scrolling towards you.

```json
{ "background": { "shader": "grid.frag.qsb", "area": "foot" } }
```

`grid.frag`:

```glsl
#version 440
// A background effect for "area": "foot": a floor of grid lines running to a
// horizon and scrolling towards you, as on a synthwave record's sleeve. Its
// lines are an art pixel wide, in the scheme's colour, fading into the distance.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 size;    // the area, in art pixels: the window's top to the screen's foot
    vec2 origin;  // its top left on the screen, in art pixels
    float time;   // seconds
    vec4 ink;     // the colour scheme's colour
};

// The floor's rows, counted from the horizon and coming towards you, at a
// point `below` art pixels under it.
float rowAt(float below)
{
    return 48.0 / below + time * 1.5;
}

// The lines running to the horizon, counted from its middle, at a point `x`
// art pixels across from the middle and `below` under it.
float lineAt(float x, float below)
{
    return x * 3.0 / below;
}

// Whether a whole number lies between two values: a line crosses there.
bool crosses(float a, float b)
{
    return floor(min(a, b)) != floor(max(a, b));
}

void main()
{
    vec2 p = floor(qt_TexCoord0 * size) + origin;
    // The horizon two fifths of the way up from the area's foot, the lines
    // meeting in its middle.
    float horizon = floor(origin.y + size.y * 0.6);
    float x = p.x - (origin.x + size.x * 0.5);
    float below = p.y - horizon;

    float light = 0.0;
    if (below == 0.0) {
        light = 0.6;                       // the horizon itself
    } else if (below > 0.0) {
        // Lit when a row or a line passes through this art pixel: between its
        // top and its foot, or between any two of its corners. Rows only
        // from where they are two art pixels apart or more.
        bool row = below >= 10.0 && crosses(rowAt(below), rowAt(below + 1.0));
        bool line = crosses(lineAt(x, below), lineAt(x + 1.0, below + 1.0))
                 || crosses(lineAt(x + 1.0, below), lineAt(x, below + 1.0));
        if (row || line)
            // Gone near the horizon, where they would crowd together.
            light = 0.45 * smoothstep(12.0, 56.0, below);
    }
    fragColor = vec4(ink.rgb * light, light) * qt_Opacity;
}
```

- With `"area": "foot"` the floor fills the lowest two fifths of the area, from below the window up into it.
- When `time` starts over at 3600, every row moves by 5400 rows, a whole number, so nothing jumps.

### 4. Keeping the text effects

A screen shader of your own replaces OSD/OS's, which draws the text effects (Rainbow, Shimmer, Glow, Flicker). This one draws them as OSD/OS's does, so Settings → Look → Effects → Text Effect and the theme's `text` keep working. Its own look is only scanlines.

```json
{
    "text": "Rainbow",
    "screen": { "shader": "keeptext.frag.qsb", "scanlines": 0.3 }
}
```

`keeptext.frag`:

```glsl
#version 440
// A screen effect of one's own that keeps Settings → Look → Effects → Text Effect working:
// the text effects are drawn by OSD/OS's own screen shader, which this one
// replaces, so it draws them itself, as OSD/OS's does (shaders/effects.frag).
// Its own look here is only scanlines: put yours where it says.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 resolution;
    float px;
    float time;
    float scanlines;
    float rainbow;    // the text effect's numbers, 0 to 1
    float shimmer;
    float flicker;
    float inkGlow;
    vec4 ink;
    vec4 paper;
};

layout(binding = 1) uniform sampler2D source;

float grain(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec3 hue(float h)
{
    return clamp(abs(fract(h + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
}

// How much of the ink a colour is: 0 the paper, 1 the ink.
float inkOf(vec3 c)
{
    vec3 axis = ink.rgb - paper.rgb;
    return clamp(dot(c - paper.rgb, axis) / max(dot(axis, axis), 0.0001), 0.0, 1.0);
}

// The text effects, on what is drawn in the two colours only.
vec3 textEffects(vec3 color, vec2 uv)
{
    vec2 art = px / resolution;
    float t = inkOf(color);
    float drawn = 1.0 - smoothstep(0.06, 0.2, length(color - mix(paper.rgb, ink.rgb, t)));
    vec3 newInk = ink.rgb;
    if (rainbow > 0.0) {
        vec2 cell = floor(uv / art);
        newInk = mix(newInk, mix(hue(fract(cell.x * 0.006 + cell.y * 0.003 - time * 0.15)), vec3(1.0), 0.15), rainbow);
    }
    if (shimmer > 0.0) {
        float d = uv.x + uv.y * 0.35 - (fract(time / 3.5) * 1.8 - 0.4);
        newInk = mix(newInk, vec3(1.0), (1.0 - smoothstep(0.0, 0.06, abs(d))) * shimmer * 0.85);
    }
    if (flicker > 0.0) {
        float f = grain(vec2(floor(time * 12.0), 7.31));
        float dip = f > 0.9 ? 1.0 : (f > 0.82 ? 0.5 : 0.0);
        newInk = mix(paper.rgb, newInk, 1.0 - flicker * (0.75 * dip + 0.06 * (0.5 + 0.5 * sin(time * 45.0))));
    }
    if (inkGlow > 0.0) {
        vec2 g = art * 1.5;
        float around = (inkOf(texture(source, uv + vec2(g.x, 0.0)).rgb) + inkOf(texture(source, uv - vec2(g.x, 0.0)).rgb)
                      + inkOf(texture(source, uv + vec2(0.0, g.y)).rgb) + inkOf(texture(source, uv - vec2(0.0, g.y)).rgb)) * 0.25;
        t = max(t, t + (around - t) * inkGlow * 0.8);
    }
    return mix(color, paper.rgb + (newInk - paper.rgb) * t, drawn);
}

void main()
{
    vec2 uv = qt_TexCoord0;
    vec3 color = texture(source, uv).rgb;
    color = textEffects(color, uv);
    // Your own look here; this one's is scanlines.
    color *= 1.0 - scanlines * 0.7 * step(0.5, fract(uv.y * resolution.y / px));
    fragColor = vec4(clamp(color, 0.0, 1.0), 1.0) * qt_Opacity;
}
```

- Copy `textEffects()` and its three helpers into a shader of your own, and call it on the colour you read from `source`.
- `drawn` is 1 for colours between `paper` and `ink` (the text, the lines, the bars) and 0 for anything else, which is left as it is.
- No `"animate"` is needed for Rainbow, Shimmer and Flicker. A shader that moves on its own needs `"animate": true`.

## The template's shaders, line by line

Both sources are in the [theme template](https://github.com/mehmetraif/OSD-OS/tree/main/docs/theme-template), with their own comments. The lines that matter:

### plasma.frag: a background

[plasma.frag](https://github.com/mehmetraif/OSD-OS/blob/main/docs/theme-template/plasma.frag): a plasma drifting behind the menus in the scheme's colour. OSD/OS has it built in too, as Settings → Look → Effects → Background Effect's **Plasma** (`shaders/bg-plasma.frag`).

| Lines | What they do |
|---|---|
| `uniform buf { … }` | `qt_Matrix` and `qt_Opacity` first, then `size`, `origin`, `time`, `ink` and `paper` (declared, but not used) |
| `bayer2()`, `bayer4()` | A 4 × 4 ordered dither, built from two 2 × 2 ones: 16 thresholds from 0 to 15/16 |
| `vec2 p = floor(qt_TexCoord0 * size) + origin;` | The art pixel on the screen: the same under a dialog's ground as under the window's |
| `float v = …` | The plasma: four waves added, across, down, slanted, and in rings round a wandering point |
| `step(bayer4(p), level * level * 0.5)` | The dither: a pixel is lit where its threshold is below the brightness, at most half of them |
| `float alpha = 0.4 * lit;` | A lit pixel at 40% of the scheme's colour, an unlit one clear |

### tube.frag: a screen

[tube.frag](https://github.com/mehmetraif/OSD-OS/blob/main/docs/theme-template/tube.frag): a picture tube's phosphor stripes, a hum bar rolling slowly down the picture, and the theme's `scanlines`.

| Lines | What they do |
|---|---|
| `uniform buf { … }` | `qt_Matrix` and `qt_Opacity`, then only what it uses: `resolution`, `px`, `time`, `scanlines` |
| `float stripe = mod(floor(pixel.x), 3.0);` | Which of three columns this screen pixel is in |
| `vec3 mask = …; color *= mask * 1.2;` | A phosphor stripe: each column keeps one of red, green and blue whole and 70% of the other two, then 20% brighter all over |
| `float fromBar = abs(fract(qt_TexCoord0.y - time * 0.05) - 0.5);` | The distance from the hum bar, which comes round once every 20 seconds |
| `color *= 1.0 - scanlines * 0.7 * step(0.5, fract(pixel.y / px));` | Scanlines: the lower half of every line of art pixels darkened by up to 70% |

The template's `theme.json` gives it `"animate": true`, so that the hum bar rolls, and `"scanlines": 0.4`. It doesn't read the text numbers, so the template's text effect doesn't show ([example 4](#4-keeping-the-text-effects)).

## Tips

- **Start from OSD/OS's own** [shaders](https://github.com/mehmetraif/OSD-OS/tree/main/shaders), which take the same names: `effects.frag` is the screen and text effects, `bg-matrix.frag`, `bg-fire.frag`, `bg-stars.frag`, `bg-snow.frag`, `bg-flag.frag` and `bg-plasma.frag` the backgrounds.
- **`bg-fire.frag`**, compiled into a theme with `"area": "foot"`, burns from the screen's foot instead of the window's.
- **Think in art pixels.** Snap to them with `floor()`, and shade with a dither rather than with transparency.
- **Make random numbers without `sin()`**: use the `grain()` hash above.
- **Keep it light.** A screen shader runs for every screen pixel, about 2 million at 1080p, up to thirty times a second. The Pi's GPU is slower than a computer's.
- **Mind the hour.** `time` starts over at 3600. A period that divides 3600 (1, 2, 3, 4, 5, 6, 8, 10, 12, 15, 20, 30, 60 seconds) keeps its beat across it.

## See also

- [Themes](https://github.com/mehmetraif/OSD-OS/wiki/Themes): the `theme.json` a shader goes in, and making a theme
- [Effects](https://github.com/mehmetraif/OSD-OS/wiki/Effects): OSD/OS's own effects, their numbers, and what they cost
- [Building from source](https://github.com/mehmetraif/OSD-OS/wiki/Building-from-Source): Qt Shader Tools in a build
- [Troubleshooting](https://github.com/mehmetraif/OSD-OS/wiki/Troubleshooting)
