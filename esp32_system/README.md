# Firmware reference

See [the project overview](../README.md), [中文介绍](../README.zh-CN.md) and [build & flash guide](../docs/GETTING_STARTED.md).

## Board pin map

| Function | Pins / chip |
| --- | --- |
| MCU | ESP32-WROOM-32, 4 MB Flash |
| LCD | ILI9341, 240 × 320 |
| LCD SCK / MOSI / MISO | 14 / 13 / 12 |
| LCD CS / DC | 15 / 2 |
| Backlight | 21, PWM |
| Resistive touch | XPT2046 |
| Touch SCK / MOSI / MISO | 25 / 32 / 39 |
| Touch CS / IRQ | 33 / 36 |
| Speaker DAC | GPIO26 (check your board revision/amplifier) |

Hardware variations matter. Verify `LGFX_CYD.h` before using a different display revision.

## Source map

- `esp32_system.ino`: launcher, touch routing, app scheduling, Dota and Spotify UI.
- `home_apps.h`, `home_style.h`: app registry and launcher layout.
- `deadlock_app.cpp`: bounded streaming match fetch, cached cards and paging.
- `codex_buddy_ble.cpp`, `buddy_mao_animator.cpp`: NimBLE messaging and animation.
- `synth_app.cpp`: synthesizer UI and bounded audio teardown.
- `bounded_spotify_client.h`, `system_health.cpp`, `network_self_test.cpp`: cancellation, diagnostics and safety checks.
- `src/`: modified vendored libraries; licenses retained.

## Diagnostics

115200 baud. `diag` reports memory/task status; `selftest` runs malformed-download checks;
`stress 10` schedules 120 app transitions; `home` stops navigation tests and returns home.
The Windows script `tools/device_stability_test.ps1 -Port YOUR_PORT -Rounds 10` exercises controls and emits local logs. It triggers Spotify commands and synth notes: use low volume and avoid touching the screen during the test.
Its assertions assume an account with at least ten recorded Deadlock matches. It is not a generic CI test for arbitrary empty accounts. Personal logs must stay private.
