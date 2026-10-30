# Install OSD/OS

## On a Raspberry Pi

The simplest way is the **OSD/OS image**: download `OSD-OS-<version>-raspberry-pi.img.xz` from the [latest release](https://github.com/mehmetraif/OSD-OS/releases/latest), flash it ([os/README.md](os/README.md#flashing)) and the Pi boots straight into OSD/OS. From a terminal, `curl -fsSL https://osdos.tv/get.sh | sh -s -- image` downloads it into the current folder and checks it against the SHA-256 GitHub keeps for it (`iex "& { $(irm https://osdos.tv/get.ps1) } image"` in Windows PowerShell). [OSD/OS Setup](https://osdos.tv/setup/) chooses its Wi-Fi, display output, password, SSH and settings before the first start, in one file for the card's boot partition ([os/README.md → OSD/OS Setup](os/README.md#osdos-setup)).

The following steps will instead set up an SD card for your Raspberry Pi with the latest version of OSD/OS (and optionally set it up to autostart after boot).  

Steps 1-4 are focused on setting up a new card with Raspberry Pi OS Lite (64-Bit) and include options for writing a config.txt that will output to a CRT or modern TV.  

However, if you already have Raspberry Pi OS set up and working for your TV then the specific OSD/OS install steps start at step 5.

### Requirements

- A RaspberryPi
    - The [Pi 4](https://www.raspberrypi.com/products/raspberry-pi-4-model-b/) fits in a nice sweet spot of performance + composite out and its the model I use daily so its the model I am most familiar with. It supports 1080p H264/HEVC playback well on both a CRT and over HDMI.
    - The [Pi 3B and 3B+](https://www.raspberrypi.com/products/raspberry-pi-3-model-b/) work well too with some caveats...  
        - The default configuration for Pi 3 supports smooth 1080p H264 playback at the expense of removing crop functionality.  If crop is important for your use case on a Pi 3 then set Settings → TV / Movie → **1080p Playback** to Off, with the caveat that 1080p H264 playback will no longer be smooth (720p and below will still work well). [Playback and mpv](https://github.com/mehmetraif/OSD-OS/wiki/Playback-and-mpv) in the wiki explains the two decode paths.
        - If you choose to boot a Pi 3/3B+ from USB mass storage instead of SD, some USB flash drives can hang during early boot. If that happens, try an SD card or a different USB drive first before assuming the OSD/OS install is the issue.
    - The [Pi 5](https://www.raspberrypi.com/products/raspberry-pi-5/) also works well but I've only tested over HDMI to a modern TV. The Pi 5 doesn't have a direct composite output port: its composite output is on two pads on the board, which [Display Output](https://github.com/mehmetraif/OSD-OS/wiki/Display-Output) in the wiki covers, along with SCART RGB.
    - What is known about each model is in the wiki's [Installation](https://github.com/mehmetraif/OSD-OS/wiki/Installation#supported-hardware) page. If you have a setup that is working for you and would like to help out others, please tell us in an [issue](https://github.com/mehmetraif/OSD-OS/issues) so it can go in the wiki.
- SD Card (minimum of 4GB) with RaspberryPi OS already set up
    - Note: installed this way, OSD/OS is only an application, not an OS, so you will need to make sure you have an OS setup and working with the display you'd like to use. The [OSD/OS image](os/README.md) is the other way round: it is the whole system.
    - In the below steps I provide an example using Raspberry Pi OS Lite that you can use to create a fresh SD card along with configs I've tested for CRT and HDMI output.
- A keyboard to navigate
- Internet Access (either WiFi or network cable will work)

### Optional

- A CRT TV and a composite cable
    - Composite out is my recommended way to use OSD/OS
    - it will also work over HDMI as well so just select the config that works for your setup in step 2 below.
    - This is the composite cable I use if you happen to have a CRT: https://www.adafruit.com/product/2881 (note: I've only tested composite on the Pi 3/4 - the Pi 5 works well over HDMI
- USB remote control
    - Keyboard input works well but if you want that experience of sitting back and playing video on a VCR then a remote will definitely help with that.
    - I use this one: https://www.amazon.com/dp/B01FVUGPE8
- USB game controller 
    - Most controllers (Xbox, PlayStation, 8BitDo, NES-style, etc.) should work out of the box: D-pad/left stick to navigate, A to select, B to go back, Start for play/pause.  
    - Controllers can be plugged in at any time, and the button mapping can be customized — see [BUILDING.md → Gamepad input](BUILDING.md#gamepad-input-inputcfg).
    - If a controller isn't detected, make sure your user is in the `input` group: `sudo usermod -aG input $USER` then reboot.

### Steps

1) Write RaspberryPi OS Lite (64-bit) to an SD Card

    I reccomend using [Raspberry Pi Imager](https://www.raspberrypi.com/software/), it handles everything from OS selection to preconfiguring networking and user set up in nice simple flow

    Here is what you should select for OS if using Raspberry Pi Imager:

    | OS > Raspberry Pi OS (other) | Raspberry Pi OS Lite (64-bit) |
    | --- | --- |
    | <img src="https://github.com/user-attachments/assets/bb9f7a47-12b7-4580-abf4-ec8ad22153ba" /> | <img src="https://github.com/user-attachments/assets/30c39fce-99f8-48c9-9ad0-2b39b52690c1" /> |

    I would also suggest filling out Hostname, User and Wifi in the customization section and enabling SSH there as it will save you from having to set those up manually later.

2) After the write is complete, reconnect the card to your PC and update your config.txt to one of the following (please make sure to choose the one that best matches your TV). The OSD/OS image comes set up instead, and switches between HDMI, composite and SCART RGB in Settings → Display Output ([os/README.md](os/README.md#display-output-hdmi-composite-or-scart)):

    **Option 1: For composite out on a CRT TV (NTSC)...**
    ```
    # --- Global ---
    auto_initramfs=1
    disable_splash=1
    disable_overscan=1
    dtparam=audio=on

    # Composite
    enable_tvout=1
    sdtv_mode=0      # 0 = NTSC, 2 = PAL
    sdtv_aspect=1    # 1 = 4:3

    # --- Pi 3B ---
    [pi3]

    # Drivers & Video
    dtoverlay=vc4-fkms-v3d,cma-256

    # Overclocking
    over_voltage=4
    arm_freq=1300
    core_freq=450
    sdram_freq=500

    # --- Pi 3B+ ---
    [pi3+]

    # Drivers & Video
    dtoverlay=vc4-fkms-v3d,cma-256

    # Overclocking
    over_voltage=2
    arm_freq=1500
    core_freq=500
    sdram_freq=500

    # --- Pi 4B ---
    [pi4]

    # Drivers & Video
    dtoverlay=vc4-fkms-v3d,cma-256
    dtoverlay=rpivid-v4l2

    # Overclocking
    over_voltage=2
    arm_freq=1750
    gpu_freq=600

    # --- Pi 5 ---
    [pi5]

    # Drivers & Video
    dtoverlay=vc4-kms-v3d,cma-512,composite=1
    
    # --- Global ---
    [all]
    ```

    or **Option 2: for HDMI out on a modern TV...**
    ```
    # --- Global ---
    auto_initramfs=1
    disable_splash=1
    disable_overscan=1

    # HDMI
    display_auto_detect=1
    hdmi_force_hotplug=1

    # --- Pi 3B ---
    [pi3]

    # Drivers & Video
    dtoverlay=vc4-fkms-v3d,cma-256

    # Overclocking
    over_voltage=4
    arm_freq=1300
    core_freq=450
    sdram_freq=500

    # --- Pi 3B+ ---
    [pi3+]

    # Drivers & Video
    dtoverlay=vc4-fkms-v3d,cma-256

    # Overclocking
    over_voltage=2
    arm_freq=1500
    core_freq=500
    sdram_freq=500

    # --- Pi 4B ---
    [pi4]

    # Drivers & Video
    dtoverlay=vc4-fkms-v3d,cma-256
    dtoverlay=rpivid-v4l2

    # Overclocking
    over_voltage=2
    arm_freq=1750
    gpu_freq=600

    # --- Pi 5 ---
    [pi5]

    # Drivers & Video
    dtoverlay=vc4-kms-v3d
    
    # --- Global ---
    [all]
    ```

3) Place the SD card in your Raspberry Pi and let it run through its first boot sequence

    - Raspberry Pi OS Lite does **not** boot to a desktop. On first boot you may see boot log text, a text login console, or sometimes just a blinking cursor for a bit while it finishes booting.

4) Once complete, SSH in and run `sudo raspi-config`

    - If you set the hostname/user in Raspberry Pi Imager, SSH should usually be as simple as `ssh your-user@your-hostname.local`
    - If `.local` doesn't resolve on your network, use the Pi's IP address instead (`ssh your-user@192.168.x.x`). You can find it in your router's client list or on the Pi itself with `hostname -I`

    - Turn on Auto Login: `System Options > Auto Login > Yes`
    - Expand filesystem: `Advanced Options > Expand Filesystem > Yes`
    - Select Finish and allow the Raspberry Pi to reboot

5) After that completes SSH in again and run the following to install the latest version of OSD/OS

    ```bash
    bash <(curl -fsSL https://github.com/mehmetraif/OSD-OS/releases/latest/download/install.sh)
    ```

    This will install all of the needed dependencies (note: over WiFi it will take about 20 mins to complete) 

    **Optional** 
    - You will get an option at the end of the install script that asks: `Install systemd autostart service? [y/N]` 
    - If you type `Y` and press enter it will set up OSD/OS to autostart when your Raspberry Pi boots to create a simple appliance experience (bascially a dedicated OSD/OS device).
    - If you choose that option please make sure to enter your primary user for the pi at the next prompt.  If you don't provide one it will set it up for the `pi` user.
    - If you ever need to inspect the autostart logs later, use `sudo journalctl -u osdos -f`

At this point you can type `osdos` at any time to start up the app.  And if you installed the autostart service then the next time you boot your Pi it will boot directly into OSD/OS.

### Post Install

**Modules**

- The Local Files and Playlists modules will be enabled by default and you can open settings to enable any other modules you would like to display.  
- Please see the [Modules](https://github.com/mehmetraif/OSD-OS/wiki/Modules) pages in the wiki for details on any additional set up that may be needed for the modules you'd like to use.

**CRT Ouput**

- On a CRT's display output (or a TV's mode off the image: 288 lines or fewer at least twice as many pixels wide, or 720×480 or 720×576) OSD/OS draws for the tube's 4:3, whose pixels aren't square: the menus, the games and the videos, mpv being told the screen is 4:3 (`--monitoraspect=4:3`).
- If video playback still looks slightly squeezed/stretched on your tube then please try these steps:
    - create an `mpv.conf` file in `~/.config/mpv`
    - in that file add a line with the following, the 4:3 OSD/OS gives mpv
        ```
        monitoraspect=1.333
        ```
    - raise or lower it a little until a circle in a 4:3 test pattern is round (I recommend adding one to the local files media directory for easy access if you need it).
- A `monitoraspect` (or `monitorpixelaspect`) line there takes the place of the 4:3 OSD/OS gives mpv. `monitoraspect` is the screen's shape, the same in every mode, where `monitorpixelaspect` is a pixel's and so changes with the mode.
- An earlier version of these steps suggested `monitorpixelaspect=0.888889`. OSD/OS now tells mpv the screen's shape itself, so remove that line: it is only right for 720×480, and in its place OSD/OS stops passing its 4:3.
- For an explanation on PAR along with other values you can try for both NTSC and PAL set ups; please see [this wiki entry](https://en.wikipedia.org/wiki/Pixel_aspect_ratio#Introduction) and thank you to DVDCreep for the findings [here](https://github.com/anthonycaccese/240-MP/discussions/267) that helped work this out.

**Audio**

- If analog / composite audio is unusually quiet:** 
    - OSD/OS keeps the card at 0 dB, its full level without a boost, as anything plays
    - Run `amixer sset PCM 100%` to raise the AV jack 4 dB above that, which OSD/OS leaves as it is (loud sound can clip there)
    - If that solves it and you want to keep the level across reboots, run `sudo alsactl store`
- If there is no audio at all:** 
    - **Settings → Audio Output** picks the card from the app, where plain ALSA plays (Raspberry Pi OS Lite, the OSD/OS image): it lists the cards there are, the AV jack, HDMI or a USB sound card, and plays through the one chosen from then on. The two options below choose outside the app, for every program or for mpv; a card chosen in Settings wins over both, and on **Auto** they apply. Without the first of them, Auto gives every player the first card that can play while ALSA's first card can't (a Pi 5's HDMI without the picture); the second still has the last word for mpv.
    - ALSA's `default` device resolves to whichever sound card enumerated first and sometimes that may not be the one your audio is actually plugged into, which will result in missing audio.
    - To see the cards your Pi enumerated run `cat /proc/asound/cards` (or `aplay -l`).  The short name in brackets is the card id you'll in one of the options below.  The ids you see will also depend on which config.txt from step 2 you used. (the Pi 3 / Pi 4 run fake KMS where analog / composite audio shows up as `Headphones`, while the Pi 5 blocks run Full KMS where HDMI audio shows up as `vc4hdmi0` and `vc4hdmi1`).  Any USB audio devices you've attached will have their own entries too, so please read the ids off your own Pi vs copying from here.
    - Once you have the ID for your audio device there are two options to fix it, please pick whichever fits your setup best:
        - **Option 1: change the default at the OS level**
            - Create `/etc/asound.conf` to set the card that ALSA should treat as the default, substituting the card id you read above:

                ```
                defaults.pcm.card "your-card-id"
                defaults.ctl.card "your-card-id"
                ```
            - Every program on the Pi picks this up (not just OSD/OS) so this is recommened if you want to set audio output for all applications you have running on your OS.  This works on Raspberry Pi OS Lite (the image these steps use), where plain ALSA is in charge of the `default` device.  If you are running a Desktop image instead then PipeWire typically owns `default` and you should pick your output there rather than in `/etc/asound.conf`.
        - **Option 2: change it for mpv only**
            - Create (or add to your existing) `~/.config/mpv/mpv.conf` and add a single line naming the device:
                ```
                audio-device=alsa/default:CARD=your-card-id
                ```
            - Run `mpv --audio-device=help` to see the exact device strings mpv will accept and copy the one that matches your audio output device. `default:CARD=` is ALSA's own way to the card: `plughw:` can't feed the Pi's HDMI under full KMS, which takes its samples framed as IEC958.
    - In both options the change applies the next time playback starts, as long as Settings → Audio Output is on Auto. Option 1 reaches everything that plays sound; Option 2 every mpv OSD/OS starts for a video, but not the menu music's (which runs mpv without a config) or the player inside the window under Transparent Background (libmpv, which reads no `mpv.conf`).  Please see [ARCHITECTURE.md → How mpv flags are layered](ARCHITECTURE.md#how-mpv-flags-are-layered-the-precedence-cascade) for how the two layer.

**Exit to Terminal and Restart**

- If you have the autostart service installed, the Quit dialog gains an `Exit to Terminal` option alongside `Power Off`. Choosing that will drop you to a login shell on the Pi instead of powering off, and leaves autostart intact for subsequent reboots. 
- It also offers `Restart`, which reboots the Pi. An install from before it was added needs `install.sh` run again to get it, since the service's stop helper is what reboots.
- To get back into OSD/OS from that shell you can do one of the following:
    1. (*Recommended*) type `sudo systemctl start osdos` to start up OSD/OS and the autostart service again
    2. type `sudo reboot` to reboot and start up the device from scratch (which will also restart the autostart service)
    3. type `osdos` which will relaunch the app unmanaged in your shell; here the Quit dialog shows the plain Yes/No menu and selecting Yes will just return you to the shell rather than powering off

### Update

**From within the app (recommended):** go to `Settings → Update`, check for updates, download, and choose Apply & Restart. Your settings will be retained. Only a newer release is ever offered, and if the power goes out while the update is being swapped in, the next start puts the previous version back whole and applies the update again.

> Installs made before in-app updates were possible need to re-run the install script **once** (just follow the steps below).  Doing that will pick up the new launcher.  There is a check on the Update screen that will let you know if that's the case for you.

**Via the install script** (also works any time; your settings will be retained):

1) SSH into your Raspberry Pi
2) Re-run the install script
    ```bash
    bash <(curl -fsSL https://github.com/mehmetraif/OSD-OS/releases/latest/download/install.sh)
    ```
3) When it asks to "`Install systemd autostart service? [y/N]`"
    - If you already have autostart set up please answer `Y`, which brings the service and its helpers up to date (the next prompt offers the user the service already runs as). Answering `N` keeps your existing service as it is and brings its helpers up to date, and the app files still go to its user, which in-app updates rely on
    - If you don't have autostart installed and want to keep it that way just answer `N`
    - And if you want to turn on autostart please answer `Y`
    - On the OSD/OS image you won't be asked: the script keeps the image's own service as it is and just brings the app and its helpers up to date
    - If the script warns that the user can't write `/opt/osdos`, in-app updates won't be able to apply until that user can
4) Once that completes just run `sudo reboot` and when your pi restarts you'll be on the latest version

### Uninstall

1) If you installed the autostart service, stop and remove it first (stopping it from outside leaves the Pi on):

    ```bash
    sudo systemctl disable --now osdos.service osdos-display.socket osdos-wifi.socket osdos-name.socket
    sudo systemctl unmask getty@tty1.service autovt@.service
    sudo rm -f /etc/systemd/system/osdos.service /etc/systemd/system/osdos-terminal.service /usr/local/bin/osdos-stop
    sudo rm -f /etc/systemd/system/osdos-display.socket /etc/systemd/system/osdos-display@.service
    sudo rm -f /etc/systemd/system/osdos-wifi.socket /etc/systemd/system/osdos-wifi@.service
    sudo rm -f /etc/systemd/system/osdos-name.socket /etc/systemd/system/osdos-name@.service
    sudo rm -f /etc/polkit-1/rules.d/50-osdos-networkmanager.rules
    sudo systemctl daemon-reload
    ```

2) Then remove OSD/OS itself, and the udev rule the installer added, via terminal or over SSH. Your SD card is then free for other things:

    ```bash
    sudo rm -rf /opt/osdos
    sudo rm -f /usr/local/bin/osdos /etc/udev/rules.d/99-osdos-tty.rules
    ```

