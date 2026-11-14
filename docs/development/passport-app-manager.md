**English** · [简体中文](passport-app-manager.zh_CN.md)

# Passport Runner

The status bar shows the time in UTC+8, the Wi-Fi state, and battery level.
The three home sections show the FoloToy logo or a large clock with a weather
strip, the app count and available storage, and the shortcuts below.
Use UP or DOWN to move the selection and OK to open it. Hold UP to return from
a manager page.

- **Develop** shows the browser address and eight-character connection code for
  app installation. Hold OK to start the setup hotspot when needed.
- **Apps** lists installed apps, including ESP-Claw if uploaded. Press OK to open
  an app or hold OK to manage it.
- **Settings** switches the home display between the logo and a clock, changes
  the theme between Passport green and orange, adjusts the manager's brightness,
  and opens the Wi-Fi setup page. Passport green is the default. Display, theme,
  and brightness choices persist across restarts.

Installed apps share 4,160 KiB of Flash. Each binary must fit the 2,368 KiB
shared boot partition. The **Passport Runner** web console opens on the app
desktop. Select an app to see its details, open its community page if available,
or remove it. The sidebar links to Wi-Fi, home weather, app storage, and
development. **App storage** shows total, used, and free space and the per-app limit.

## Home weather

Open **Home weather** in the browser manager, search for a city, and save a
result. The browser uses [Open-Meteo's city search](https://open-meteo.com/en/docs/geocoding-api).
While the Passport shows its home screen, it queries [MET Norway](https://api.met.no/weatherapi/locationforecast/2.0/documentation)
using the selected city's coordinates. The device shows rounded Celsius and a
translated summary from the forecast under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
Use **Turn off weather** to remove the saved city. The display shows an
unavailable state if it has no usable forecast.

## Install and run Pomodoro

1. Start the Passport and select **Develop**. If it is in
   hotspot mode, join the displayed Wi-Fi network. Use the eight-character
   connection code as the hotspot password.
2. Open the address displayed on the device and enter the same code. Under
   **Install app**, choose **Upload Passport app** and upload
   `Passport-Pomodoro.bin`. Use **Wi-Fi settings** to join an existing 2.4 GHz
   network. Open the address shown on the device after the switch.
3. Hold UP to return home, enter **Apps**, select **Pomodoro**, and press OK. Use
   UP/DOWN to choose 15, 25, or 45 minutes before a session. OK starts, pauses,
   or resumes the timer. A completed focus session offers a five-minute break.
4. Hold UP in either Pomodoro or ESP-Claw to return to the manager. Pomodoro
   saves its remaining time and reopens paused. The app list keeps installed
   images across a restart. To reclaim space, open the app's details on the web
   desktop and select **Remove app**.

When you open an uploaded app, the manager copies it to the shared boot
partition and restarts into it. The factory manager enables the ESP-IDF OTA
rollback mechanism and leaves each selected app pending verification. The
next reset, including a power cycle after an app stops responding, boots the
manager. Switching apps uses the same NVS partition for Wi-Fi, ESP-Claw settings,
and saved timer state. Each boot generates a new eight-character code.

## Configure Wi-Fi

In the web console, open **Wi-Fi settings** and select **Scan for nearby Wi-Fi**.
Choose a 2.4 GHz network from the list or enter its name manually for a hidden
network. Enter the password and select **Connect Wi-Fi**. An open network can
use a blank password. After the switch, open the address on the device screen.

Runner saves up to two networks. A network becomes preferred once it obtains an
IP address and its credentials are saved. Saving a third network replaces the
older entry. After a restart or a disconnect, the device tries the other saved
network if the first attempt times out. If neither works, it opens the setup
hotspot. Failed setup attempts preserve the saved credentials. **Clear saved
networks** removes both entries.

## Manage an app on the Passport

Use UP or DOWN to highlight an installed app. A short press of OK opens it.
Hold OK to view the app size. If the app has a community play ID, it appears
above the size. Select **Delete app**, then
move from the preselected **Cancel** to **Delete** and press OK. The Passport
returns to its app list after deletion. Refresh the web page to see the updated
app list and available space. You can upload the original app file again later.

## Remember this browser

On the login page, enter the code and select **Remember this browser (90 days)**.
Reopening the same address in that browser signs in automatically, including
after rebooting or switching between the manager and an updated ESP-Claw.
The setup hotspot and the LAN have different addresses, so pair once at the
LAN address you use regularly. A different browser or address needs its own
pairing. **Sign out** revokes the current pairing.

The Passport keeps up to eight pairings and replaces the oldest when a ninth
browser is added. The browser removes its saved login after 90 days or when
you clear its site data. The eight-character code is still needed for new
pairings and joining the setup hotspot. Use remembered login on your own
browser and a trusted local network.

## Import a community firmware download

Download the raw merged `.bin` from the [community](https://ai-passport.folotoy.cn/plays/).
On the app desktop, choose **Install app** → **Import community firmware** and
select the file. The browser checks and extracts the application locally.
Review its size and version, edit its name if desired, and select
**Install this app**.

Alternatively, choose **Install app** → **Install from a community play link**,
paste a [community play page](https://ai-passport.folotoy.cn/plays/1471/), and choose
**Download and install**.
Passport must be connected to Wi-Fi with Internet access. It streams the
official download to the browser, which verifies it against the published
SHA-256, extracts the app, and installs it if all checks pass. Only the
extracted app takes up space in the library. Open it from the device's app list.

The official play title appears in the web and device app lists. The device
uses the app identifier when its screen font lacks a character in the title.
Long-press OK on an app to see its community play ID. On the web desktop, select
the app and click the ID to open the official play page. To add this metadata to an older
installation, paste the same play link again. Runner updates the metadata when
the downloaded app matches the installed image byte for byte. A local `.bin`
has no official play metadata.

The web importer accepts raw bundles up to 8 MiB with one unsigned
ESP32-C3 factory app, a valid partition table and an ESP-IDF SHA-256 trailer.
It recognizes the legacy FoloToy `recovery` and `cardid` reservations when
their declared layouts match and the bundle carries no data for either slot.
The importer rejects other extra partitions and data outside the app. The
extracted image must fit the 2,368 KiB shared boot partition and the free space
shown on the device. Successful installation verifies the image format. The original
app controls remain in effect, so an app without a return gesture can return
to the manager by restarting the device.

The [local extractor](../../tools/extract_passport_app.py) is also available
from the project directory with Python 3:

```bash
python3 tools/extract_passport_app.py community-full.bin
python3 tools/extract_passport_app.py community-full.bin -o extracted.bin
python3 tools/extract_passport_app.py community-full.bin -o Passport-Pinball.bin --name Passport-Pinball
```

The first command lists the application partitions and image sizes. The
second saves the original app bytes. Use `--partition LABEL` if the image
contains several apps. The third command sets the app name inside the image
and updates its checksum and SHA-256 digest for upload. It leaves the original
download intact and refuses to overwrite an existing output file.

Use **Install app** → **Upload Passport app** to upload the file produced by `--name`.
The standalone tool can also inspect and extract images from some bundles
that the web importer declines. Test the app's screen, controls and
behavior on the Passport separately.

## Source layout

| Directory | Contents |
| --- | --- |
| `main/runner/` | Passport Runner firmware, app installation and launch, management web UI, weather, target configuration, and partition table |
| `main/apps/esp-claw/` | ESP-Claw app, model and Lua logic, and its web UI |
| `main/apps/pomodoro/` | Pomodoro app and timer state machine |
| `main/common/` | Shared persistent configuration and the build entry for the Chinese font |
| `main/common/network/` | Wi-Fi, browser pairing, and HTTP handling shared by Runner and ESP-Claw |
| `components/bsp/` | Board drivers and APIs |

The root CMake file selects the target through `PASSPORT_APP`: `manager` (default),
`claw`, or `pomodoro`. Each target lists its sources and assets in its own
`CMakeLists.txt`.

## Build and package

Activate ESP-IDF 5.5.3 and run `./tools/validate.sh`. The gate builds all three
apps and verifies the [8 MiB partition table](../../main/runner/partitions.apps.csv). It
archives the `0x0` merged image with the manager in `factory`, an empty
LittleFS `app_store`, and a blank `app_run` in `ota_0`. Use
`build/FoloToy-AI-Passport-full.bin` for a new installation. Its matching
manager ELF is under `build/firmware/`. The gate prints the paths to the
separate `Passport-ESP-Claw.bin` and `Passport-Pomodoro.bin` uploads under
`build/apps/`, where it also archives their debug artifacts.

A merged flash overwrites NVS and the app store. For an existing installation
with the same partition table, updating only the manager at `0x20000` preserves
Wi-Fi, browser pairings, and installed apps.
[Flashing and stored data](engineering/firmware-layout.md#flashing-and-stored-data)

## Develop an app

On a computer with ESP-IDF 5.5.3, build a standalone ESP32-C3 application
image for the Passport layout and BSP. Use a project name that starts with `Passport-`, with an
ASCII letter, digit, hyphen, or underscore suffix and a total of at most 31
characters. Upload the application `.bin` with its ESP-IDF image SHA-256 trailer.
The manager checks the target, size, and complete image before listing it. A
compatible app leaves its OTA boot state pending and calls `esp_restart()` for
the return gesture. Install native images you trust, as apps run with firmware
access to the device.
