# Effects

OSD/OS has five kinds of effect: on the text, behind the menus, round the selected line, over the whole screen, and between windows. Each has a row in Settings → Look → **Effects** and a key in a [theme](https://github.com/mehmetraif/OSD-OS/wiki/Themes).

## At a glance

| Kind | Settings row | Choices | Drawn by | Theme key | Config key |
|---|---|---|---|---|---|
| Text | Text Effect | Rainbow, Shimmer, Glow, Flicker | the GPU | `effects.text` | `app.text_effect` |
| Background | Background Effect | Matrix, Fire, Stars, Snow, Checkered Flag, Plasma | the GPU | `effects.background` | `app.background_effect` |
| Selector | Selector Effect | Sparkles, Welding, Lightning, Rainbow, Snow | the CPU | `effects.selector` | `app.selector_effect` |
| Screen | Screen Effect | Scanlines, CRT, VHS | the GPU | `effects.screen` | `app.screen_effect` |
| Transition | Transition | Fade, Cube, Ripple, Wave, Drop | Qt Quick; Ripple, Wave and Drop the GPU | `effects.transition` | `app.transition` |

Every row also offers **Off**, and **THEME** while there is a theme. The default is THEME; without a theme, Off.

## Setting an effect

### In Settings

Open Settings → Look → **Effects** and change a row with ◄ ►.

- **THEME** (saved as `""`) is the theme's effect, **Off** none, and a name that effect, whatever the theme says.
- Choosing a theme sets every row back to THEME.
- Without a theme, THEME isn't offered and `""` shows as Off ([Themes](https://github.com/mehmetraif/OSD-OS/wiki/Themes#how-a-theme-is-applied)).
- Text Effect, Background Effect and Screen Effect are dimmed where the GPU can't draw them ([below](#what-needs-a-gpu-and-qt-shader-tools)).

### In a theme

A theme's `effects` takes each effect's name, `"Off"`, or an object of its own: numbers for the text and the screen, a shader for the background or the screen.

```json
{
    "effects": {
        "text":       "Shimmer",
        "background": "Snow",
        "selector":   "Sparkles",
        "screen":     { "scanlines": 0.4, "curvature": 0.5, "vignette": 0.6 },
        "transition": "Drop"
    }
}
```

Names are matched exactly, capitals included. Numbers go from 0 (none) to 1. Every key: [theme.json reference](https://github.com/mehmetraif/OSD-OS/wiki/Themes#effects).

### In config.json

Settings saves each row under `app` in `config.json` ([Configuration files](https://github.com/mehmetraif/OSD-OS/wiki/Configuration-Files)): `""` for THEME, `"Off"`, or the name.

```json
{
    "app": {
        "text_effect": "Glow",
        "background_effect": "Off",
        "selector_effect": "Sparkles",
        "screen_effect": "CRT",
        "transition": "Drop"
    }
}
```

The rows take names only. Numbers of your own go in a theme.

## Text effects

They change what is drawn in the scheme's two colours: the text, the lines and the bars.

| Name | What it looks like | Its number |
|---|---|---|
| **Rainbow** | The ink runs through the rainbow's colours in slanted, drifting bands. A full turn takes 6.7 seconds | `rainbow` 1.0 |
| **Shimmer** | A slanted white glint sweeps across the screen every three and a half seconds | `shimmer` 1.0 |
| **Glow** | A halo of the ink round the text and the lines | `glow` 0.8 |
| **Flicker** | The ink dips towards the background now and then, like a failing neon sign, with a faint buzz | `flicker` 0.8 |

In a theme, the four are numbers you can mix:

| In theme.json | In a shader | What it does at 1 |
|---|---|---|
| `rainbow` | `rainbow` | The ink entirely the rainbow's colours; at 0.5 halfway between them and the ink |
| `shimmer` | `shimmer` | The glint at its brightest, 85% white |
| `glow` | `inkGlow` | The fullest halo |
| `flicker` | `flicker` | Dips to a quarter of the ink, and the strongest buzz |

```json
{ "text": { "glow": 0.5, "flicker": 0.15 } }
```

With a screen shader of its own, a theme's text effect shows only if that shader draws it ([Shaders](https://github.com/mehmetraif/OSD-OS/wiki/Shaders#4-keeping-the-text-effects)).

## Background effects

They are drawn behind the menus.

| Name | What it looks like |
|---|---|
| **Matrix** | A rain of glyphs falling in columns, each with a bright head and a fading trail |
| **Fire** | Pixel flames burning up from the window's foot, about a third of the way up |
| **Stars** | A starfield drifting from right to left in three layers, twinkling |
| **Snow** | Snow falling in three layers, swaying |
| **Checkered Flag** | A racing flag of 16-art-pixel squares, waving |
| **Plasma** | An old demo's plasma, dithered and drifting slowly ([its shader](https://github.com/mehmetraif/OSD-OS/wiki/Shaders#plasmafrag-a-background)) |

- **In the window** (all six): with OSD Background: Window, inside the window's frame, which is the skin's `border` wide. With Full or Off, the whole screen.
- **Along the foot** (`"area": "foot"`, for a theme's own shader): from the window's top down to the screen's foot ([Shaders](https://github.com/mehmetraif/OSD-OS/wiki/Shaders#a-background-effects-shader)).
- **On art pixels**: one pixel per art pixel, a pixel of a 240-line picture, scaled up without smoothing. A dialog's ground draws the same picture in the same place.

## Selector effects

They play round the selected line, or round a dialog's answer while one is open.

| Name | What it looks like |
|---|---|
| **Sparkles** | Sparks streaming off the line's four corners and fading. About a third twinkle as small crosses |
| **Welding** | Bursts of sparks from one corner at random, white hot, cooling to red as they fall |
| **Lightning** | Jagged bolts from a corner outward, now and then along the line's top or bottom edge |
| **Rainbow** | A pixel rainbow of six bands running down from under the line. It runs under the lines below; in a dialog, in front |
| **Snow** | Flakes falling from the line's top edge and melting a line or two below. They stay where they fall as the line moves |

They need no GPU. Selector Effect: Snow and Background Effect: Snow are separate, and can be used together.

## Screen effects

A picture tube's or a tape's look over everything OSD/OS draws: the menus and their effects, the boot screen, the screen saver and the pointer. A video is never under it ([below](#never-over-a-video)).

| Name | What it looks like | Its numbers |
|---|---|---|
| **Scanlines** | Dark lines between the picture's lines | `scanlines` 0.5 |
| **CRT** | A tube's curved face, its corners rounded off and darker, with scanlines and a glow round light parts | `scanlines` 0.35, `curvature` 0.6, `glow` 0.35, `vignette` 0.5 |
| **VHS** | A tape's colour bleeding sideways, moving grain, a little glow and faint scanlines | `bleed` 0.6, `noise` 0.5, `glow` 0.25, `scanlines` 0.15 |

The six numbers, each from 0 (none) to 1:

| Number | What it does at 1 |
|---|---|
| `scanlines` | The lower part of every line of art pixels darkened by up to 70% |
| `curvature` | The picture bulges like a tube's face, up to 15% at the corners, which round off into black |
| `glow` | Light parts glow into the dark round them, an art pixel and a half out |
| `bleed` | Red taken from up to an art pixel and a half to the left, blue from as far to the right |
| `noise` | Grain on every art pixel, up to 15% lighter or darker, new at every frame |
| `vignette` | Black at the very corners, half as bright at the middle of each edge |

```json
{ "screen": { "scanlines": 0.5, "curvature": 0.8, "glow": 0.2, "noise": 0.1, "vignette": 0.7 } }
```

A theme's `screen` can also be a shader of its own, which is given these numbers ([Shaders](https://github.com/mehmetraif/OSD-OS/wiki/Shaders#a-screen-effects-shader)).

## Transitions

How one window gives way to the next.

| Name | What happens | Time | Pace |
|---|---|---|---|
| **Fade** | The old window fades away over the new | 0.48 s | slow at both ends |
| **Cube** | The window turns over like a cube's face, the next window coming in from a side at random. Cube Left, Right, Up and Down are read as Cube | 0.65 s | slow at both ends |
| **Ripple** | The old window ripples out as the new ripples in | 0.48 s | slow at both ends |
| **Wave** | A wave runs out from a corner at random, the old window settling into the new as it passes | 0.9 s | even |
| **Drop** | A drop falls in a corner at random. Its ring spreads, the new window inside it | 1 s | slowing |

<table>
<tr><th width="50%">Cube</th><th width="50%">Drop</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/images/transition-cube.gif" width="100%" alt="Transition: Cube" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/images/transition-drop.gif" width="100%" alt="Transition: Drop" /></td></tr>
</table>

Ripple, Wave and Drop are shaders on the GPU. Where they can't run, Settings doesn't offer them, and a theme's is played as a Fade.

### When a window changes without one

The next window is shown at once:

- when Transition is Off, or the theme has none, or names one that isn't;
- while the effects rest, and into or out of a video's screen ([below](#never-over-a-video));
- for the first screen, and while the boot screen or the window asking to keep a new display output is up;
- for a module's first screen, which comes in with the module;
- when the old window can't be caught as a picture within a quarter of a second.

A change asked for while another plays finishes that one at once.

### How a window change runs

Every screen change goes through `root.changeWindow(loader, source, properties)` in [Main.qml](https://github.com/mehmetraif/OSD-OS/blob/main/Main.qml): the main menu's, Settings', and each module's own, from its `Root.qml`. The old window is caught as a still picture, and the new one is live from the start.

## Never over a video

The effects and the menu music **rest** while:

| What | When |
|---|---|
| A video plays inside OSD/OS's window | With Settings → TV / Movie → Transparent Background, full screen or behind the menus |
| Another program has the screen | On a Pi without a desktop: mpv playing a video, a takeover script, an Arcade game in MAME, a web player |
| A video is loading | The loading screen |
| A video's menu is open | The menu that back opens over a video with Transparent Background ([Using OSD/OS](https://github.com/mehmetraif/OSD-OS/wiki/Using-OSD-OS)) |
| A module shows a video's screen | A player's, a card's, Netflix's and Prime Video's launch screen, a script's console or takeover, YouTube's sign-in |
| A test pattern is up | Settings → Screen → Test Patterns |

While they rest:

- the text, background, selector and screen effects are off;
- the menu music stops, and starts again from the beginning afterwards ([Menu music](https://github.com/mehmetraif/OSD-OS/wiki/Menu-Music#when-it-plays-and-when-it-rests));
- a window changes without its transition.

A video played inside the window never passes through a shader.

## What needs a GPU and Qt Shader Tools

| Effect | Needs Qt Shader Tools in the build | Needs a GPU |
|---|---|---|
| Text, Background and Screen effects | yes | yes |
| Transition: Ripple, Wave, Drop | yes | yes |
| A theme's own background or screen shader | no: its `.qsb` comes compiled | yes |
| Selector effects | no | no |
| Transition: Fade, Cube | no | no |

- **Qt Shader Tools** compiles OSD/OS's [shaders](https://github.com/mehmetraif/OSD-OS/tree/main/shaders) into the app when it is built. The releases and the OSD/OS image have them.
- **A build from source** needs `qt6-shadertools-dev` on Debian and Raspberry Pi OS, or Qt Shader Tools from Qt's installer. Without it, CMake says `Qt Shader Tools not found — the shader effects will be unavailable` ([Building from source](https://github.com/mehmetraif/OSD-OS/wiki/Building-from-Source)).
- **A GPU** is Qt Quick drawing with OpenGL, OpenGL ES, Metal, Direct3D or Vulkan. Qt's software renderer draws no shaders.
- **Without either**, Text Effect, Background Effect and Screen Effect are dimmed in Settings, and Transition leaves out Ripple, Wave and Drop. A theme's Ripple, Wave or Drop is played as a Fade.

## What they cost

- **Still effects** (Scanlines, CRT, Glow) draw the screen only when something on it changes.
- **Moving effects** draw it about thirty times a second: a background effect, VHS's noise, Rainbow, Shimmer, Flicker, or a theme's screen shader with `"animate": true`.
- **A screen or text effect** runs for every screen pixel: about 2 million at 1080p, 307,200 at 640 × 480.
- **A background effect** runs on art pixels: a quarter of the screen's pixels at 480 lines, a sixteenth at 1080.
- **The selector effect** draws a small picture on the CPU.

## See also

- [Themes](https://github.com/mehmetraif/OSD-OS/wiki/Themes), [Skins](https://github.com/mehmetraif/OSD-OS/wiki/Skins), [Shaders](https://github.com/mehmetraif/OSD-OS/wiki/Shaders), [Menu music](https://github.com/mehmetraif/OSD-OS/wiki/Menu-Music): the rest of the look
- [Settings](https://github.com/mehmetraif/OSD-OS/wiki/Settings#look): every row of the look
- [How it works](https://github.com/mehmetraif/OSD-OS/wiki/How-It-Works): the shell, the display path and playback