## On macOS (ARM)

If you don't have a Raspberry Pi and would like to try OSD/OS, I also provide a build for macOS on Apple Silicon.  You can download a DMG archive from the latest release and run it on your mac following these steps...

### Requirements

- An Apple Silicon Mac running the latest version of macOS (it will not work on Intel based devices)
- Internet Access (either WiFi or network cable will work)

### Steps

1. Download the DMG archive from the latest release. Or in Terminal: `curl -fsSL https://osdos.tv/get.sh | sh` downloads it into the current folder and checks it against the SHA-256 GitHub keeps for it.
2. Mount it and move the osdos.app into your Applications folder
3. Make sure you have mpv installed (OSD/OS requires MPV for playback): `brew install mpv`. For menu music in MIDI or a tracker's XM, MOD, S3M or IT, also `brew install fluid-synth libopenmpt`, with a General MIDI SoundFont (`.sf2`) in the data folder's `soundfonts` (`~/Library/Application Support/OSD-OS/soundfonts/`).
4. Double click the app (`osdos.app`) and it should open full screen. If macOS refuses to open it because it can't check it (a release that isn't notarized; its notes say so), go to System Settings → Privacy & Security and choose **Open Anyway**, or run `xattr -dr com.apple.quarantine /Applications/osdos.app` and open it again. You need to do this only the first time you open it.

