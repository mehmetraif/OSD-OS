# Netflix and Prime Video

What Netflix and Prime Video carry in your country, browsed in OSD/OS's own menus and played in the service's own player, full screen in Chromium. The two modules work the same way, so one page covers both.

<table>
<tr><th width="50%">Netflix</th><th width="50%">Movies › Popular</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/netflix.png" width="100%" alt="Netflix: Recently Watched, Favorites, Search, Movies, Series and Netflix Home" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/netflix-movies.png" width="100%" alt="Movies › Popular: a page of titles at a time" /></td></tr>
</table>

<table>
<tr><th width="50%">Info screen</th><th width="50%">Playing on Netflix</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/info-screen.png" width="100%" alt="A title's info screen: story, genre, director, cast and rating from TMDB" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/netflix-player.png" width="100%" alt="Netflix has the screen: hold back for two seconds to come back" /></td></tr>
</table>

<table>
<tr><th width="50%">Prime Video</th><th width="50%">Search (shown here in Local Files)</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/prime-video.png" width="100%" alt="Prime Video: the same tree" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/keyboard.png" width="100%" alt="The on-screen keyboard" /></td></tr>
</table>

## How it works

- **The catalogue** comes from [TMDB](https://www.themoviedb.org) (The Movie Database), which lists per country what each service carries, from [JustWatch](https://www.justwatch.com)'s data.
- **Playback** is the service's own website, opened full screen in Chromium with Widevine. It opens at the title's own page when [Wikidata](https://www.wikidata.org) knows it, else at the service's search for the title.

## What they need

| Piece | Why | Where it comes from |
|---|---|---|
| A TMDB API key | The catalogue. | Free from themoviedb.org: [below](https://github.com/mehmetraif/OSD-OS/wiki/Netflix-and-Prime-Video#the-tmdb-api-key). |
| Chromium | Plays the service's website. OSD/OS looks for `chromium`, `chromium-browser`, `google-chrome-stable` and `google-chrome`, in that order. | The image has it. |
| Widevine (`libwidevinecdm0`) | The DRM module both services require. | The image has it. |
| `cage` | Gives the browser the screen when there is no desktop. | The image has it. |
| `wtype` | Lets holding back close the browser cleanly, so a fresh sign-in is kept. | The image has it. |
| A keyboard (or a mouse) | Works the service's own player, and signs in. | |

On **Raspberry Pi OS** (an app install):

```sh
sudo apt install chromium libwidevinecdm0 cage wtype
```

- The **OSD/OS image** has all of it (about 400 MB), unless it was built with `OSDOS_STREAMING=0` ([The OSD/OS image](https://github.com/mehmetraif/OSD-OS/wiki/The-OSD-OS-Image)).
- A **desktop** (Raspberry Pi OS with a desktop, SteamOS, another Linux) needs only the browser with Widevine.
- A **Mac** uses Google Chrome if it is in `/Applications`, and Safari otherwise.

Both modules are off by default: Settings → TV / Movie → **Netflix** (or **Prime Video**) → **Enabled**: On.

## The TMDB API key

Both modules read the same key, from `tmdb_api_key.txt` in the data folder:

| System | File |
|---|---|
| OSD/OS image, Raspberry Pi OS, SteamOS | `~/.local/share/OSD-OS/tmdb_api_key.txt` |
| macOS | `~/Library/Application Support/OSD-OS/tmdb_api_key.txt` |

To get a key, make a free account on [themoviedb.org](https://www.themoviedb.org), then ask for one in your account's **Settings → API**. Either of the two it shows works:

- the **API Key** (v3): 32 hexadecimal characters;
- the **API Read Access Token** (v4): a long token beginning `eyJ`.

The first line that is neither empty nor a `#` comment is the key. Written over SSH (not a real key):

```sh
printf '%s\n' '0123456789abcdef0123456789abcdef' > ~/.local/share/OSD-OS/tmdb_api_key.txt
```

A new key works at once, without a restart. Without one, Search, Movies and Series say **No TMDB API key: put one in tmdb_api_key.txt in the data folder (free from themoviedb.org, Settings, API)**.

## Country and language

Each module has its own:

- **Catalogue Region**: the country whose catalogue to list: TR (the default), US, GB, DE, FR, NL, ES, IT, CA or AU.
- **Catalogue Language**: English or Turkish, for titles, genres and stories. A story without a translation shows in English.

Only titles on the subscription are listed, not ones the service rents or sells.

## Browsing the catalogue

The catalogue uses the same tree, with the same keys, as Local Files ([Using OSD/OS](https://github.com/mehmetraif/OSD-OS/wiki/Using-OSD-OS)). On a title, select plays it and ► opens its info screen.

| Entry | What it holds |
|---|---|
| **Recently Watched** | The last 30 titles you opened, newest first. |
| **Favorites** | Titles you added from their options, newest first, at most 100. |
| **Search** | TMDB's first page of matches, films and series together, kept to those the service carries in your country. |
| **Movies** | **Popular**, then a folder per genre. |
| **Series** | The same, for series. |
| **Netflix Home** / **Prime Video Home** | The service's own home page (`https://www.netflix.com/browse`, `https://www.primevideo.com`). |

A list shows 20 titles, the most popular first, with **More…** for the next page, up to 500 pages. Titles carry their year: `Spirited Away (2001)`.

### Info screens

The info screen opens with ► on a title, or by itself once the cursor rests on one. **Settings → TV / Movie → Info Screen** sets when: after 1, 2, 3 or 5 seconds, **Key** for ► only, or **Off**.

- It shows the title, year and length (or seasons), the story, then GENRE, DIRECTOR or CREATOR, CAST and RATING (TMDB's average out of 10).
- Select plays. ▲ ▼ close it and move through the list. ◄ or back closes it. ► offers the options.

### Options

► on the info screen (or on a title, with Info Screen off):

- **Add to Favorites** / **Remove from Favorites**
- **Play at Startup** / **Don't Play at Startup**: OSD/OS opens this title straight after the boot screen. It goes on Favorites too.

Netflix and Prime Video titles can't go on a [playlist](https://github.com/mehmetraif/OSD-OS/wiki/Playlists).

## Opening a title

1. Select on a title. A screen says **Opening** and its name; back cancels.
2. Chromium opens the title's page full screen, or the service's search for its name when the page isn't known.
3. The service plays the title in its own player. The arrow keys move between links and titles; a mouse works too.

- If you aren't signed in, the service asks you to. It is easier to [sign in](https://github.com/mehmetraif/OSD-OS/wiki/Netflix-and-Prime-Video#signing-in-and-out) first, from Settings.
- A video left playing behind the menus (Transparent Background) ends when the browser opens.

## Coming back

| Do this | Keyboard | Gamepad | What happens |
|---|---|---|---|
| Hold back for two seconds | Esc | B (or Back) | OSD/OS closes the browser and returns to the tree where you were. |
| Close the browser | `Ctrl+W` (or `Alt+F4`) | | The same. |

Backspace and Right Shift don't count as back while the browser is open. On a Mac, quit the browser (`⌘Q`).

## Signing in and out

1. Settings → TV / Movie → **Netflix** (or **Prime Video**) → **Sign in**.
2. Have a keyboard ready, and press select. The service's sign-in page opens full screen.
3. Sign in with the keyboard.
4. Come back by holding back, or with `Ctrl+W`.

- Each service keeps its sign-in in its own Chromium profile in the data folder: `netflix/browser` and `prime_video/browser`.
- **Sign out** deletes that profile. It does nothing while the browser is open.
- Without `wtype` (or on a desktop), close the browser with `Ctrl+W` after signing in, or wait half a minute before holding back. Otherwise the sign-in may not be saved.

## Chromium, Widevine and cage

The browser is started by [scripts/web-player.sh](https://github.com/mehmetraif/OSD-OS/blob/main/scripts/web-player.sh):

```text
web-player.sh <service> <url> [letterbox|14:9|panscan|anamorphic]
web-player.sh --close <service>
```

- With a desktop (`WAYLAND_DISPLAY` or `DISPLAY` set), the browser opens over OSD/OS. Without one, it runs inside `cage`, which needs the user in the `video` and `input` groups.
- On an Arm board the browser tells the sites it is ChromeOS, which these players require. `OSDOS_WEB_PLAYER_UA` sets another user agent.
- **Scaling** enlarges the player's picture: 8/7 for 14:9, 4/3 for Pan & Scan, 4/3 taller for Anamorphic. Letterbox adds nothing. It doesn't apply on a Mac.

## Settings

Settings → TV / Movie → **Netflix**, with keys under `modules.com.osdos.netflix`. **Prime Video** has the same settings, under `modules.com.osdos.prime_video`.

| Setting | Values | Default | What it does | Config key |
|---|---|---|---|---|
| Enabled | On, Off | Off | Shows the module on the main menu. | `enabled` |
| Catalogue Region | TR, US, GB, DE, FR, NL, ES, IT, CA, AU | TR | The country whose catalogue to list. | `region` |
| Catalogue Language | English, Turkish | English | The language of titles, genres and stories. | `catalog_language` |
| Scaling | Default, Letterbox, 14:9, Pan & Scan, Anamorphic | Default | How a 16:9 picture fills a 4:3 screen in the service's player. Default follows Settings → TV / Movie → Scaling. | `video_scaling` |
| Sign in | Select | | Opens the service's sign-in page full screen. | |
| Sign out | Select | | Forgets the sign-in: deletes the profile. | |

The app's **Info Screen** (`app.info_screen`), **Play at Startup** (`app.startup_favorite`) and **Scaling** (`app.video_scaling`) apply too ([Settings](https://github.com/mehmetraif/OSD-OS/wiki/Settings)).

## Files and config keys

In the data folder:

| File | What it is |
|---|---|
| `tmdb_api_key.txt` | The TMDB key, shared by both modules |
| `lists.json` | Recently Watched (`recent`) and Favorites (`favorites`), under `com.osdos.netflix` and `com.osdos.prime_video` |
| `netflix/browser/`, `prime_video/browser/` | Each service's Chromium profile, with its sign-in |
| `netflix/scaling/`, `prime_video/scaling/` | The Scaling extension, rewritten for each run |
| `netflix/wayland`, `prime_video/wayland` | Where cage's socket is, while the browser is open |

A `config.json` fragment. Edit the file only while OSD/OS is closed.

```json
{
    "modules": {
        "com.osdos.netflix": {
            "enabled": true,
            "region": "GB",
            "catalog_language": "English",
            "video_scaling": "Default"
        },
        "com.osdos.prime_video": {
            "enabled": true,
            "region": "DE",
            "catalog_language": "English",
            "video_scaling": "Pan & Scan"
        }
    }
}
```

## Attribution

- This product uses the TMDB API but is not endorsed or certified by TMDB.
- Which service carries a title where comes from [JustWatch](https://www.justwatch.com), through TMDB.
- A title's page on each service is found through [Wikidata](https://www.wikidata.org).
- Netflix and Prime Video are their owners' services: OSD/OS only opens their websites.

See [Credits](https://github.com/mehmetraif/OSD-OS/wiki/Credits).

## Troubleshooting

| Message or symptom | What to do |
|---|---|
| **No TMDB API key: put one in tmdb_api_key.txt in the data folder.** | The file is missing, empty, or in the wrong folder. See [The TMDB API key](https://github.com/mehmetraif/OSD-OS/wiki/Netflix-and-Prime-Video#the-tmdb-api-key). |
| **TMDB turned the API key down: check tmdb_api_key.txt.** | Check that the whole key or token is on one line, with nothing else on it. |
| **Could not reach TMDB: check the network.** | No answer from `api.themoviedb.org`. |
| A folder is empty, or titles are missing. | Check Catalogue Region. Only titles on the subscription are listed. |
| **Could not open Netflix: Chromium is not installed** (or **cage is not installed**) | `sudo apt install chromium libwidevinecdm0 cage wtype`. |
| **web-player.sh is missing from the app** | Reinstall OSD/OS. |
| **The browser closed with error N**, or **The browser did not start** | The browser's last lines under it say why. Without a desktop, the user running OSD/OS must be in the `video` and `input` groups. |
| The screen stays black after selecting a title. | The browser can take several seconds to start on a Pi. If it stays black, hold back, then check `dpkg -l chromium libwidevinecdm0`. `sudo journalctl -u osdos -f` shows the app's log. |
| The service says it can't play protected content. | Install Widevine (`sudo apt install libwidevinecdm0`). If you set `OSDOS_WEB_PLAYER_UA`, unset it. |
| Signed out again after closing the browser. | See [Signing in and out](https://github.com/mehmetraif/OSD-OS/wiki/Netflix-and-Prime-Video#signing-in-and-out). |
| Holding back does nothing. | Hold Esc (or B on a gamepad), not Backspace, for the full two seconds. |
| No sound. | The browser plays through the card set in Settings → Sound → Audio Output ([Audio Output](https://github.com/mehmetraif/OSD-OS/wiki/Audio-Output)). |

More in [Troubleshooting](https://github.com/mehmetraif/OSD-OS/wiki/Troubleshooting).

## See also

[YouTube](https://github.com/mehmetraif/OSD-OS/wiki/YouTube), [The OSD/OS image](https://github.com/mehmetraif/OSD-OS/wiki/The-OSD-OS-Image), [Scripts](https://github.com/mehmetraif/OSD-OS/wiki/Scripts), [Using OSD/OS](https://github.com/mehmetraif/OSD-OS/wiki/Using-OSD-OS), [Controls](https://github.com/mehmetraif/OSD-OS/wiki/Controls), [Credits](https://github.com/mehmetraif/OSD-OS/wiki/Credits)
