# Build & flash

Run commands from the repository root. Use Arduino CLI on your PATH. Target: ESP32-2432S028R with ILI9341, XPT2046 resistive touch, 4 MB Flash, no PSRAM.

## 1. Dependencies

Tested versions (also used by CI):

```sh
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32@3.3.11 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli lib install "LovyanGFX@1.2.26" "ArduinoJson@7.4.3" "NimBLE-Arduino@2.5.0" "JPEGDEC@1.8.4" "AnimatedGIF@2.1.1"
```

See [pin map](../esp32_system/README.md). Different display/touch revisions need changes to `LGFX_CYD.h`.

## 2. Local configuration

PowerShell:

```powershell
Copy-Item esp32_system/user_config.example.h esp32_system/user_config.h
Copy-Item esp32_system/spotify_config.example.h esp32_system/spotify_config.h
```

Linux/macOS: use `cp` instead. Do not overwrite an existing configured file without backing it up privately. Edit Wi-Fi credentials and `DOTA_ACCOUNT_ID`: a Steam **32-bit account ID**, shared by Dota and Deadlock, not a SteamID64 or vanity URL. Leaving placeholders allows a build but not live data.

Both real headers are ignored. Never use `git add -f` on them. Firmware contains their contents, so **do not share your compiled images**.

## 3. Spotify authorization (optional)

1. Create your own app in the [Spotify developer dashboard](https://developer.spotify.com/dashboard).
2. Register the exact redirect URI `http://127.0.0.1:8888/callback`.
3. With Python 3 installed, run `python tools/spotify_auth.py` and enter your Client ID/Secret locally. It opens a browser, checks OAuth state and saves the refresh token only to the ignored local header.
4. Rebuild after authorization. Start playback on your phone/computer before testing controls.

The firmware is a remote, not an audio-streaming Spotify client. Playback controls require Premium ([Spotify reference](https://developer.spotify.com/documentation/web-api/reference/skip-users-playback-to-next-track)). Development-mode access and playlist policies can change; consult current Spotify docs. Unauthorized accounts, expired tokens and absent playback devices cause errors rather than local music playback.

**Security limitation:** firmware TLS currently skips certificate verification. Only use this prototype on trusted networks; protect/revoke credentials appropriately.

## 4. Compile and upload

```sh
arduino-cli compile --jobs 4 --fqbn esp32:esp32:esp32:PartitionScheme=huge_app esp32_system
arduino-cli board list
arduino-cli upload -p YOUR_SERIAL_PORT --fqbn esp32:esp32:esp32:PartitionScheme=huge_app esp32_system
```

Replace `YOUR_SERIAL_PORT` with the discovered device port. Install your board's USB-UART driver if it does not appear. Close serial monitors before flashing. The sketch includes a 3 MB app partition for 4 MB Flash; no OTA dual-slot storage is available.

Tap four calibration targets on first boot. Settings persist in NVS. Swipe horizontally to change launcher pages; swipe up from the **bottom edge** at least about 55 pixels to return home. Inside Spotify use its on-screen directional controls.

## 5. Diagnostics

Use 115200 baud and commands `diag`, `selftest`, `home`. `stress 10` runs 120 scripted transitions and can trigger audio/network actions. On Windows:

```powershell
./tools/device_stability_test.ps1 -Port YOUR_SERIAL_PORT -Rounds 10
```

The script expects ten recorded Deadlock matches for your configured account, so empty history is not a firmware failure. Logs stay in ignored `build/`. Redact identifiers before sharing any excerpts. Never operate the screen simultaneously with automated tests.

For local layout previews install Pillow (`python -m pip install Pillow`). `preview_home.py` uses your Arduino LovyanGFX font installation; `preview_deadlock.py --account-id YOUR_ACCOUNT_ID` fetches your history and writes private previews to `build/`. Do not publish those previews without redacting your data.
