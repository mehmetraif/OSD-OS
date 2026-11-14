# Emby

The Emby module plays films, series and home videos from an [Emby](https://emby.media) server. You sign in with the server's own username and password, or with your **Emby Connect** account. Once signed in, it works like the [Jellyfin](https://github.com/mehmetraif/OSD-OS/wiki/Jellyfin) module, screen for screen.

<table>
<tr><th width="50%">Emby</th><th width="50%">Resume</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/emby.png" width="100%" alt="Emby: Connect to Server, with Local Server or Emby Connect, Server URL, Username, Password and Sign In" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/resume.png" width="100%" alt="Resume playback? Resume from 1:12:34, or start from the beginning" /></td></tr>
</table>

## What you need

- An Emby server the device can reach.
- For **Local Server**: its address with the port (Emby's defaults are 8096 for HTTP, 8920 for HTTPS) and a user account on it.
- For **Emby Connect**: an emby.media account linked to the server.
- No keyboard: select on a field opens the on-screen keyboard. A USB or Bluetooth keyboard works too.

## Turning it on

**Settings → TV / Movie → Emby → Enabled**. Emby then appears on the main menu. Its other settings show once you have signed in.

## Signing in

Open **Emby** from the main menu. **Connect to Server** starts with **LOCAL SERVER** or **EMBY CONNECT**. ◄ ► choose one; Enter or ▼ moves into the form:

| | Local Server | Emby Connect |
|---|---|---|
| **Server URL** | The server's address, with `http://` or `https://` and the port | (not asked) |
| **Username** / **Email** | Your username on that server | Your emby.media email (or user name) |
| **Password** | Its password (empty for a user without one) | Your emby.media password |

- ▲ ▼ (or Tab and Shift+Tab) move from row to row. Esc leaves the screen.
- Select on a field opens the on-screen keyboard. For the password, **SHOW** shows what was typed and **HIDE** hides it.
- OK goes on to the next field, then to **Sign In**. Select there signs in.

### Local Server

The username and password go to the server only. The password is never stored. Use an `https://` address on a network you don't trust.

### Emby Connect

- With more than one server on the account, **Choose a Server** lists them. Select one.
- A server is tried at its address on your network first, then at its address on the internet: a server in another house, or one in a container that reports the container's own address.
- If it gives no sign-in at either, the error shows on the same list, to pick another.
- After sign-in Emby Connect isn't needed again.

### What the sign-in screen can say

| Message | What it means |
|---|---|
| **PLEASE ENTER A SERVER URL**, **PLEASE ENTER A USERNAME**, **ENTER YOUR EMBY CONNECT EMAIL** | A field is empty |
| **INCORRECT USERNAME OR PASSWORD** | The server refused them. Use the server's own account, not the emby.media one |
| **SIGN IN FAILED (HTTP …)** | The server refused the sign-in with that status |
| **CONNECTION FAILED: …** | Nothing answered: a typing slip, a missing `http://` or port, the server is off, or (Emby Connect) no internet |
| **INVALID AUTH RESPONSE** | Something answered, but not an Emby server |
| **INCORRECT EMBY CONNECT EMAIL OR PASSWORD** | emby.media refused them |
| **EMBY CONNECT SIGN IN FAILED (HTTP …)**, **INVALID EMBY CONNECT RESPONSE** | emby.media refused or answered oddly |
| **FAILED TO LOAD SERVERS: …** | The account's server list couldn't be fetched |
| **NO SERVERS LINKED TO THIS ACCOUNT** | Link the server to your emby.media account in the server's user settings, or use Local Server |
| **SERVER EXCHANGE FAILED (HTTP …)**, **INVALID EXCHANGE RESPONSE** | The chosen server refused the account, or answered at neither of its addresses (HTTP 0). Pick another, or use Local Server |
| **INVALID SERVER SELECTION** | Emby Connect listed the server without an access key |

### Fill in the address beforehand

For Local Server, type the address in Settings → TV / Movie → **EMBY** → **Server Address**. It can also go in `config.json` in the data folder (`~/.local/share/OSD-OS/` on Linux and the image, `~/Library/Application Support/OSD-OS/` on a Mac), with OSD/OS stopped:

```json
{
    "modules": {
        "com.osdos.emby": {
            "enabled": true,
            "server_url": "http://192.168.1.30:8096"
        }
    }
}
```

- Merge it into the `modules` part of the file you have.
- On the image: `sudo systemctl stop osdos` before editing, `sudo systemctl start osdos` after.
- Check the file with `python3 -m json.tool ~/.local/share/OSD-OS/config.json`. A file that isn't valid JSON is read as empty.

In the server's list of devices, OSD/OS appears under the machine's host name, with **OSD/OS** as the app. Removing it there signs OSD/OS out.

## The same as Jellyfin

These work as in the Jellyfin module, whose page has the detail:

- [The home screen](https://github.com/mehmetraif/OSD-OS/wiki/Jellyfin#the-home-screen): **Continue Watching** and **Next Up** (up to 20 each), then the libraries. Movies, Shows, Home Videos and Collections libraries show when ON in **Settings → TV / Movie → Emby → Libraries**.
- [Browsing](https://github.com/mehmetraif/OSD-OS/wiki/Jellyfin#browsing): films and series A to Z, home videos folder by folder, collections by kind.
- [Browsing by letter](https://github.com/mehmetraif/OSD-OS/wiki/Jellyfin#browsing-by-letter): ► to the letters in the Movies, Shows and Home Videos lists.
- [Series and seasons](https://github.com/mehmetraif/OSD-OS/wiki/Jellyfin#series-and-seasons): PLAY opens the next episode to watch.
- [An item's page](https://github.com/mehmetraif/OSD-OS/wiki/Jellyfin#an-items-page): **PLAY ►** / **RSUM ►**, **Audio** and **Subtitles**, ► on PLAY for a playlist.
- [Video Quality](https://github.com/mehmetraif/OSD-OS/wiki/Jellyfin#direct-play-and-transcoding): Direct Play, or a transcode at 480p (NTSC CRT), 576p (PAL CRT), 720p or 1080p (4, 4.5, 6 or 10 Mbps).
- [Resume and Continue Watching](https://github.com/mehmetraif/OSD-OS/wiki/Jellyfin#resume-continue-watching-and-next-up): the server's resume points; **Resume Playback** Ask or Always.
- [Autoplay Next Episode](https://github.com/mehmetraif/OSD-OS/wiki/Jellyfin#autoplay-next-episode): the next episode of the series, across seasons.
- [With Transparent Background](https://github.com/mehmetraif/OSD-OS/wiki/Jellyfin#with-transparent-background): back returns to the menus with the picture behind them.
- [Putting a video on a playlist](https://github.com/mehmetraif/OSD-OS/wiki/Jellyfin#putting-a-video-on-a-playlist): ► on PLAY, online or offline.

## Where Emby differs

| | Jellyfin | Emby |
|---|---|---|
| Signing in | Quick Connect | Username and password, or Emby Connect |
| An episode in a list | `SHOW S1E2: TITLE` | `SHOW: TITLE`, with `(YEAR)` when the episode has one |
| The Collections library's list | With the letter panel | Without it |
| Text subtitles with Direct Play | Every text track | Only the one you chose ([below](#subtitles)) |
| Text subtitles in a transcode | Burned into the picture | Fetched as a file ([below](#subtitles)) |
| AUDIO and SUBTITLE in the deck's menu during a transcode | Ask the server for a new transcode | Step through what the stream carries |
| Dolby Vision on Direct Play | As the server decides | An SDR transcode ([below](#dolby-vision)) |
| Intro and credit skipping | The server's media segments; settings shown only when it has them | The server's chapter markers; settings always shown ([below](#intro-skip-and-credit-skip)) |
| Sign out | Ends the session | Also removes the device from the server |
| The token | In a request header | Also in the stream's address (`api_key`): treat mpv's log as private |

## Subtitles

- **Direct Play:** only the text subtitle (SRT, ASS, WebVTT…) chosen on the item's page is fetched. For another external track, choose it there before PLAY.
- Subtitles inside the file stay reachable with the deck's **SUBTITLE** button.
- **A transcode:** only picture subtitles (PGS, DVB, DVD) are burned in. It carries just the audio track you chose, so pick tracks before PLAY.

## Dolby Vision

mpv can't show Dolby Vision, so OSD/OS asks for an SDR transcode of such a video even on **Direct Play**.

- That transcode is capped at 1080 lines and 20 Mbps. With a quality tier, the tier's cap applies.
- A text subtitle survives it. A picture subtitle is dropped for that video.

## Intro Skip and Credit Skip

Emby marks an intro and the credits as chapter markers once it has detected them. A video without markers plays as usual.

| Setting | What happens when the intro or the credits start |
|---|---|
| **Off** (the default) | Nothing |
| **Auto** | Skipped, once per episode. Skipping the credits ends the episode |
| **Button** | The deck's menu opens with **SKIP** under the cursor: select skips, or let it play. ▲ or ▼ brings the menu back while the part lasts |

## Settings

Settings → TV / Movie → Emby. Everything but Enabled and Scaling shows only once you have signed in.

| Setting | Values | Default | What it does | Config key |
|---|---|---|---|---|
| Enabled | ON, OFF | OFF | Shows Emby on the main menu | `enabled` |
| Libraries | each library, ON or OFF | all ON | Which libraries the home screen lists | `libraries` |
| Video Quality | Direct Play, 480p (NTSC CRT), 576p (PAL CRT), 720p, 1080p | Direct Play | Direct Play, or a transcode capped at that size | `video_quality` |
| Resume Playback | Ask, Always | Ask | Whether a video with a resume point asks, or carries on | `resume_playback` |
| Autoplay Next Episode | ON, OFF | OFF | Plays the series' next episode when one ends | `autoplay_next_episode` |
| Intro Skip | Off, Auto, Button | Off | What happens at a marked intro | `intro_skip` |
| Credit Skip | Off, Auto, Button | Off | What happens at marked credits | `outro_skip` |
| Scaling | Default, Letterbox, 14:9, Pan & Scan, Anamorphic | Default | How 16:9 fills the 4:3 screen in Emby; Default follows Settings → TV / Movie → Scaling | `video_scaling` |
| Sign out | (an action) | | Removes this device from the server and forgets the sign-in | |

### How the values are saved

The keys sit under `modules.com.osdos.emby`, nested as in the fragment [above](#fill-in-the-address-beforehand). See [Configuration Files](https://github.com/mehmetraif/OSD-OS/wiki/Configuration-Files).

| Key | Saved as |
|---|---|
| `enabled`, `autoplay_next_episode` | `true` or `false` |
| `libraries` | an object of `"<library id>": true` or `false` (`"7": true`); a library left out counts as ON |
| `video_quality` | `"auto"` (Direct Play), `"480p"`, `"576p"`, `"720p"` or `"1080p"` |
| `resume_playback` | `"ask"` or `"always"` |
| `intro_skip`, `outro_skip` | `"Off"`, `"Auto"` or `"Button"` |
| `video_scaling` | the option's name, such as `"Letterbox"` |
| `server_url` | the server address: Settings → TV / Movie → EMBY → Server Address, and the sign-in screen |

## How the sign-in is stored

`emby_auth.json` in the data folder, readable by your user only (mode 600), holds the server's address, its token for this device and the user.

- No password or Emby Connect token is stored.
- **Sign out** deletes the file.
- A self-signed certificate is accepted for the server's own address.

## Troubleshooting

Sign-in messages are listed [above](#what-the-sign-in-screen-can-say).

| Problem | What to do |
|---|---|
| **CONNECTION FAILED: …** | Check the address has `http://` or `https://` and the port. From the device, `curl http://192.168.1.30:8096/System/Info/Public` (with your address) should answer with the server's name |
| The home screen stays on **LOADING...** | The server didn't answer (the log has `LOAD LIBRARIES FAILED`), or no library is ON in Settings → TV / Movie → Emby → Libraries |
| Back on the sign-in screen by itself | The device was removed on the server. Sign in again |
| No intro or credits are skipped | The server hasn't detected markers in that episode |
| Offline playlist says **Not Allowed** | An administrator must allow your Emby user to download media. Then **Retry Downloads** on the playlist's page |
| No Continue Watching or Next Up | They show only when they have something |

Logs: `journalctl -u osdos -b | grep -i emby`. mpv's log is `/tmp/runtime-pi/osdos-mpv.log` on the image ([elsewhere](https://github.com/mehmetraif/OSD-OS/wiki/Configuration-Files#logs-and-temporary-files)). See [Troubleshooting](https://github.com/mehmetraif/OSD-OS/wiki/Troubleshooting).

## See also

[Jellyfin](https://github.com/mehmetraif/OSD-OS/wiki/Jellyfin), [Plex](https://github.com/mehmetraif/OSD-OS/wiki/Plex), [Playlists](https://github.com/mehmetraif/OSD-OS/wiki/Playlists), [Playback and mpv](https://github.com/mehmetraif/OSD-OS/wiki/Playback-and-mpv), [Settings](https://github.com/mehmetraif/OSD-OS/wiki/Settings), [Modules](https://github.com/mehmetraif/OSD-OS/wiki/Modules)
