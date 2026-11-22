# Contributing

Bug reports and ideas are welcome as issues. Please use the forms offered when you open one, and report security
problems privately instead, as [SECURITY.md](SECURITY.md) says.

This project is proprietary (see [LICENSE](LICENSE)). A pull request can be accepted only once you have agreed in writing
that its copyright passes to the maintainer, so the project stays under one owner. Ask in an issue before you start on
one.

## Building and testing

No hardware or NVR is needed for the tests; everything runs on loopback. On Windows, build from VS Code or PowerShell,
because the toolchain's setup refuses to run under Git Bash.

Each end-to-end test stands on its own. It starts from a quiet hub with the six test sensors in place
(`quiet_hub` in `tools/e2e_common.py`), sets up anything else it needs, and puts back what it changed. A test that
needs a paired node pairs one of its own (`PairedNode` in `tools/e2e_nodes.py`), which numbers its own reports. So
a test can run alone or in any order, and a failure names the tests to run again. Run only one test or a few while
working on them, and the whole set before a commit. Don't run two of these at once: they use the same ports.

```
pio test -e native            # the unit tests
pio run -e host               # the PC build that the next two drive
python tools/e2e_test.py      # the end-to-end test, playing the NVR's part
python tools/e2e_pages.py     # the pages in a real browser: pip install playwright==1.52.0, then
                              # python -m playwright install chromium
python tools/e2e_test.py node  # only the tests with "node" in their names; --list names them all
pio run -e esp32_16mb         # the firmware for the board you have; CI builds the others
pio run -e host -t compiledb  # then python tools/tidy.py: clang-tidy's checks, as .clang-tidy lists them
python tools/precheck.py      # CI's quick checks: C++ and page style, settings and colours; --fix formats.
                              # Run before each push once you have run: git config core.hooksPath tools/hooks
python tools/update_hub.py HOST  # build, sign and send the firmware to a hub on the network, and watch that it
                              # keeps it; refuses a commit CI has not passed, and while an alert is active
python tools/compare_pictures.py  # after python tools/screenshots.py: whether a page changed without its pictures
                              # being committed again, as CI checks; pip install pillow
python tools/coverage.py      # how much of the library the tests run: .pio/coverage/report/index.html;
                              # pip install gcovr, and add --pages for the browser tests too
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
  script. CI refuses either otherwise; `python tools/precheck.py` runs the same checks here, and the pre-push hook
  in `tools/hooks` runs it before each push.
- **A hub is updated only with a build CI has passed.** The PC tests cannot show the board's stack or timing, and CI
  is the least a build must pass before it guards a home. `tools/update_hub.py` refuses anything else unless told
  `--untested`, and watches the hub until it keeps the new firmware or says it went back.
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
- **Every phrase of the pages has its Portuguese.** The English stays in the code as the key: a script's words go
  through `tr("...")` (with `{name}` for what changes, `trCount` for a count, `trParts` for a sentence holding a field),
  a message from the hub through `hubText`, and `web/pt.js` has the Portuguese of each. `tools/check_translations.py`,
  run with the quick checks, fails while any phrase of the pages, or any message the hub sends them, has none, or while
  English in a script skips `tr()`; a code or a name is marked `// not words`.
- **Secrets stay out of git.** Passwords go only in `include/secrets.h`, which is not kept in git.

## Releases

A release is a tag such as `v0.2.0` on `main`. Pushing the tag builds the firmware for every board and publishes it,
with that version's part of [CHANGELOG.md](CHANGELOG.md) and the third-party notices.
The update files are signed with the `FIRMWARE_SIGNING_KEY` secret, whose public half is `FIRMWARE_PUBLIC_KEY` in
`include/config.h`, and the release lists their SHA-256, carries a software bill of materials made from
THIRD_PARTY_NOTICES.md by `tools/sbom.py`, and has GitHub's build attestation.

A hub takes an update over the network only when it is signed with the key it trusts, so a build of your own goes on
over the USB cable (`pio run -e esp32_16mb -t upload`). To update your hubs over the network with your own builds, make a
key with `python tools/sign_firmware.py --new-key`, put its public half in `secrets.h` as `FIRMWARE_PUBLIC_KEY`, flash
that build over USB once, and sign each later build with `python tools/sign_firmware.py firmware.bin signed.bin`.

A new release of the Arduino core or ESP-IDF, or a new library, needs its entry in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and its licence text in `licenses/`. Until 1.0.0, a new minor version may change settings or the
messages; from 1.0.0, versions follow [semantic versioning](https://semver.org).
