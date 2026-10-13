# High score files for the tests

MAME's high score files of four games, for `highscores_test.cpp`, laid
out as MAME keeps them:

| File | Game | Kept by |
|---|---|---|
| `hiscore/ffight.hi` | Final Fight | MAME's hiscore plugin |
| `hiscore/gng.hi` | Ghosts'n Goblins | MAME's hiscore plugin |
| `nvram/tgm2/eeprom` | Tetris The Absolute: The Grand Master 2 | the game's own EEPROM |
| `nvram/solarfox/nvram` | Solar Fox | the game's own NVRAM: a best score alone, no table |
| `hiscore.dat` | | MAME's hiscore plugin |

They come from the test inputs of hi2txt's game definitions
([GreatStoneEx/hi2txt-xml](https://github.com/GreatStoneEx/hi2txt-xml),
commit 990fca2, GPL-2.0, the definitions' source in
`modules/highscores/data/README.md`):

- `ffight.hi` from `src/test/input/2022_01_01_0239/hi/`;
- `gng.hi` from `src/test/input/2014_02_11_0148/hi/`;
- `tgm2/eeprom` from `src/test/input/2014_04_23/nvram/`;
- `solarfox/nvram` from `src/test/input/2017_01_04_0181/nvram/`;
- `hiscore.dat` holds the `ffight` and `gng` blocks of
  `src/test/input/2022_01_01_0239/hiscore.dat`, MAME's hiscore.dat of
  that day. Ghosts'n Goblins' definition has two layouts of the same
  size; the block tells them apart, and without it the table comes out
  in the wrong order.
