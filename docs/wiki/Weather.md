# Weather

The weather, shown the way a cable weather channel showed it in the 90s: current conditions, a three-day forecast, other places and an almanac, in turn, with music under them. Forecasts come from [Open-Meteo](https://open-meteo.com), for anywhere in the world, without an account or a key.

<table>
<tr><th width="50%">Current conditions</th><th width="50%">Extended forecast</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/weather.png" width="100%" alt="Weather: current conditions in London" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/weather-forecast.png" width="100%" alt="Weather: the extended forecast for three days" /></td></tr>
</table>

## Setting it up

1. Settings → TV / Movie → **WEATHER** → **Enabled**: On. Weather appears on the main menu.
2. Write your location in `weather_location.txt` in the data folder (below).
3. Open **Weather** from the main menu.

Until the file is there, the module says what is missing (see [Troubleshooting](https://github.com/mehmetraif/OSD-OS/wiki/Weather#troubleshooting)).

## The location file

| System | File |
|---|---|
| OSD/OS image, Raspberry Pi OS, SteamOS | `~/.local/share/OSD-OS/weather_location.txt` |
| macOS | `~/Library/Application Support/OSD-OS/weather_location.txt` |

- The **first line** is your location, a place name or coordinates.
- **Each line after it** is another place for the Other Locations screen, which has room for about six.
- Blank lines are ignored. A line starting with `#` is a comment.
- **After changing the file, restart OSD/OS.** Places are looked up once per run.
- An extra place that can't be found is skipped.

### A place name

`Name, qualifier, qualifier…`. The part before the first comma is looked up in Open-Meteo's geocoder. The qualifiers choose among places of that name:

| Qualifier | Matches | Example |
|---|---|---|
| A country code | The place's ISO country code | `Istanbul, TR` |
| A country alias | USA, U.S.A., U.S., America, United States of America → US; UK, Britain, Great Britain, England, Scotland, Wales → GB; Holland → NL; UAE → AE; South Korea → KR | `London, UK` |
| A US state's abbreviation | The state | `Portland, ME` |
| A region's initials | A multi-word region (New South Wales → NSW, British Columbia → BC) | `Sydney, NSW` |
| A region or country, in full | The region or the country, as the geocoder names it | `Springfield, Illinois` |

- The place matching the most qualifiers wins; among equals, or with no match, the most populous. `Portland` alone is Portland, Oregon.
- Country codes are the safest qualifier. The geocoder calls Turkey "Republic of Türkiye", so `Istanbul, Turkey` matches nothing.

### Coordinates

`latitude, longitude` or `latitude, longitude, LABEL`, in decimal degrees. Use them for an ambiguous or tiny place. The screen shows the label in capitals; without one, it shows the coordinates (`42.3601, -71.0589`).

### An example

```text
# OSD/OS weather. The first line is the main location;
# each line after it is a place for the Other Locations screen.
Portland, ME, USA
London, UK
Istanbul, TR
Sydney, NSW
35.6762, 139.6503, TOKYO
Reykjavik
42.3601, -71.0589, BOSTON
```

## The screens

The screens follow one another in a loop, each up for the **Screen Time** (10 seconds by default). **Displays** chooses which are in the loop.

| Screen | What it shows |
|---|---|
| **Current Conditions** | `CURRENTLY IN <PLACE>`, the condition and the temperature, then HUMIDITY, DEWPOINT, PRESSURE, WIND and VISIBILITY. |
| **Extended Forecast** | Today and the next two days: the day's name, the condition, LO and HI. |
| **Other Locations** | Your other places: the name, the condition and the temperature. Only in the loop when the file lists extra places that were found. |
| **Almanac** | Sunrise and sunset for today and tomorrow, then MOON PHASES: the next new, first quarter, full and last quarter moons, with their dates. |

A strip runs along the foot of every screen: the date and a clock in the place's own time, then one reading after another, every five seconds.

| Action | Keyboard | Gamepad | What it does |
|---|---|---|---|
| Previous / next screen | ◄ ► | D-pad left, right | Steps through the loop, and pauses it there |
| Pause or resume the loop | Enter | A | Stays on the screen you are on, or carries on |
| Pause or resume the music | ▼ | D-pad down | For this visit only; the Music setting is unchanged |
| Leave | Esc | B | Back to the main menu; the music stops |

While the module is open, the screen saver stays away.

## Where the numbers come from

- **Open-Meteo**, for every place, fetched again every 10 minutes while the module is open.
- **The National Weather Service**, for US places: the latest observation from a station within 25 km, if under 75 minutes old, replaces Open-Meteo's current values.
- **The moon's phases** are worked out on the player.

## Units and the clock

| | Metric | US |
|---|---|---|
| Temperature | °C | °F |
| Wind | km/h | mph |
| Pressure | millibars (`1022.7 MB`) | inches of mercury (`30.20`) |
| Visibility | kilometres (`17 KM`) | miles (`11 MI.`) |

- Changing **Units** fetches the numbers again at once.
- **Hours Format** sets the clock and the almanac's sunrise and sunset: `14:52:58` and `07:12` with 24-hour, `2:52:58 PM` and `7:12 AM` with 12-hour.
- Hours Format and Screen Time take effect the next time you open the module.

## Background music

With **Music** on (the default), music plays while the weather is up: shuffled once, then looped.

- Without a list of your own, it streams eight tracks from the Weatherscan collection on the Internet Archive, so it needs the network.
- It plays on the card set in Settings → Sound → Audio Output. The menu music is held off meanwhile.

To play your own, list them in `weather_music.txt` in the data folder. It replaces the built-in list:

- one track per line: a file, or an `http://` or `https://` URL;
- a file is an absolute path, or a path relative to the data folder;
- spaces are fine, in paths and in URLs;
- `#` comments and blank lines are ignored;
- any format mpv plays: MP3, OGG, FLAC, WAV, M4A and more.

```text
# Music for OSD/OS's Weather module: shuffled once, then looped.
# A path relative to the data folder:
weather-music/Smooth Jazz 01.mp3
weather-music/Smooth Jazz 02.ogg
# An absolute path, here on the image's film partition:
/media/OSD-OS/Music/Lounge/Blue Hour.flac
# A URL:
https://archive.org/download/weatherscancompletecollection/02 Beach Frolic.mp3
```

## Settings

Settings → TV / Movie → **WEATHER**. The keys sit under `modules.com.osdos.weather`.

| Setting | Values | Default | What it does | Config key |
|---|---|---|---|---|
| Enabled | On, Off | Off | Shows Weather on the main menu. | `enabled` |
| Displays | Current Conditions, Extended Forecast, Other Locations, Almanac: each on or off | All on | Which screens are in the loop. With none on, Current Conditions alone. | `displays.current`, `.extended`, `.others`, `.almanac` |
| Music | On, Off | On | Background music while the forecast is up. | `music` |
| Units | Metric, US | Metric | See [Units](https://github.com/mehmetraif/OSD-OS/wiki/Weather#units-and-the-clock). | `units` |
| Screen Time | 10 SEC, 30 SEC, 60 SEC | 10 SEC | How long each screen stays up. | `screen_time` |
| Hours Format | 24-hour, 12-hour | 24-hour | The clock, sunrise and sunset. | `hours_format` |

## Files and config keys

| File | What it is |
|---|---|
| `weather_location.txt` | Your location, then your other places. You write it. |
| `weather_music.txt` | Optional: your own music list. OSD/OS never writes it. |

```json
{
    "modules": {
        "com.osdos.weather": {
            "enabled": true,
            "displays": {
                "current": true,
                "extended": true,
                "others": true,
                "almanac": false
            },
            "music": true,
            "units": "US",
            "screen_time": "30 SEC",
            "hours_format": "12-hour"
        }
    }
}
```

A screen missing from `displays` counts as on.

## Attribution

- Weather data by [Open-Meteo.com](https://open-meteo.com), under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
- Current observations for US places from the National Weather Service ([api.weather.gov](https://api.weather.gov)).
- The look is inspired by [WeatherStar 3000+](https://github.com/netbymatt/ws3kp) by netbymatt, after The Weather Channel's WeatherStar.
- The built-in music is from the Weatherscan collection on the [Internet Archive](https://archive.org/details/weatherscancompletecollection).

See [Credits](https://github.com/mehmetraif/OSD-OS/wiki/Credits).

## Troubleshooting

When the module can't start, it says why:

| Screen | Meaning | What to do |
|---|---|---|
| **NO LOCATION SET** | There is no `weather_location.txt` in the data folder. | Write it, as [above](https://github.com/mehmetraif/OSD-OS/wiki/Weather#the-location-file). Check the name (not `weather_location.txt.txt`) and the folder. |
| **LOCATION FILE IS EMPTY** | The file has no line that isn't blank or a comment. | Add your location on the first line. |
| **LOCATION FILE COULD NOT BE READ** | The file can't be opened. | The user running OSD/OS must be able to read it. |
| **LOCATION NOT FOUND** | The geocoder knows no place of that name. | Check the spelling of the part before the first comma, or use coordinates. |
| **CAN'T REACH THE LOCATION SERVICE** | The geocoder didn't answer. | Check the network. Back, then open Weather again. |

| Problem | What to do |
|---|---|
| The wrong town, or a place missing from Other Locations | Add qualifiers (`Portland, ME, USA`), or use coordinates with a label. The log says `skipping other location` for a place not found. |
| It stays on **LOADING...** | The forecast didn't arrive: check the network. It tries again every 10 minutes while open. |
| A change to the location file doesn't show | Restart OSD/OS. |
| No music | Check the Music setting, the network (the built-in tracks stream), the paths in `weather_music.txt`, and Settings → Sound → Audio Output. The log says `music process exited` or `mpv not found`. |
| The clock shows another time than the device | It shows the time where the weather is. |

The log on the image: `sudo journalctl -u osdos -f`. More in [Troubleshooting](https://github.com/mehmetraif/OSD-OS/wiki/Troubleshooting).

## See also

[Ambient:Mode](https://github.com/mehmetraif/OSD-OS/wiki/Ambient-Mode), [Menu music](https://github.com/mehmetraif/OSD-OS/wiki/Menu-Music), [Audio Output](https://github.com/mehmetraif/OSD-OS/wiki/Audio-Output), [Settings](https://github.com/mehmetraif/OSD-OS/wiki/Settings), [Configuration files](https://github.com/mehmetraif/OSD-OS/wiki/Configuration-Files), [Credits](https://github.com/mehmetraif/OSD-OS/wiki/Credits)
