# Security & privacy

Hobby firmware, not a hardened credential vault.

- `esp32_system/user_config.h` and `spotify_config.h` are local-only. Never force-add them.
- Firmware embeds those values. Never publish personal `.bin`, `.elf`, `.map`, build directories or serial logs. Build from examples.
- Account IDs, match history, BLE IDs and local paths can identify users. Redact screenshots, issues and diagnostics.
- HTTPS calls `setInsecure()`: certificates are not checked. A network attacker may intercept tokens. Use a trusted network; certificate validation is a priority contribution.
- The Spotify OAuth helper listens on `127.0.0.1:8888` and checks OAuth state. It writes secrets to a local header. Protect it and revoke compromised tokens.
- BLE is encrypted; wider desktop interoperability testing is needed. Pair only with trusted devices.

Before committing, run `python tools/check_public_tree.py`. It scans staged content, rejects private paths/common secret patterns, and compares data against local config values without printing them. A guardrail, not a comprehensive audit.

Do not post secrets in public issues. Use GitHub private vulnerability reporting when available. If a secret is exposed, revoke it first; deletion does not remove Git history or existing clones.
