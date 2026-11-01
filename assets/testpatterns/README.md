# Test patterns

Settings → Screen → Test Patterns (`views/TestPatterns.qml`) shows these pictures to
set up a TV or monitor by. They are from the **240p Test Suite** by Artemio
Urbina and its contributors (the Monoscope designed by Keith Raney, the Grid
after the CPS-2's), https://github.com/ArtemioUrbina/240pTestSuite, free
software under the GNU General Public License, version 2 or (at your option)
any later version, here under version 3 with the rest of OSD/OS. The
version 2 text is `assets/licenses/GPL-2.0.txt`.

Taken unchanged from the suite at commit
`8af9e81a3e3bb329afadde48a2efc36f0579f3cf`:

| File here | From the suite | Picture |
|---|---|---|
| `grid-240.png` | `240psuite/Dreamcast/PVR/pngs/grid.png` | 320 × 240 at the top left of a 512 × 256 texture |
| `grid-480.png` | `240psuite/Dreamcast/PVR/pngs/480/grid-480.png` | 640 × 480 at the top left of a 1024 × 512 texture |
| `smpte.png` | `240psuite/Dreamcast/PVR/pngs/SMPTEColorBars.png` | 320 × 240 at the top left of a 512 × 256 texture |
| `monoscope-240.png` | `240psuite/Wii/240pSuite/textures/monoscope.png` | 320 × 240 |
| `monoscope-480.png` | `240psuite/Wii/240pSuite/textures/monoscope480p.png` | 640 × 480 |
| `pluge.png` | `240psuite/Wii/240pSuite/textures/pluge.png` | 320 × 240 |
| `colorbars.png` | `240psuite/Wii/240pSuite/textures/color_grid.png` | 320 × 240 |
| `grayramp.png` | `240psuite/Wii/240pSuite/textures/grayramp.png` | 320 × 240 |
| `sharpness.png` | `240psuite/Wii/240pSuite/textures/sharpness.png` | 320 × 240 |
| `convergence-grid.png` | `240psuite/Wii/240pSuite/textures/Convergence-01-grid.png` | 16 × 16 tile |
| `convergence-cross.png` | `240psuite/Wii/240pSuite/textures/Convergence-02-cross.png` | 16 × 16 tile |
| `convergence-dots.png` | `240psuite/Wii/240pSuite/textures/Convergence-03-dots.png` | 16 × 16 tile |
| `convergence-colors.png` | `240psuite/Wii/240pSuite/textures/Convergence-04-colors.png` | 128 × 64 tile |
| `convergence-colors-black.png` | `240psuite/Wii/240pSuite/textures/Convergence-05-colorsbl.png` | 128 × 64 tile |

The view draws only the picture of a texture (`sourceClipRect`), tiles the
tiles over a 320 × 240 picture as the suite does, and scales each to the
screen's height without smoothing. On a screen whose lines are a whole
number of the Grid's 16-line squares but for 240 and 480 (288p, 576i), the
view draws the Grid itself, in the suite's squares, one line for each of the
screen's, rather than scale `grid-240.png` by a fraction. The help shown with each pattern is put
from the suite's own help screens.
