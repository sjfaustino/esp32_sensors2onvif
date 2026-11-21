# Contributing

Bug reports and ideas are welcome as issues. Please use the forms offered when you open one, and report security
problems privately instead, as [SECURITY.md](SECURITY.md) says.

This project is proprietary (see [LICENSE](LICENSE)). A pull request can be accepted only once you have agreed in writing
that its copyright passes to the maintainer, so the project stays under one owner. Ask in an issue before you start on
one.

## Building and testing

No hardware or NVR is needed for the tests; everything runs on loopback. On Windows, build from VS Code or PowerShell,
because the toolchain's setup refuses to run under Git Bash.

```
pio test -e native            # the unit tests
pio run -e host               # the PC build that the next two drive
python tools/e2e_test.py      # the end-to-end test, playing the NVR's part
python tools/e2e_pages.py     # the pages in a real browser: pip install playwright==1.52.0, then
                              # python -m playwright install chromium
pio run -e esp32_16mb         # the firmware for the board you have; CI builds the others
sh tools/fuzz.sh              # the parsers under libFuzzer, on Linux or macOS with clang, as CI runs them; on
                              # Windows: powershell -ExecutionPolicy Bypass -File tools\fuzz.ps1, after
                              # winget install LLVM.LLVM, with Visual Studio's C++ build tools
```

[docs/how-it-works.md](docs/how-it-works.md) explains where things are and why, and
[docs/protocol.md](docs/protocol.md) describes the ESP-NOW messages.

## The rules every change keeps

- **No alert is missed.** A false alert is better than a missed one. A report is confirmed only once it is on storage,
  and nothing that confirms an alert may need the heap.
- **Tests come with the change.** A fix adds a test that fails without it; a new behaviour gets checks in the unit or
  end-to-end tests.
- **The style is checked.** C++ with clang-format 19.1.7, using `.clang-format`. The page scripts and styles with
  `npx prettier@3.3.3 --print-width 120 --end-of-line lf --write web/*.js web/*.css`, and `node --check` on each
  script. CI refuses either otherwise.
- **The documents follow the code.** `docs/user-guide.md` for what a user sees, with its pictures
  taken again by `python tools/screenshots.py` when a page changes, `docs/how-it-works.md` for how it works, and
  `docs/protocol.md` for the messages. Every time the hub waits for is in `lib/sensorcam/cam_timing.h`.
- **Commit messages say what changed for the hub**, in a sentence, such as "A scheduled restart waits for alerts to
  end", with the reason in the body.
- **A colour is named, light and dark.** The pages' stylesheets set every colour once, as a variable in their
  `:root` blocks, with its dark value beside it, and the rules use only those names. `tools/check_colours.py`, run by
  CI, refuses a colour written anywhere else.
- **A new setting starts in the table.** Every setting kept in flash is one line of `CAM_SETTINGS` in
  `lib/sensorcam/cam_settings.cpp`: its key, its kind and what a good value is. The page's checks and the backup's come
  from it, and `tools/check_settings.py`, run by CI, fails until `src/hub_settings.cpp` loads and saves it.
- **Secrets stay out of git.** Passwords go only in `include/secrets.h`, which is not kept in git.

## Releases

A release is a tag such as `v0.2.0` on `main`. Pushing the tag builds the firmware for every board and publishes it,
with that version's part of [CHANGELOG.md](CHANGELOG.md) and the third-party notices.
A new release of the Arduino core or ESP-IDF, or a new library, needs its entry in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and its licence text in `licenses/`. Until 1.0.0, a new minor version may change settings or the
messages; from 1.0.0, versions follow [semantic versioning](https://semver.org).
