# Contributing

Small, reproducible contributions welcome. Open an issue first for new board profiles, applications or major UI changes.

1. Follow [setup](docs/GETTING_STARTED.md); keep real config local.
2. Keep drawing on the UI thread, bound network/audio jobs and cancel safely on exit. Follow [the contract](esp32_system/APP_CONTRACT.md).
3. Compile with pinned versions; describe hardware, reproduction steps and tests.
4. For visual changes, provide redacted images or labeled host previews. For lifecycle changes, run diagnostics when hardware is available.
5. Stage intended files only and run `python tools/check_public_tree.py`. No personal firmware or logs.

Useful first contributions: configurable timezone, app translations, error messages, documentation and alternate pin profiles. Certificate validation and lifecycle changes need careful tests.

Do not claim an integration is tested unless tested. Document sources/terms for new art. Respectful collaboration matters more than feature count.
