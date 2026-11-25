# Phosphor

[![Build](https://github.com/melihkoray/phosphor/actions/workflows/build.yml/badge.svg)](https://github.com/melihkoray/phosphor/actions/workflows/build.yml)
[![Built and signed by GitHub Actions](https://img.shields.io/badge/built%20%26%20signed-GitHub%20Actions-2ea44f?logo=githubactions&logoColor=white)](https://github.com/melihkoray/phosphor/attestations)
[![VirusTotal: 0 of 61 engines](https://img.shields.io/badge/VirusTotal-0%2F61%20(v2.0.2)-2ea44f?logo=virustotal&logoColor=white)](https://www.virustotal.com/gui/file/9efc70694101580fcef897c6525e87db4b76faa0925c411bf40a54ddf860b0bd)

![Phosphor on the Waveshare 3.49" bar: the snowy cabin, the fireplace, the ocean liner, the radio, 21, a storm and the fish tank](media/readme/board-3d.webp)

Phosphor turns the **[Waveshare ESP32-S3-Touch-LCD-3.49](https://www.waveshare.com/esp32-s3-touch-lcd-3.49.htm)**,
a 640×172 bar display, into a retro desk gadget: a clock, weather, internet radio, timers, pixel scenes to leave
running, three games and a few small tools. Every page is drawn four times, as an old vacuum-fluorescent display
(VFD), C64-style, Amiga-style and 2600-style.

![The same clock and scenes in the VFD, C64, Amiga and 2600 looks](media/readme/hero.gif)

**[Install from the browser →](https://melihkoray.github.io/phosphor/)** (Chrome or Edge, USB cable, no tools needed)

> [!WARNING]
> Installing replaces whatever runs on the board now, the Waveshare factory demo included. It is not backed up and
> Phosphor cannot restore it. On a first install, tick **Erase device** in the installer for a clean start.

> [!IMPORTANT]
> **Built and tested on the V2 board** (a **V2** sticker on the case, **Rev1.1** printed on the board). The firmware
> detects the board version when it starts. V1 should work: its pins and panel reset match Waveshare's own V1 demo. Nobody has
> confirmed it yet, so if you run it on a V1, please [open an issue](https://github.com/melihkoray/phosphor/issues) and
> say how it went. On v2.0.0 the brightness setting has no effect on V2: update.

## Knock on the table to play 21

![21 against a cartoon dealer, Amiga-style](media/readme/21.gif)

Blackjack against a cartoon dealer, ten hands a match. You don't have to touch the screen: the board's motion sensor
feels your knuckles on the table. Knock once to stand, twice to hit (or tap HIT and STAND).

## Lighthouse Keeper

![Lighthouse Keeper](media/readme/keeper.gif)

My own game in the style of the 1980s handheld LCD games. Ships head for the rocks in three lanes; tap a lane to light
it and turn the ship away. Fixed segments with their faint ghosts, Game A and a faster Game B with more ships, three
misses and the night is over.

## Internet radio

![The radio page in the VFD look](media/readme/radio-vfd.gif)
![The radio page, Amiga-style, with a tracker pattern that runs with the music](media/readme/radio-amiga.gif)

Up to seven genre buttons; tap one again for the next station. The dancers and the equalizer move with the music
(hold them for another look).
Find stations on the settings page through [Radio Browser](https://www.radio-browser.info) or paste your own stream
(MP3 and AAC). The alarm wakes you with it.

## One page, four machines

![The snowy cabin, the fireplace and the weather in the VFD, C64-style, Amiga-style and 2600-style looks](media/readme/machines.png)

The scenes are redrawn in each machine's resolution and colors, not just recolored: 16 colors on the C64, 32 out of
4096 on the Amiga, 4 per line on the 2600. Change the look in **Settings > Theme** on the board.

## And many more

![Fish tank, lighthouse, ocean liner, focus timer, clock and alarm, play menu, snowy cabin, hourglass, runner, tools, coin flip and settings](media/readme/more.png)

And the real board on my desk (tilting it tilts the sea under the liner):

![Phosphor in the hand: tilting the board tilts the sea under the liner](media/hero.gif)

## New in v2.0.0

Four looks, a Play menu with three games, a Tools menu, settings on the board itself, scenes you can touch and a daily
update notice. The full list, with the fixes, is in the
[v2.0.0 release notes](https://github.com/melihkoray/phosphor/releases/tag/v2.0.0).

**Coming from v1.0.x:** install once more from the [installer page](https://melihkoray.github.io/phosphor/) over USB and
leave **Erase device** unticked. The flash layout changed, so this one step cannot happen over Wi-Fi. Your Wi-Fi,
city, stations and other settings are kept.

## What it does

The other pages, shown here in the VFD look:

| | |
|---|---|
| ![Clock](media/clock.gif) | **Clock.** Big segment digits, date, battery, 12 or 24 hour. Alarm: tap the bell to switch it on or off, tap the alarm hours (+1) or minutes (+5) to set it; it wakes you with the radio. |
| ![Weather](media/weather.gif) | **Weather.** Now and the next three days for your city ([Open-Meteo](https://open-meteo.com), no key). Tap for the days after. |
| ![Timer](media/timer.gif) | **Timer.** Add or remove 1, 5 or 10 minutes by touch, up to 99; it rings at the end. |
| ![Study](media/study.gif) | **Focus timer.** Pomodoro rounds in a little study room. |

Pixel scenes, to leave running:

- **Fish tank:** tap to feed, hold for night. Once a day a rare visitor.
- **Snowy cabin:** tap or shake to stir the snow, hold for a snow globe; holiday lights in winter.
- **Fireplace:** a cat sleeps by the fire. Tap to wake it, tilt the board for a ball of yarn, hold to add a log.
- **Lighthouse:** follows the time of day. Tap for a big wave, hold for a storm.
- **Ship:** an ocean liner that tilts with the board. Hold for the fog horn; now and then a whale.
- **Lava lamp** (tap for new colors) and an **hourglass** you turn over by tilting the board.

### Play

![Runner, Amiga-style](media/screens/runner-amiga.png)

Lighthouse Keeper and 21 (above), and **Runner**: tap the top of the screen to jump, the bottom to duck. Archers, bugs,
pits and ledges; the scenery changes every 400 m. High scores are kept on the board.

### Tools

![The Tools menu, 2600-style](media/screens/tools-2600.png)

Spirit level (tap sets zero), coin flip (tap or shake), box breathing (4-4-4-4), scoreboard (tap +1, hold −1) and a
screen lamp (tap for a color, swipe up or down for brightness). In any app or game, swipe sideways to go back to its
menu (in a game, from the pause or title screen).

### Settings on the board

![The Settings page, Amiga-style](media/screens/settings-amiga.png)

The last page. Look, clock style, temperature unit, language (English, Turkish or Polish), brightness, city, auto page turn,
holiday lights, night dimming, which pages show, help pages for every gesture, Wi-Fi (with a QR code that opens the
settings page on your phone) and About (version, and a note when a newer one is out).

## Setup

You need the [Waveshare ESP32-S3-Touch-LCD-3.49](https://www.waveshare.com/esp32-s3-touch-lcd-3.49.htm) (tested on
V2; V1 is untested: see the note at the top) and a USB-C data cable.

1. Open the [installer page](https://melihkoray.github.io/phosphor/), connect the board with a USB-C data cable, press
   **Install Phosphor** and pick **USB JTAG/serial debug unit** from the port list (`cu.usbmodem…` on a Mac, `COMx` on
   Windows, `ttyACM0` on Linux). Not sure? Unplug the board and plug it back in: the entry that comes back is the board.
2. The board restarts and shows a Wi-Fi name like `Phosphor-Setup-1A2B` and a QR code. Join it from your phone (no
   password); a setup page opens. If it does not, or the browser shows normal websites, turn off mobile data and type
   `http://192.168.4.1` in the browser (if you see a menu, tap **Configure WiFi**). Tap your
   home Wi-Fi (2.4 GHz), type its password, scroll down to **Device name** (prefilled as `phosphor-1a2b`, change it to
   something short like **`desk`**: this is the name you type in the next step) and tap **Save**. Write the name down.
   (Lost it? **Settings > Wi-Fi** on the board shows its IP address and a QR code for the settings page.)
3. With the phone back on your home Wi-Fi (mobile data on again), open `http://<device name>.local/settings` with the
   name from step 2, for example `http://desk.local/settings`. If the phone cannot open `.local` addresses
   (some Android phones), use the board's IP address from **Settings > Wi-Fi**. There you set:
   - **City:** search by name; weather and the time zone follow it.
   - **Language:** English, Turkish or Polish screen texts.
   - **Clock:** 12 or 24 hour.
   - **Focus timer:** focus, short and long break minutes.
   - **Radio:** groups and stations. Search [Radio Browser](https://www.radio-browser.info) or paste a stream URL,
     test it on the device, reorder, delete, export the list to a file and import it again.

Settings are stored on the board and survive restarts and updates (a reinstall with **Erase device** clears them).

**Touch:** swipe left for the next page, right for the previous one, up or down for the volume; taps work the page's
own buttons. Hold on the clock to switch the alarm on or off, hold on a timer to reset it. **Settings > Help** on the
board lists every gesture. Back buttons: PWR turns the screen off and on (hold 3 s on battery to power off), BOOT
changes brightness (hold 5 s to forget Wi-Fi and open the setup again), RESET restarts.

<img src="docs/img/buttons.jpg" width="600" alt="The back edge of the board: the BOOT, PWR and RESET buttons on the left, the speaker in the middle">

## Troubleshooting

- **No "USB JTAG/serial debug unit" in the port list:** try another cable (many only charge) or USB port. Still
  nothing: hold BOOT while plugging the cable in.
- **The board does not join your Wi-Fi:** most often a mistyped password (phones capitalise the first letter); hold
  BOOT for 5 seconds and try again. The ESP32 also only works with 2.4 GHz: pick the non-5G network if there are two.
- **The setup Wi-Fi or page is gone:** hold BOOT for 5 seconds. The board forgets the saved Wi-Fi and opens the setup
  again until you finish it. (After a restart with a failing saved Wi-Fi it opens by itself, but only for 3 minutes.)
- **The setup page does not open, or the browser shows normal websites:** the phone is on mobile data. Turn it off,
  stay joined to `Phosphor-Setup-…` ("no internet" is normal) and type `http://192.168.4.1` in the browser.
- **Forgot the device name:** **Settings > Wi-Fi** on the board shows the IP address and a QR code for the settings
  page. Your router's list of connected devices has it too.
- **`desk.local` does not open:** be on the same Wi-Fi as the board. Some Android phones cannot open `.local`: use a
  computer, or the board's IP address.
- **A station does not play:** use Test on the settings page. MP3 and AAC streams are tested.

## Building it yourself

Needs [arduino-cli](https://arduino.github.io/arduino-cli/) and the ESP32 core:

```bash
arduino-cli core install esp32:esp32@3.3.12 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

```bash
./build.sh                          # compile; output in build/
./build.sh /dev/cu.usbmodem101      # compile and flash over USB
```

To update over Wi-Fi, copy `firmware/secrets.h.example` to `firmware/secrets.h`, set a long random `OTA_TOKEN`, flash
once over USB, then `./build.sh desk.local`. Without a token, Wi-Fi updates are switched off.

Every push to `main` is built by GitHub Actions and published to the installer page; tags (`v2.0.0`) become releases.

## HTTP API

The main endpoints, plain HTTP on your local network (`GET /status` also tells you the board's IP address):

| Request | |
|---|---|
| `GET /status` | JSON status (IP, page, battery, ...) |
| `GET /screen` | the current screen, raw RGB565 640×172 |
| `POST /page` `name=clock\|radio\|weather\|timer\|study\|hourglass\|aquarium\|snow\|fire\|sea\|wave\|lava\|tools\|play\|settings` | show a page |
| `POST /cmd` `name=radio arg=next\|stop\|toggle\|<index>` | radio control (also `vol`, `alarm`, `timer`) |
| `POST /cmd` `name=theme arg=vfd\|c64\|amiga\|atari` | change the look |
| `POST /cmd` `name=app arg=fener\|blackjack\|runner\|level\|coin\|breathe\|score\|lamp\|close` | open an app or game, or close it |
| `POST /text` `text=... title=... ttl=60` | show a message |
| `GET/POST /api/settings` | the settings as `key=value` lines (POST needs the header `X-Phosphor: 1`) |
| `POST /update` | firmware update, needs the `X-Token` header (see above) |

## Security notes

- The device is meant for a home network. Pages, radio, brightness and messages can be changed by anyone on the same
  Wi-Fi, without a password. Only settings writes need a custom header (`X-Phosphor`), so other websites open in your
  browser cannot change them. Do not expose the device to the internet.
- Firmware updates, renaming and Wi-Fi reset over the network need `OTA_TOKEN`; the published builds have none, so
  those are off.
- There is no microphone code and no cloud account. The board talks to Open-Meteo (weather), NTP servers (time), the
  radio streams you choose (weather is fetched over plain HTTP) and, once a day, the installer page
  (`melihkoray.github.io`, HTTPS) to read the latest version number. The settings page in your phone's browser also
  talks to Open-Meteo geocoding and Radio Browser.
- The installer and the release files are built by GitHub Actions on GitHub's servers from this repository
  ([workflow](.github/workflows/build.yml)), not on anyone's computer. Since 22 Nov 2026 every build also gets a
  `SHA256SUMS.txt` and a signed [build provenance attestation](https://github.com/melihkoray/phosphor/attestations): to
  check that a `.bin` came from here, run `gh attestation verify phosphor.bin -R melihkoray/phosphor` (GitHub CLI).
  The v2.0.2 release `phosphor.bin` scans clean on [VirusTotal](https://www.virustotal.com/gui/file/9efc70694101580fcef897c6525e87db4b76faa0925c411bf40a54ddf860b0bd) (0 of 61 engines).

## Credits and license

GPL-3.0, see [LICENSE](LICENSE). Uses [ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S) (GPL-3.0,
vendored in `libraries/`), [QRCode](https://github.com/ricmoo/qrcode) (MIT, vendored in `libraries/`),
[U8g2](https://github.com/olikraus/u8g2) (BSD-2-Clause), [WiFiManager](https://github.com/tzapu/WiFiManager) (MIT),
[SensorLib](https://github.com/lewisxhe/SensorLib) (MIT), the Espressif AXS15231B display and touch drivers
(Apache-2.0) and [ESP Web Tools](https://esphome.github.io/esp-web-tools/) for the installer.

The pictures behind the scenes in the C64-style, Amiga-style and 2600-style looks were generated with an AI image
model (the prompts are in [`design/retro-proto/gen/`](design/retro-proto/gen)), then cut down to each machine's
resolution, palette and color limits by [`convert.py`](design/retro-proto/convert.py). The 8×8 fonts are drawn for
this project; no ROM fonts are copied. Commodore 64, Amiga and Atari 2600 are trademarks of their owners; the looks
are only in their style. Not affiliated with Waveshare.

Made by [Melih Koray](https://melihkoray.com) · [Project page](https://melihkoray.com/projects/phosphor)
