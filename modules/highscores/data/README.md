# hi2txt definitions

`hi2txt.zip` holds the game definitions of [hi2txt-xml](https://github.com/GreatStoneEx/hi2txt-xml): one XML file per game, describing how its high score table is laid out in the file MAME saves it in (the hiscore plugin's `.hi` file, or the game's NVRAM). The High Scores module reads them through openhi2txt (`thirdparty/openhi2txt`).

- **Source**: hi2txt-xml at commit `990fca2` (13 February 2022), every `src/main/db/*.xml`: 3,102 definitions.
- **Licence**: GPL-2.0, in `hi2txt-LICENSE`. The definitions are data the module reads at run time.
- **Authors**: GreatStone and the contributors named in hi2txt-xml's README; many definitions come from HiToText.

To update them, from a newer checkout of hi2txt-xml:

```sh
git clone --depth 1 https://github.com/GreatStoneEx/hi2txt-xml
scripts/update-hi2txt.sh hi2txt-xml
```

The script zips the definitions in name order with fixed times, so the same checkout always gives the same zip. Note the new commit above.
