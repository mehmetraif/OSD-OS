# Plex

The Plex module plays films and series from a [Plex Media Server](https://www.plex.tv). You sign in with a code at plex.tv/link, pick a Plex Home profile and a server, then browse the libraries as Plex arranges them.

<table>
<tr><th width="50%">Signing in</th><th width="50%">Resume</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/plex-sign-in.png" width="100%" alt="Plex sign in: a code, and Visit plex.tv/link and enter the code above" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/resume.png" width="100%" alt="Resume playback? Resume from 1:12:34, or start from the beginning" /></td></tr>
</table>

## What you need

- A Plex account and a Plex Media Server it can use (yours, or one shared with you), reachable on your network or over the internet.
- A phone or computer to enter the code on. The TV needs no keyboard, not even for a profile PIN.

## Turning it on

**Settings → TV / Movie → Plex → Enabled**. Plex then appears on the main menu. Its other settings show once you have signed in.

## Signing in

1. Open **Plex** from the main menu. It shows a four-character code and **Visit plex.tv/link and enter the code above**.
2. On a phone or computer, go to **plex.tv/link**, sign in to your Plex account if asked, and enter the code.
3. **Select User** lists the profiles. An account with one profile skips this step. A profile with a PIN asks for it ([below](#profiles)).
4. **Select Server** lists the servers that profile can use. Choose one.

- A code left unentered for long expires: **THE CODE EXPIRED: GO BACK AND TRY AGAIN**. Open Plex again for a fresh one.
- The device appears in your Plex account's authorized devices as **OSD/OS**. Removing it there signs OSD/OS out.

## Profiles

OSD/OS plays as one Plex Home profile at a time.

- **Auto Sign In** off (the default): **Select User** comes up every time you open Plex. On: Plex opens on the libraries as the profile chosen last.
- **Settings → TV / Movie → Plex → Current User** also switches profile. A profile with a PIN asks for it the next time you open Plex.
- **A PIN** is four digits: ▲ ▼ change the digit under the cursor, ◄ ► move between the four, select submits. A keyboard's digit keys work too. A wrong PIN shows **INCORRECT PIN**. Back cancels. The PIN is never saved.
- **Managed users** see only the servers shared with them.
- **NFC cards never switch profiles** ([Writing an NFC card](#writing-an-nfc-card)).

## Servers

At sign-in OSD/OS tries each server's addresses in this order, 3 seconds each, and keeps the first that answers:

1. local addresses (on your network);
2. remote addresses (the server's own address on the internet);
3. relay addresses (Plex's relay service).

The address that answers is saved with the sign-in. If the server no longer answers there as its libraries are read (a new IP address, a new router), OSD/OS asks plex.tv for its addresses again and keeps the first that answers.

To use another server:

- On the Plex home screen, ◄ or ► opens **Select Server** when the profile has more than one (the hint bar shows `[◄►]:SERVER`).
- Or **Settings → TV / Movie → Plex → Server**.

## The home screen

| Row | Shown when | What it opens |
|---|---|---|
| **Continue Watching** | The server has something to carry on with | What this profile has part-watched and what comes next |
| **Live TV** | The server has a DVR with a channel lineup | The channel list ([Live TV](#live-tv)) |
| Each library | It is a film, TV or other-video library and is ON in **Settings → TV / Movie → Plex → Libraries** | The library's own menu |

Music and photo libraries are left out. Back leaves Plex.

## Inside a library

| Row | What it lists |
|---|---|
| **Recommended** | The library's hubs, as Plex arranges them (Recently Added and the like) |
| **Library** | Everything in the library, sorted by title, with a letter panel |
| **Collections** | The library's collections |
| **Playlists** | The server's video playlists that hold this library's titles |
| **Categories** | The server's filters for the library (genre, year, director and the like): a filter, then a value, then its titles |
| **Folders** | The library's folders as on the server's disk |

An episode reads `SHOW S1E2: TITLE`. A film with an edition carries it (`BLADE RUNNER (FINAL CUT)`).

### Browsing by letter

**Library**, and an alphabetical **Folders** listing, have a column of letters to the right:

- ► moves to the letters (the hint bar shows `[►]:BROWSE`);
- ▲ ▼ jump from letter to letter, the list following;
- select, ◄ or back return to the list.

Titles are filed under the first letter of their sort title; anything else under `#`.

### Collections and playlists: Play All and Shuffle

A collection or playlist with more than one title, or a single show, starts with:

| Row | What it does |
|---|---|
| **» PLAY ALL** | Plays everything in order. A show plays all its episodes |
| **~ SHUFFLE** | The same, shuffled, with episodes of different shows mixed |
| **@ WRITE NFC CARD** | Writes a card that plays the set ([below](#writing-an-nfc-card)) |

Every title starts from its beginning. The queue goes on to the next title whatever Autoplay Next Episode says, and is cut at 2000 episodes.

## Shows and seasons

A show's page has **PLAY ►** (**RSUM ►** with a resume point), **VIEW EXTRAS**, **WRITE NFC CARD** and its **Seasons**. A season's page is the same, with its **Episodes**.

**PLAY** on a show or a season asks **What would you like to play?**:

| Choice | What it does |
|---|---|
| **Play Next Episode** (**Resume Next Episode** when one is part-watched) | Opens the page of the part-watched episode; failing that, the first unwatched one; failing that, the first. On a season, within that season |
| **Shuffle Episodes** | Plays random episodes from the show or season, each from its beginning |

**Shuffle Episodes** reports nothing to Plex: watched marks and Continue Watching stay as they were. It goes on to another episode only while **Autoplay Next Episode** is on.

## A film's or an episode's page

The page shows **PLAY ►** (or **RSUM ►** with a saved position), the name, year, running time and summary, then:

| Row | What it does |
|---|---|
| **VIEW EXTRAS** | Trailers, deleted scenes and the like. Shown when there are extras |
| **WRITE NFC CARD** | Writes a card for this film or episode. Shown while the NFC Reader module is on and a reader is connected |
| **Audio** | ◄ ► choose the audio track |
| **Subtitles** | ◄ ► choose a subtitle track, or **OFF** |

PLAY saves the track choice back to Plex, so every Plex app plays the file the same way.

## Playing

### Direct Play and transcoding

**Settings → TV / Movie → Plex → Video Quality**:

| Video Quality | What happens |
|---|---|
| **Direct Play** (the default) | The file is streamed as it is. A file mpv can't play is transcoded by itself |
| **8 Mbps (1080p)**, **4 Mbps (720p)**, **2 Mbps (480p)** | The server re-encodes the video, capped at that bitrate, with the audio track you chose and the subtitle burned in. For a weak network |

### Resume and Continue Watching

Resume points are Plex's own, shared with every Plex app.

- **Resume Playback: Ask** (the default): a video with a resume point asks **Resume playback?** `Resume from 0:30` / `Start from the beginning`.
- **Resume Playback: Always**: it carries on without asking.

### Autoplay Next Episode

With **Autoplay Next Episode** on, an episode that plays to its end is followed by the next one of the same season, with the same audio and subtitle languages. After the last episode of a season, the page returns.

### Audio and subtitles during playback

▲ or ▼ during a video opens the deck's menu: the position bar, **AUDIO**, **SUBTITLE**, **CROP** and **STOP** ([Playback and mpv](https://github.com/mehmetraif/OSD-OS/wiki/Playback-and-mpv)).

- In Direct Play, **AUDIO** and **SUBTITLE** step through the file's tracks.
- A transcode carries only the tracks chosen on the page, so choose them there before PLAY.
- With subtitles **OFF**, forced subtitles still show.

### With Transparent Background

With **Settings → TV / Movie → Transparent Background** on, back during a video returns to Plex's menus while the picture plays on behind them. A transcode may end soon after. Choose the title again to play it full screen, from its resume point.

## Live TV

When the server has a DVR with a channel lineup, **Live TV** sits on the home screen after Continue Watching.

- Select a channel to watch it (**TUNING …** while the server tunes it).
- Back stops it. To change channel, go back to the list.
- There is nothing for recordings or the guide.

## Writing an NFC card

With the [NFC Reader](https://github.com/mehmetraif/OSD-OS/wiki/NFC-Reader) module on and a reader connected, Plex pages offer **WRITE NFC CARD**. Select it and hold a card to the reader. The steps, and the file kept per card, are in [NFC Reader](https://github.com/mehmetraif/OSD-OS/wiki/NFC-Reader#writing-a-card-from-plex).

| From | The card plays | It asks |
|---|---|---|
| A film's or an episode's page | That film or episode | **Write card** |
| A show's or a season's page | The episode on deck (part-watched, else the first unwatched), or random episodes | **Write card (Sequential Episodes)** or **Write card (Shuffle Episodes)** |
| A collection's or playlist's list (**@ WRITE NFC CARD**) | The whole set, as PLAY ALL or SHUFFLE would | **Write card (In Order)** or **Write card (Shuffled)** |

A card plays as whoever is signed in. It never signs in, switches profile or switches server. When it can't play, the NFC Reader's screen says why; select tries again.

| Message | What to do |
|---|---|
| **NOT SIGNED IN TO PLEX** | Open the Plex module and sign in |
| **NO PLEX SERVER SELECTED** | Open the Plex module and choose a server |
| **THIS PLEX PROFILE NEEDS ITS PIN — OPEN THE PLEX MODULE TO SIGN IN** | Enter the PIN in the Plex module |
| **NOT FOUND ON THIS SERVER** | The title is on another server, was deleted, or the profile in use can't see it |
| **PLEX SERVER UNREACHABLE** | Check the server and the network |
| **NO EPISODES FOUND**, **THIS ITEM HAS NO PLAYABLE MEDIA**, **UNSUPPORTED ITEM TYPE**, **COULD NOT LOAD ITEM**, **THIS CARD HAS NO PLEX ITEM** | Write the card again from a title that plays |

## Settings

Settings → TV / Movie → Plex. Everything but Enabled and Scaling shows only once you have signed in.

| Setting | Values | Default | What it does | Config key |
|---|---|---|---|---|
| Enabled | ON, OFF | OFF | Shows Plex on the main menu | `enabled` |
| Current User | the account's Plex Home profiles | the one chosen at sign-in | Switches profile; a PIN is asked the next time you open Plex | `current_user_id` |
| Auto Sign In | ON, OFF | OFF | ON opens Plex on the libraries as the current profile | `auto_sign_in` |
| Server | the servers the profile can use | the one chosen at sign-in | The server Plex browses and plays from | `server_machine_id` |
| Libraries | each library, ON or OFF | all ON | Which libraries the home screen lists | `libraries` |
| Video Quality | Direct Play, 8 Mbps (1080p), 4 Mbps (720p), 2 Mbps (480p) | Direct Play | Direct Play, or a transcode capped at that bitrate | `video_quality` |
| Resume Playback | Ask, Always | Ask | Whether a video with a resume point asks, or carries on | `resume_playback` |
| Autoplay Next Episode | ON, OFF | OFF | Plays the season's next episode when one ends | `autoplay_next_episode` |
| Scaling | Default, Letterbox, 14:9, Pan & Scan, Anamorphic | Default | How 16:9 fills the 4:3 screen in Plex; Default follows Settings → TV / Movie → Scaling | `video_scaling` |
| Sign out | (an action) | | Signs out of Plex on this device | |

### How the values are saved

The keys sit under `modules.com.osdos.plex`.

| Key | Saved as |
|---|---|
| `enabled`, `auto_sign_in`, `autoplay_next_episode` | `true` or `false` |
| `current_user_id` | the profile's Plex user id, as a string |
| `server_machine_id` | the server's machine identifier |
| `libraries` | an object of `"<machine id>_<library number>": true` or `false`; a library left out counts as ON |
| `video_quality` | `"auto"` (Direct Play), `"8000"`, `"4000"` or `"2000"` (kbps). Unset means Direct Play |
| `resume_playback` | `"ask"` or `"yes"` (Always) |
| `video_scaling` | the option's name, such as `"Pan & Scan"` |

A `config.json` fragment (the ids are examples of their shape):

```json
{
    "modules": {
        "com.osdos.plex": {
            "enabled": true,
            "current_user_id": "12345678",
            "auto_sign_in": true,
            "server_machine_id": "1a2b3c4d5e6f7a8b9c0d1e2f3a4b5c6d7e8f9a0b",
            "libraries": {
                "1a2b3c4d5e6f7a8b9c0d1e2f3a4b5c6d7e8f9a0b_1": true,
                "1a2b3c4d5e6f7a8b9c0d1e2f3a4b5c6d7e8f9a0b_3": false
            },
            "video_quality": "auto",
            "resume_playback": "ask",
            "autoplay_next_episode": true,
            "video_scaling": "Pan & Scan"
        }
    }
}
```

Safe to change by hand, with OSD/OS stopped: `enabled`, `auto_sign_in`, `video_quality`, `resume_playback`, `autoplay_next_episode` and `video_scaling`. Change profile and server in Settings. See [Configuration Files](https://github.com/mehmetraif/OSD-OS/wiki/Configuration-Files).

## How the sign-in is stored

Two files in the data folder (`~/.local/share/OSD-OS/` on Linux and the image, `~/Library/Application Support/OSD-OS/` on a Mac, or `$DATA_ROOT`), readable by your user only (mode 600):

| File | What it holds |
|---|---|
| `plex_key.pem` | A private key made for this device at sign-in. It renews the sign-in without your password |
| `plex_auth.json` | The account's token, the profiles, the servers found at sign-in and the profile and server in use |

**Sign out** asks plex.tv to forget the device, then deletes both files. Profile PINs and your Plex password are never stored.

## Troubleshooting

| Problem | What to do |
|---|---|
| **Requesting Pin…** stays, or **PIN REQUEST FAILED: …** | The device can't reach plex.tv: check the network |
| The code was entered but it still says **Waiting…** | The code may have expired. Back out, open Plex again and enter the fresh code |
| **FAILED TO LOAD ACCOUNT: …** | plex.tv did not return the account's profiles; try again |
| **Select Server** is empty | Check the server is running and signed in to the same account (or shared with it) |
| **NO SERVER CONFIGURED**, or **LOAD LIBRARIES FAILED: …** | The server is off or out of reach: its saved address and those plex.tv has for it now were tried. Check the server and the network and open Plex again; if it stays, **Settings → TV / Movie → Plex → Sign out**, then sign in again |
| **SERVER REJECTED THIS DEVICE. PLEASE TRY AGAIN.** | Try again; if it stays, sign out and in |
| **LOADING...** for half a minute, then an error such as **LOAD LIBRARY FAILED: …** | The server stopped answering (a NAS asleep, the Wi-Fi dropped); a request is given up after 30 seconds. Wake the server or check the network, then open the list again |
| **PLEX DID NOT ISSUE A SERVER TOKEN FOR THIS DEVICE…** | Sign out and sign in again |
| Back on the sign-in screen by itself | The device was removed from your Plex account, or its sign-in couldn't be renewed. Sign in again |
| A library is missing | Settings → TV / Movie → Plex → Libraries; music and photo libraries are never listed |
| A video stutters or buffers | On a weak or remote connection, pick a lower **Video Quality**. With a transcode, the server's processor may be the limit: try **Direct Play**. See [Playback and mpv](https://github.com/mehmetraif/OSD-OS/wiki/Playback-and-mpv) |
| Certificate errors | Plex's own `*.plex.direct` certificates are accepted. With any other HTTPS address, check the server's certificate |

Logs: `journalctl -u osdos -b | grep -i plex` on the image or a Raspberry Pi install (lines start `[PlexAuth]`, `[PlexBackend]` or `[Plex]`). mpv's log is `/tmp/runtime-pi/osdos-mpv.log` on the image ([elsewhere](https://github.com/mehmetraif/OSD-OS/wiki/Configuration-Files#logs-and-temporary-files)). See [Troubleshooting](https://github.com/mehmetraif/OSD-OS/wiki/Troubleshooting).

## See also

[Jellyfin](https://github.com/mehmetraif/OSD-OS/wiki/Jellyfin), [Emby](https://github.com/mehmetraif/OSD-OS/wiki/Emby), [NFC Reader](https://github.com/mehmetraif/OSD-OS/wiki/NFC-Reader), [Playback and mpv](https://github.com/mehmetraif/OSD-OS/wiki/Playback-and-mpv), [Settings](https://github.com/mehmetraif/OSD-OS/wiki/Settings), [Modules](https://github.com/mehmetraif/OSD-OS/wiki/Modules), [Writing a Module](https://github.com/mehmetraif/OSD-OS/wiki/Writing-a-Module) (`PlexBackend` is the reference backend)
