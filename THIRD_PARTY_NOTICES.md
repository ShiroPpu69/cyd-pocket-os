# Third-party notices

The root MIT license covers original project code, not every asset or dependency.

| Component | Source / terms |
| --- | --- |
| Modified SpotifyArduino client (`esp32_system/src/SpotifyArduino*`) | [Brian Lough / spotify-api-arduino](https://github.com/witnessmenow/spotify-api-arduino), MIT; full notice in `SpotifyArduino.LICENSE`. |
| Vendored ESP32Synth (`esp32_system/src/ESP32Synth`) | [danilogcrf2-oss / ESP32Synth](https://github.com/danilogcrf2-oss/ESP32Synth), MIT; full notice in that directory's `LICENSE`. Local changes bound driver waits and task shutdown. |
| Spotify integration inspiration | [Spotify-Diy-Thing](https://github.com/witnessmenow/Spotify-Diy-Thing), Brian Lough. Spotify names, service and artwork remain their respective owners'. |
| Buddy messaging inspiration | [openelab-commits / codex-buddy](https://github.com/openelab-commits/codex-buddy). No upstream desktop plugin or Mao sprite pack is distributed here. Includes a local NimBLE/NUS adapter. |
| Dota / Deadlock portraits and characters | Valve / respective rights holders. `hero_icons_rgb565.h`, `deadlock_hero_assets.h`, Axe-inspired Buddy sprite data and corresponding previews are not covered by MIT. Portraits obtained through the community API are not owned by this project. Replace game artwork with appropriately licensed assets for commercial products. |
| Runtime libraries | Installed separately: LovyanGFX, ArduinoJson, NimBLE-Arduino, JPEGDEC, AnimatedGIF and Arduino ESP32 core; upstream licenses apply. |

No complete yoRadio app is present in the firmware. Upstream checkouts, unused legacy sprite packs, personal configs/reports and compiled firmware are not distributed.

No affiliation or endorsement by Valve, Spotify, OpenAI or upstream maintainers is implied. Trademark ownership is unchanged. Rights holders can open an issue identifying any asset concern, without posting personal credentials.
