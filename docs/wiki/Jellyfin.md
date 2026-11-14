# Jellyfin

The Jellyfin module plays films, series and home videos from a [Jellyfin](https://jellyfin.org) server. You sign in with **Quick Connect**: OSD/OS shows a code and you approve it on a device already signed in to Jellyfin. The [Emby](https://github.com/mehmetraif/OSD-OS/wiki/Emby) module is its twin.

<table>
<tr><th width="50%">Jellyfin</th><th width="50%">Add to Playlist</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/jellyfin.png" width="100%" alt="Jellyfin: Connect to Server, with the Server URL field and the Quick Connect button" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/add-to-playlist.png" width="100%" alt="Add to playlist? The playlists, then New Online Playlist and New Offline Playlist" /></td></tr>
</table>

## What you need

- A Jellyfin server the device can reach, and its address with the port, such as `http://192.168.1.20:8096`. Jellyfin's usual ports are 8096 (HTTP) and 8920 (HTTPS).
- **Quick Connect** turned on in the server's dashboard.
- A phone, tablet or computer signed in to Jellyfin as the user OSD/OS should play as.
- No keyboard: select on the address field opens the on-screen keyboard. A USB or Bluetooth keyboard works too.

## Turning it on

**Settings → TV / Movie → Jellyfin → Enabled**. Jellyfin then appears on the main menu. Its other settings show once you have signed in.

## Signing in with Quick Connect

1. Open **Jellyfin** from the main menu. **Connect to Server** has a **Server URL** field and a **Quick Connect** button.
2. Select on the field opens the on-screen keyboard. Enter the server's address with its port, as `http://192.168.1.20:8096`. OK goes on to **Quick Connect**.
3. Select on **Quick Connect**. The code fills the screen, with **Enter the above code at:** and an address ending `/web/#quickconnect`.
4. On your other device, open that address, enter the code and authorize it.
5. The screen reads **Approved** and the libraries open.

Back on the code screen cancels. OSD/OS appears in the server's list of devices as **OSD/OS**; removing it there signs OSD/OS out.

| Message | What it means |
|---|---|
| **Please enter a server URL** | The Server URL field is empty |
| **CONNECTION FAILED: …** | Nothing answered at that address: a typing slip, a missing `http://` or port, or the server is off or on another network |
| **CONNECTION FAILED: HOST REQUIRES AUTHENTICATION** | Quick Connect is off: turn it on in the Jellyfin dashboard |
| **INVALID RESPONSE FROM SERVER** | Something answered, but not a Jellyfin server |
| **CODE EXPIRED** | Two minutes passed without approval. Back (`[ESC]:RETRY`) returns to the first screen for a new code |
| **POLL FAILED: …** | The connection dropped while waiting, or the server forgot the code. Go back and start again |
| **Approved…**, and nothing after it | The sign-in failed after approval. Go back and start again |

### Sign in without a keyboard

Type the server's address in Settings → TV / Movie → **JELLYFIN** → **Server Address**. It can also go in `config.json` in the data folder (`~/.local/share/OSD-OS/` on Linux and the image, `~/Library/Application Support/OSD-OS/` on a Mac), with OSD/OS stopped:

```json
{
    "modules": {
        "com.osdos.jellyfin": {
            "enabled": true,
            "server_url": "http://192.168.1.20:8096"
        }
    }
}
```

- Merge it into the `modules` part of the file you have.
- **Connect to Server** then opens with the address filled in, and select starts Quick Connect.
- On the image: `sudo systemctl stop osdos` before editing, `sudo systemctl start osdos` after.
- Check the file with `python3 -m json.tool ~/.local/share/OSD-OS/config.json`. A file that isn't valid JSON is read as empty.

## The home screen

| Row | Shown when | What it opens |
|---|---|---|
| **Continue Watching** | You have something part-watched | Up to 20 part-watched films and episodes |
| **Next Up** | A series has a next episode for you | Up to 20 next episodes, one per series |
| Each library | It is a Movies, Shows, Home Videos or Collections library, and ON in **Settings → TV / Movie → Jellyfin → Libraries** | The library ([below](#browsing)) |

Music, book, photo and mixed-content libraries are left out. Back leaves Jellyfin.

## Browsing

An episode reads `SHOW S1E2: TITLE`, anything else `TITLE (YEAR)`. An empty list reads **NO ITEMS FOUND**.

| Library | How it is listed |
|---|---|
| Movies | Every film, A to Z by the server's sort name, with the letter panel |
| Shows | Every series, A to Z, with the letter panel |
| Home Videos | Its folders and videos one level at a time, A to Z with the letter panel. Photos and audio are left out |
| Collections | Every collection, A to Z, with the letter panel |

A collection opens on the kinds it holds (**MOVIES**, **SERIES**, **EPISODES**, **COLLECTIONS**), each listed oldest first. A collection of one kind opens its list straight away.

### Browsing by letter

The A-to-Z lists have a column of letters to the right:

- ► moves to the letters (the hint bar shows `[►]:BROWSE`);
- ▲ ▼ jump from letter to letter, the list following;
- select, ◄ or back return to the list.

Titles that don't start with a letter are under `#`.

## Series and seasons

A series' page has **PLAY ►** (**RSUM ►** with a resume point) and its **Seasons**. A season's page has the same button and its **Episodes**.

| PLAY on | Opens the page of |
|---|---|
| A series | The episode Jellyfin's Next Up gives; for a series never started, the first episode of season 1 |
| A season | Its part-watched episode; failing that, its first unwatched one; failing that, its first |

## An item's page

The page shows **PLAY ►** (**RSUM ►** with a resume point), the name, year, running time and overview, then under **Playback Settings:**

| Row | What it does |
|---|---|
| **Audio** | ◄ ► choose the audio track, when the file has more than one |
| **Subtitles** | ◄ ► choose a subtitle track, or **Off** (shown when the file has subtitles) |

- Select on PLAY plays. Back cancels the loading screen.
- ► on PLAY puts the video on a playlist ([below](#putting-a-video-on-a-playlist)); the hint bar shows `[►]:PLAYLIST`.
- The tracks start as your Jellyfin user's language preferences and subtitle mode say.

## Playing

### Direct Play and transcoding

**Settings → TV / Movie → Jellyfin → Video Quality**:

| Video Quality | What happens |
|---|---|
| **Direct Play** (the default) | The file is streamed as it is. A file the server can't send directly, or mpv can't play, comes as a transcode |
| **480p (NTSC CRT)** | A transcode, at most 480 lines and 4 Mbps |
| **576p (PAL CRT)** | A transcode, at most 576 lines and 4.5 Mbps |
| **720p** | A transcode, at most 720 lines and 6 Mbps |
| **1080p** | A transcode, at most 1080 lines and 10 Mbps |

A transcode carries the audio track you chose, with the subtitle you chose burned in.

### Resume, Continue Watching and Next Up

Resume points are the server's, shared with every Jellyfin app.

- **Resume Playback: Ask** (the default): a video with a resume point asks **Resume playback?** `Resume from 0:30` / `Start from the beginning`.
- **Resume Playback: Always**: it carries on without asking.

### Autoplay Next Episode

With **Autoplay Next Episode** on, an episode that plays to its end is followed by the next one of the series, across seasons, with the same audio and subtitle languages. With no next episode, the player returns to the page.

### Tracks during playback

▲ or ▼ during a video opens the deck's menu: the position bar, **AUDIO**, **SUBTITLE**, **CROP** and **STOP** ([Playback and mpv](https://github.com/mehmetraif/OSD-OS/wiki/Playback-and-mpv)).

- **Direct Play:** **AUDIO** and **SUBTITLE** step through the file's tracks.
- **A transcode:** each press restarts the stream where it was, with the next track. Subtitles step through to Off; audio has no Off.

### Intro Skip and Credit Skip

The settings use the intro and credits segments the server marks in an episode. They need the Jellyfin Intro Skipper plugin, and show only when the server offers media segments.

| Setting | What happens when an intro or credits segment starts |
|---|---|
| **Off** (the default) | Nothing |
| **Auto** | It is skipped, once per episode |
| **Button** | The deck's menu opens with **SKIP** under the cursor: select skips, or let it play. ▲ or ▼ brings the menu back while the segment lasts |

### With Transparent Background

With **Settings → TV / Movie → Transparent Background** on, back during a video returns to Jellyfin's menus while the picture plays on behind them. A transcode may end soon after. Choose the video again to play it full screen, from its resume point.

## Putting a video on a playlist

With the [Playlists](https://github.com/mehmetraif/OSD-OS/wiki/Playlists) module on, ► on **PLAY** asks **Add to playlist?**: your playlists (an offline one marked `(Offline)`), then **New Online Playlist** and **New Offline Playlist**.

- The window answers **Added to …** (and **It downloads in the background, once** for an offline list) or **Already on …**.
- A series or season can't go on as a whole: add its episodes, or use the Playlists module's **Add Videos**.
- A playlist always uses the original file: streamed on an **online** list, downloaded once on an **offline** one.
- A user without the right to download shows **Not Allowed**.
- A playlist reports nothing to the server: resume points and watched marks stay as they were.

## Settings

Settings → TV / Movie → Jellyfin. Everything but Enabled and Scaling shows only once you have signed in.

| Setting | Values | Default | What it does | Config key |
|---|---|---|---|---|
| Enabled | ON, OFF | OFF | Shows Jellyfin on the main menu | `enabled` |
| Libraries | each library, ON or OFF | all ON | Which libraries the home screen lists | `libraries` |
| Video Quality | Direct Play, 480p (NTSC CRT), 576p (PAL CRT), 720p, 1080p | Direct Play | Direct Play, or a transcode capped at that size | `video_quality` |
| Resume Playback | Ask, Always | Ask | Whether a video with a resume point asks, or carries on | `resume_playback` |
| Autoplay Next Episode | ON, OFF | OFF | Plays the series' next episode when one ends | `autoplay_next_episode` |
| Intro Skip | Off, Auto, Button | Off | What happens at an intro segment | `intro_skip` |
| Credit Skip | Off, Auto, Button | Off | What happens at a credits segment | `outro_skip` |
| Scaling | Default, Letterbox, 14:9, Pan & Scan, Anamorphic | Default | How 16:9 fills the 4:3 screen in Jellyfin; Default follows Settings → TV / Movie → Scaling | `video_scaling` |
| Sign out | (an action) | | Revokes the token on the server and forgets it here | |

### How the values are saved

The keys sit under `modules.com.osdos.jellyfin`, nested as in the fragment [above](#sign-in-without-a-keyboard). See [Configuration Files](https://github.com/mehmetraif/OSD-OS/wiki/Configuration-Files).

| Key | Saved as |
|---|---|
| `enabled`, `autoplay_next_episode` | `true` or `false` |
| `libraries` | an object of `"<library id>": true` or `false`; a library left out counts as ON |
| `video_quality` | `"auto"` (Direct Play), `"480p"`, `"576p"`, `"720p"` or `"1080p"` |
| `resume_playback` | `"ask"` or `"always"` |
| `intro_skip`, `outro_skip` | `"Off"`, `"Auto"` or `"Button"` |
| `video_scaling` | the option's name, such as `"Pan & Scan"` |
| `server_url` | the server address: Settings → TV / Movie → JELLYFIN → Server Address, and the sign-in screen |

## How the sign-in is stored

`jellyfin_auth.json` in the data folder, readable by your user only (mode 600), holds the server's address, the access token and the user.

- Your password is never asked for.
- **Sign out** deletes the file.
- A self-signed certificate is accepted for the server's own address.

## Troubleshooting

Sign-in messages are listed [above](#signing-in-with-quick-connect).

| Problem | What to do |
|---|---|
| **CONNECTION FAILED: …** | Check the address has `http://` or `https://` and the port. From the device, `curl http://192.168.1.20:8096/System/Info/Public` (with your address) should answer with the server's name |
| Back on the sign-in screen by itself | The device was removed in the dashboard. Sign in again |
| The home screen stays on **LOADING...** | The server didn't answer (the log has `LOAD LIBRARIES FAILED`), or no library is ON in Settings → TV / Movie → Jellyfin → Libraries |
| No Continue Watching or Next Up | They show only when they have something |
| No Intro Skip or Credit Skip rows | The server has no media segments (install the Intro Skipper plugin), or you aren't signed in |
| A video stutters | Choose a CRT tier in Video Quality |
| Changing subtitles restarts the video | It is a transcode; Direct Play switches without a restart |
| Offline playlist says **Not Allowed** | An administrator must allow your Jellyfin user to download media. Then **Retry Downloads** on the playlist's page |

Logs: `journalctl -u osdos -b | grep -i jellyfin`. mpv's log is `/tmp/runtime-pi/osdos-mpv.log` on the image ([elsewhere](https://github.com/mehmetraif/OSD-OS/wiki/Configuration-Files#logs-and-temporary-files)). See [Troubleshooting](https://github.com/mehmetraif/OSD-OS/wiki/Troubleshooting).

## See also

[Emby](https://github.com/mehmetraif/OSD-OS/wiki/Emby), [Plex](https://github.com/mehmetraif/OSD-OS/wiki/Plex), [Playlists](https://github.com/mehmetraif/OSD-OS/wiki/Playlists), [Playback and mpv](https://github.com/mehmetraif/OSD-OS/wiki/Playback-and-mpv), [Settings](https://github.com/mehmetraif/OSD-OS/wiki/Settings), [Modules](https://github.com/mehmetraif/OSD-OS/wiki/Modules)
