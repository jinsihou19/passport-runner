**English** · [简体中文](esp-claw-passport.zh_CN.md)

# ESP-Claw on AI Passport

The Passport runs the ESP-Claw Agent loop on its ESP32-C3 and sends model requests to an OpenAI-compatible Chat Completions endpoint. The port uses the upstream capability registry and event router with the Passport BSP for display, keys and battery. ESP-Claw boots as a standalone app and uses internal RAM.

## Connect and chat

1. Upload `Passport-ESP-Claw.bin` through the [app manager](passport-app-manager.md), then select ESP-Claw on the device. If its connection screen shows a hotspot, join it using the eight-character connection code as its password. Open the displayed HTTP address and enter the same code in the browser. Hold UP to return to the app manager.
2. Under **Device settings**, enter the model base URL (often ending in `/v1`), the model name and its API key if required. Choose a model and endpoint that support Chat Completions tool calls. The device appends `/chat/completions` to the base URL. HTTPS validates the server certificate with the ESP-IDF certificate bundle. The paired browser supplies its time on the first HTTPS request if network time is unavailable. HTTP is available for an explicitly configured private IPv4 address on a trusted LAN.
3. Enter your 2.4 GHz Wi-Fi name and password, then select **Connect to network**. The device switches to station mode and saves the new network after receiving an IP address. If connection fails, it returns to its hotspot and retains any previously working credentials. After a successful connection, put your browser on the same LAN and open the new address on the device screen. The code rotates on reboot.
4. Open **Chat** and send a prompt. A request can read the device status, adjust the backlight, run a Lua script or save a script for the OK key. Chat keeps the latest successful exchange as context in RAM. **New chat** clears it.

Select **Remember this browser (90 days)** when signing in to reopen the same address without typing the code. The updated ESP-Claw and manager share the pairing. [Remember this browser](passport-app-manager.md#remember-this-browser)

The web console runs over local HTTP. Configure model credentials from the password-protected setup hotspot or on a trusted LAN. Wi-Fi credentials, the model configuration and the saved key script live in the device's NVS. The web API returns whether a model key is saved.

## Screen and keys

The connection screen shows the network state, browser address and connection code. The assistant screen shows model readiness and the saved script's status. Tap **Up** or **Down** to switch screens. Hold **OK** on the connection screen to reopen setup while idle. A short press on the assistant screen runs the saved script. **Run once** in the browser uses the same event route. Use **Device settings** to delete a script or clear saved Wi-Fi.

Try asking “Bind OK to set the screen brightness to 35%.” The script can use Lua language syntax and the functions `passport.battery()` (a percentage or nil) and `passport.backlight(percent)` (10–100). Scripts are limited to 768 UTF-8 bytes, 16 KiB of Lua allocations and 10,000 instructions. This port handles one request at a time and shows free-form Chinese conversation in the browser. The LCD font covers the fixed Chinese labels and ASCII connection details.

## Build and verify

Activate ESP-IDF 5.5.3 and run `./tools/validate.sh`. The gate builds the [manager and its apps](passport-app-manager.md#build-and-package), checks inputs and font coverage, and produces `build/FoloToy-AI-Passport-full.bin` for offset `0x0`. The ESP-Claw ELF and MAP are archived with its app image under `build/apps/`. [Firmware layout and stored data](engineering/firmware-layout.md#flashing-and-stored-data)

The imported ESP-Claw revision and Apache license are recorded in [the upstream notice](../../third_party/esp-claw/NOTICE). The CJK subset is generated from Noto Sans CJK SC Regular. [Font assets and regeneration](../../assets/README.md#fonts)

The C3 port loads the built-in event rule from Flash and keeps the script in NVS. It limits model response sizes, tool counts and output buffers for internal RAM, keeps model credentials on their configured endpoint by rejecting redirects, and removes chat and tool contents from serial logs.