### Post Install

**Modules**

- The Local Files and Playlists modules will be enabled by default and you can open settings to enable any other modules you would like to display. 
- Please see the [Modules](https://github.com/mehmetraif/OSD-OS/wiki/Modules) pages in the wiki for details on any additional set up that may be needed for the modules you'd like to use.

**Settings from OSD/OS Setup**

- Choose the settings on [OSD/OS Setup](https://osdos.tv/setup/), press **Copy for a computer** there, then Settings → **Paste OSD/OS Setup** in OSD/OS: its own settings and the RetroAchievements account are set at once.
- Or save the page's `osdos-setup.json` in `~/Library/Application Support/OSD-OS/`, or in the folder `osdos.app` is in: OSD/OS applies it at its next start, says what it set, and deletes the file, which holds the account's password.
- The device's name, the display output, Wi-Fi and the system's settings are set on an OSD/OS card alone, and left as they are on a Mac.

### Update

**From within the app (recommended):** go to `Settings → Update`, check for updates, download, and choose Apply & Relaunch — the app will swap itself in `/Applications` and reopen on the new version. Your settings will be retained.

Or manually:
1. Download the DMG archive from the latest release
2. Mount it and move the osdos.app into your Applications folder to overwrite your existing version. *Your existing settings will be retained and it's safe to overwrite*
3. If macOS refuses to open the new version, allow it as in step 4 of the steps above.

### Uninstall

- Remove it just like you would any application on macOS
- Remove the configuration files in `~/Library/Application Support/OSD-OS/`

## On SteamOS / Linux x86_64

For **SteamOS** and other x86_64 Linux distros, OSD/OS ships as an **AppImage**. mpv is bundled, so there is nothing else to install; it runs on stock SteamOS without any additional package installs.

### Requirements

- SteamOS or an x86_64 Linux machine running a graphical session: X11, or Wayland with XWayland available (the AppImage ships Qt's `xcb` platform plugin)
- Internet access

The AppImage carries its own copy of the Wayland client libraries and uses them only if the system has none, so X11-only distributions that ship no Wayland at all (e.g. Batocera and similar buildroot-based images) should be able to run it too.

### Steps

> **Note for SteamOS**: please switch to Desktop Mode for the following steps

1. Download `OSD-OS-linux-x86_64.AppImage` from the [latest release](https://github.com/mehmetraif/OSD-OS/releases/latest). Or from a terminal: `curl -fsSL https://osdos.tv/get.sh | sh` downloads it into the current folder, checks it against the SHA-256 GitHub keeps for it and makes it executable, which is step 2.
2. In your file manager, right-click the file → **Properties → Permissions** → tick *Is executable* (or run `chmod +x` on it from terminal).
3. Double-click to launch. The Local Files and Playlists modules are enabled by default; open Settings to enable others (see the [Modules](https://github.com/mehmetraif/OSD-OS/wiki/Modules) pages in the wiki for details on each).

### Post Install

**Modules**

- The Local Files and Playlists modules will be enabled by default and you can open settings to enable any other modules you would like to display. 
- Please see the [Modules](https://github.com/mehmetraif/OSD-OS/wiki/Modules) pages in the wiki for details on any additional set up that may be needed for the modules you'd like to use.

**SteamOS Gaming Mode**

- To run OSD/OS from SteamOS Gaming Mode...
- In Desktop Mode, open **Steam → Games → Add a Non-Steam Game to My Library**, click **Browse**, and select the AppImage (`OSD-OS-linux-x86_64.AppImage`).
- When in Gaming Mode it will appear in your library under "Non Steam Games".
- There is a set of artwork available [here](https://github.com/anthonycaccese/240-MP/discussions/249#discussioncomment-18115953) that can be used for Gaming Mode grid display.

**Settings from OSD/OS Setup**

- Choose the settings on [OSD/OS Setup](https://osdos.tv/setup/), press **Copy for a computer** there, then Settings → **Paste OSD/OS Setup** in OSD/OS: its own settings and the RetroAchievements account are set at once.
- Or save the page's `osdos-setup.json` in `~/.local/share/OSD-OS/`, or beside the AppImage: OSD/OS applies it at its next start, says what it set, and deletes the file, which holds the account's password.
- The device's name, the display output, Wi-Fi and the system's settings are set on an OSD/OS card alone, and left as they are on a computer.

### Update

- **From within the app (recommended):** go to `Settings → Update`, check for updates, download, and choose **Quit & Apply on Next Launch**. 
- The app verifies the download, swaps the new `.AppImage` over your current one in place (keeping a `.bak` of the previous version until the new one launches cleanly). 
- In Desktop Mode the app then starts again on the new version by itself. In **Gaming Mode** the app needs to close after applying; so simply relaunch it from your Steam library to pick up the new version (the file path is unchanged, so your existing shortcut will still work). 
- Your settings in `~/.local/share/OSD-OS/` are retained.
- If the app is stored in a read-only location the in-app update can't write to then the update screen simply point you at the [Releases page](https://github.com/mehmetraif/OSD-OS/releases/latest) to download and replace the `.AppImage` manually.

### Uninstall

- Delete the `.AppImage` file (and remove it from Steam if you added it as a non-Steam game)
- Remove the configuration files in `~/.local/share/OSD-OS/`

## On Windows (x64)

For Windows 11 on x64, OSD/OS comes as a zip: one folder, `osdos`, with `osdos.exe`, Qt and the libraries it needs, and OSD/OS's own files. Nothing is installed: the folder runs from wherever you put it.

### Requirements

- Windows 11 on an x64 PC
- Internet access
- mpv, which plays the videos (step 3)

### Steps

1. Download `OSD-OS-<version>-windows-x64.zip` from the [latest release](https://github.com/mehmetraif/OSD-OS/releases/latest). Or in PowerShell: `irm https://osdos.tv/get.ps1 | iex` downloads it into the current folder, checks it against the SHA-256 GitHub keeps for it and unzips it there, which is step 2.
2. Unzip it where you want to keep OSD/OS, for example in `C:\Games`. It unpacks to one folder, `osdos`.
3. Download a Windows build of mpv from [mpv.io](https://mpv.io/installation/) and put its folder inside `osdos` as `mpv`, or `mpv.exe` beside `osdos.exe`; mpv anywhere on the `PATH` works too.
4. Double-click `osdos.exe`. Windows may stop the first start of a program it doesn't know yet (**Windows protected your PC**): choose **More info**, then **Run anyway**.

### Post Install

**Modules**

- The Local Files and Playlists modules will be enabled by default and you can open settings to enable any other modules you would like to display.
- Please see the [Modules](https://github.com/mehmetraif/OSD-OS/wiki/Modules) pages in the wiki for details on any additional set up that may be needed for the modules you'd like to use.

**Arcade**

- Settings → Arcade → **MAME** downloads MAME, and Settings → Arcade → **FinalBurn Neo** downloads RetroArch and the FinalBurn Neo core. Both are the versions the OSD/OS image has, so a PC plays online and over the local network with a Pi.

**YouTube**

- Put `yt-dlp.exe` in the data folder's `bin` folder (`%APPDATA%\OSD-OS\bin\`), beside `osdos.exe` or on the `PATH`, and `deno.exe` on the `PATH`.

**A window on the desktop**

- Settings → **Window Mode** shows OSD/OS in a window on the desktop instead of the whole screen; F11 or Alt+Enter switches at any time.

**Settings from OSD/OS Setup**

- Choose the settings on [OSD/OS Setup](https://osdos.tv/setup/), press **Copy for a computer** there, then Settings → **Paste OSD/OS Setup** in OSD/OS: its own settings and the RetroAchievements account are set at once.
- Or save the page's `osdos-setup.json` in `%APPDATA%\OSD-OS\`, or in the `osdos` folder beside `osdos.exe`: OSD/OS applies it at its next start, says what it set, and deletes the file, which holds the account's password.
- The device's name, the display output, Wi-Fi and the system's settings are set on an OSD/OS card alone, and left as they are on a PC.

**The log**

- OSD/OS opens no console window. It writes its log to `%APPDATA%\OSD-OS\osdos.log`, the run before's kept as `osdos-previous.log`; started from a Command Prompt or PowerShell, it writes it there as well.

### Update

Settings → Update says when a newer version is out and names its zip, but on Windows it doesn't put the new version in place itself:

1. Download the new `OSD-OS-<version>-windows-x64.zip` from the [latest release](https://github.com/mehmetraif/OSD-OS/releases/latest).
2. Close OSD/OS, delete the old `osdos` folder and unzip the new one in its place. *Your settings are in `%APPDATA%\OSD-OS\`, apart from the app, and stay as they are, as do the MAME and RetroArch it downloaded.*
3. If mpv was inside the old folder, put it in the new one again.

### Uninstall

- Delete the `osdos` folder
- Delete `%APPDATA%\OSD-OS\`: your settings, and the MAME and RetroArch OSD/OS downloaded

## On FreeBSD (amd64)

For FreeBSD 15 on an amd64 PC, OSD/OS comes as a FreeBSD package, `OSD-OS-<version>-freebsd-amd64.pkg`: the app, and FinalBurn Neo's core for the Arcade module, built for FreeBSD from its source, as libretro has no FreeBSD build of it. Qt 6, SDL2 and mpv are FreeBSD's own packages. OSD/OS runs on your desktop, X11 or Wayland, on the whole screen or in a window. *The package is built, installed and started in a FreeBSD virtual machine for every release; it hasn't been tried on a FreeBSD computer yet.*

### Requirements

- FreeBSD 15 on an amd64 PC, with a desktop
- Internet access

### Steps

1. Download `OSD-OS-<version>-freebsd-amd64.pkg` from the [latest release](https://github.com/mehmetraif/OSD-OS/releases/latest). Or from a terminal: `fetch -qo - https://osdos.tv/get.sh | sh` downloads it into the current folder, checks it against the SHA-256 GitHub keeps for it and gives the commands of step 2.
2. As root, install the packages it needs, then OSD/OS:
    ```sh
    pkg install -y qt6-base qt6-declarative qt6-svg sdl2 minizip mpv
    pkg add OSD-OS-<version>-freebsd-amd64.pkg
    ```
3. Start it from your desktop's terminal: `osdos`.

### Post Install

**Arcade**

- MAME and RetroArch are FreeBSD's packages: `pkg install mame retroarch` as root. FinalBurn Neo's core comes with OSD/OS; Settings → Arcade → FinalBurn Neo shows it, with the games it knows. With RetroArch there, FinalBurn Neo's games play with RetroAchievements and online play, as on the other systems.

**YouTube**

- `pkg install yt-dlp` as root.

**Settings from OSD/OS Setup**

- Choose the settings on [OSD/OS Setup](https://osdos.tv/setup/), press **Copy for a computer** there, then Settings → **Paste OSD/OS Setup** in OSD/OS; or save the page's `osdos-setup.json` in `~/.local/share/OSD-OS/`, which OSD/OS applies at its next start.

**What a FreeBSD PC doesn't have**

- What is the Pi's stays the Pi's, as on a Mac: Display Output, Audio Output's cards (the default device plays), Bluetooth and Wi-Fi (the system's own tools) and the JAMMA board. Netflix and Prime Video aren't there: no browser on FreeBSD has the Widevine they need.

### Update

Settings → Update says when a newer version is out and names its package. Download it from the [latest release](https://github.com/mehmetraif/OSD-OS/releases/latest) and add it as root, which puts it in the place of the version installed: `pkg add OSD-OS-<version>-freebsd-amd64.pkg`. *Your settings are in `~/.local/share/OSD-OS/` and stay as they are.*

### Uninstall

- `pkg delete osdos` as root
- Delete `~/.local/share/OSD-OS/`: your settings

## On OpenBSD (amd64)

For OpenBSD 8.0 on an amd64 PC, OSD/OS comes as an OpenBSD package, `OSD-OS-<number>-openbsd-amd64.tgz`: the app, and FinalBurn Neo's core for the Arcade module, built for OpenBSD from its source, as neither libretro nor OpenBSD's packages have one. Qt 6, SDL2 and mpv are OpenBSD's own packages, which come with it. OSD/OS runs on your desktop, on the whole screen or in a window. *The package is built, installed and started in an OpenBSD virtual machine for every release; it hasn't been tried on an OpenBSD computer yet.*

Unlike the release's other files, it is named by the release's number without its `v`: `OSD-OS-0.0.24-openbsd-amd64.tgz` for v0.0.24. `pkg_add` takes a package's name from its file's, so keep the name the file is downloaded with, or `pkg_add` refuses it. On the system the package is called `OSD-OS`.

The package is made for one release of OpenBSD, 8.0 for now: OpenBSD's libraries change from one release to the next, so a newer OpenBSD needs the OSD/OS package built for it.

### Requirements

- OpenBSD 8.0 on an amd64 PC, with a desktop
- Internet access

### Steps

1. Download `OSD-OS-<number>-openbsd-amd64.tgz` from the [latest release](https://github.com/mehmetraif/OSD-OS/releases/latest). Or from a terminal: `ftp -MVo - https://osdos.tv/get.sh | sh` downloads it into the current folder under the name it has, checks it against the SHA-256 GitHub keeps for it and gives the command of step 2.
2. As root, add it: the packages it needs come with it from your mirror.
    ```sh
    pkg_add -D unsigned OSD-OS-<number>-openbsd-amd64.tgz
    ```
    *`-D unsigned` because the package isn't signed with OpenBSD's key, which signs OpenBSD's own packages alone.*
3. Start it from your desktop's terminal: `osdos`.

### Post Install

**Arcade**

- MAME and RetroArch are OpenBSD's packages: `pkg_add mame retroarch` as root. FinalBurn Neo's core comes with OSD/OS; Settings → Arcade → FinalBurn Neo shows it, with the games it knows. With RetroArch there, FinalBurn Neo's games play with RetroAchievements and online play, as on the other systems.

**YouTube**

- `pkg_add yt-dlp` as root.

**Settings from OSD/OS Setup**

- Choose the settings on [OSD/OS Setup](https://osdos.tv/setup/), press **Copy for a computer** there, then Settings → **Paste OSD/OS Setup** in OSD/OS; or save the page's `osdos-setup.json` in `~/.local/share/OSD-OS/`, which OSD/OS applies at its next start.

**What an OpenBSD PC doesn't have**

- What is the Pi's stays the Pi's, as on a Mac: Display Output, Audio Output's cards (the default device plays, through sndio), Bluetooth and Wi-Fi (the system's own tools) and the JAMMA board. Netflix and Prime Video aren't there: no browser on OpenBSD has the Widevine they need.

### Update

Settings → Update says when a newer version is out and names its package. Download it from the [latest release](https://github.com/mehmetraif/OSD-OS/releases/latest) and add it as root, which updates the version installed: `pkg_add -D unsigned OSD-OS-<number>-openbsd-amd64.tgz`. *Your settings are in `~/.local/share/OSD-OS/` and stay as they are.*

### Uninstall

- `pkg_delete OSD-OS` as root
- Delete `~/.local/share/OSD-OS/`: your settings
