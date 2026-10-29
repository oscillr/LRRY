# Publishing DUELL (PRIVATE note, not exported)

## FIRMWARE BETA RELEASE (0.2.13)

Do this only after you have tested the 0.2.13 flash on your unit. Do not rebuild or bump anything: the files below are final (VERSION 0.2.13). The repository is already public and Pages is already set to GitHub Actions.

Files in the private DUELL folder (none of them is exported):
- `felucca-base/build/duell-0.2.13-beta.fwsc`, SHA-256 `f0c2f71db9bdcfc7fd84f9d8ceae0eb3cdad1d080247ab30eabe7d99c997ed1c`
- `duell-emulator-0.2.13-beta.zip` (repo root), SHA-256 `3d7d7c14ab9fd8202ca5d2e6318ebd0164cbb2fdf49dc5b74a501766c8a92cd0`
- `SHA256SUMS` (repo root; lists both file names with these hashes; the dry run shows it as UNCLASSIFIED, which is right: it is a release asset, not repo content)
- `RELEASE_NOTES.md` (the release text)

Steps, in order:

1. **Check the files** (nothing must differ from the hashes above):
   ```bash
   shasum -a 256 felucca-base/build/duell-0.2.13-beta.fwsc duell-emulator-0.2.13-beta.zip
   ```
   Copy the .fwsc next to the other two so you can attach all three from one folder (copy, not move).
2. **Dry run**, then the snapshot (docs, README, CHANGELOG and the installer text are in the working tree, not yet in DUELL-public):
   ```bash
   python3 export_public.py                       # safety checks must pass; UNCLASSIFIED: SHA256SUMS, M-UPGRADE.dmg, reddit_images/ are expected and stay private
   python3 export_public.py --commit "DUELL 0.2.13 beta"
   ```
   Check `git log` in `../DUELL-public`: author DUELL only.
3. **Push**: `cd ../DUELL-public && git push`. The Pages workflow starts by itself from the push (it still has no package, so the installer shows the "not yet released" notice for now, or keeps the previous release's files).
4. **Create the release** on GitHub: Releases > Draft a new release; tag `v0.2.13-beta` (create it, target `main`); title `DUELL 0.2.13 beta`; tick **Set as a pre-release**; description: the text of `RELEASE_NOTES.md` below its `---` line. Attach all three assets: `duell-0.2.13-beta.fwsc`, `duell-emulator-0.2.13-beta.zip`, `SHA256SUMS`. Publish (not a draft: the workflow ignores drafts).
5. **Run the Pages workflow by hand** from main: Actions > Pages > Run workflow, branch main. A release event does not start it (a run from the tag is refused at the deploy step). What the workflow takes: the **newest published release** (`gh release list --exclude-drafts --limit 1`, pre-releases count), from it every file matching `duell-*.fwsc` and `duell-emulator*.zip`; the highest version wins if several. `make_site.py` then writes the package into `/install/` as `duell-<VERSION>-beta.fwsc` plus `duell-latest.fwsc` and `duell-version.json`, and unzips the emulator into `/emu/`. `SHA256SUMS` is not fetched by the workflow. Because it uses the newest release, a later emulator-only release would remove the package from the installer: always attach the .fwsc and the emulator zip to every release.
6. **Check the hosted pages** (reload with a cache bypass):
   - <https://oscillr.github.io/DUELL/install/>: the version reads 0.2.13 beta, the flow (checklist, BACK UP, INSTALL DUELL, RETURN cards) is visible, and the package line shows the SHA-256 `f0c2f71d...97ed1c`. The hosted installer shows the **"DUELL firmware is not yet released"** notice only when no package is found; if you still see it, the workflow found no `.fwsc` in the newest release (check the asset name and that the release is published, then run the workflow again).
   - <https://oscillr.github.io/DUELL/emu/>: starts, sound after one click, the page shows 0.2.13 beta, "Download saved data" works.
   - `https://oscillr.github.io/DUELL/install/duell-latest.fwsc` downloads, and `shasum -a 256` of that download equals the package hash above; `.../install/duell-version.json` names 0.2.13.
   - <https://oscillr.github.io/DUELL/> (landing): see the note below.
7. **Landing page text.** `felucca-base/web/landing.html` still says "the firmware package follows after more hardware testing", "Installer (firmware coming soon)" and "worked once". Change those lines before step 2 (it is source, not generated), otherwise the landing page contradicts the release.
8. Then announce (docs/BETA_CALL.md has the text). Edit a release later = run the Pages workflow by hand again.

---

## EMULATOR-FIRST PUBLISH (0.1 beta)

Decision: the browser emulator goes public first; the repository becomes public now. The firmware package (`.fwsc`) and the installer flashing come later, after the hardware tests (return to official V15 and more). Until then the installer page shows "DUELL firmware is not yet released" and its flow stays hidden.

**(a) Export the snapshot** (after the final build, when the firmware helper is done):

```bash
python3 export_public.py                       # dry run first: safety checks, list of changes, UNCLASSIFIED lines
python3 export_public.py --commit "DUELL 0.1 beta (emulator first)"
```

**(b) The emulator zip.** Rebuild the emulator first if sources changed (`. ~/emsdk/emsdk_env.sh; python3 gen_only.py; sh felucca-base/web/emu/build.sh`, with `EMU_PREVIEW=0` to drop the "(preview)" mark if wanted). The page needs only `index.html`, `felucca.wasm`, `worklet.js` and `fonts/` (DotGothic16 subset + OFL.txt). `native.f32` and `native_check` in `build/emu` are test files: leave them out. `index.html` must sit at the zip root:

```bash
cd felucca-base/build/emu
zip -r ../../../duell-emulator-0.1-beta.zip index.html felucca.wasm worklet.js fonts -x '*.DS_Store'
cd ../../..
unzip -l duell-emulator-0.1-beta.zip     # check: index.html at the root, no folder prefix
```

The zip ends up in the private DUELL folder (`DUELL/duell-emulator-0.1-beta.zip`; `*.zip` is never exported). Keep that name: the workflow looks for `duell-emulator*.zip`.

**(c) First push** (once; this is the only step that contacts GitHub besides the web steps below):

```bash
cd "../DUELL-public"
git remote add origin https://github.com/oscillr/DUELL.git
git branch -M main
git push -u origin main
```

**(d) On GitHub.**
1. Settings > General > Danger Zone > Change visibility > Make public.
2. Settings > Pages > Source: **GitHub Actions**.
3. Releases > Draft a new release: tag `v0.1-beta` (target `main`), title `DUELL 0.1 beta (emulator)`, tick **Set as a pre-release**. Description: what the emulator is, that the firmware follows after more hardware testing, "no firmware download yet", no warranty, not affiliated with M-VAVE. Attach `duell-emulator-0.1-beta.zip` only. Publish.
4. The **Pages** workflow runs on every push to main, but it does NOT start from a release (a run from the release tag is refused at the deploy step by the github-pages environment). After publishing or editing a release, run it by hand: Actions > Pages > Run workflow, branch main. Settings > Pages > Source must be GitHub Actions first.
5. Check <https://oscillr.github.io/DUELL/> (landing), <https://oscillr.github.io/DUELL/emu/> (emulator starts, sound after one click, "Download saved data" works) and <https://oscillr.github.io/DUELL/install/> (shows the "not yet released" notice, no flow).

**(e) Pre-publish checklist**
- [ ] `python3 export_public.py` dry run is clean; it searches every exported file for personal data (names, paths, other e-mail addresses). Run one more search by eye for your own name or e-mail in README, docs and `git log` of DUELL-public (author must be DUELL only).
- [ ] No UNCLASSIFIED line left unexplained.
- [ ] README reads right: status banner on top, no claim that the firmware is available, the emulator and repo links work.
- [ ] Landing page, installer notice and docs say the same thing (emulator first; one unit tested; return path untested).
- [ ] Emulator zip checked with `unzip -l` (index.html at the root).
- [ ] Do NOT: attach a `.fwsc` to the release (the workflow would publish it into /install/ and the flow would appear); push before the dry run; commit `build/`, `*.zip`, `*.wasm`; publish flashing instructions; mention anything private.
- [ ] After publishing: open one issue with the bug form template yourself to check the chooser works, then close it.

When the firmware is ready: add `duell-<VERSION>-beta.fwsc` to a new release (or edit this one), keep the pattern, publish; the Pages workflow then enables the installer. Update README, INSTALL, RECOVERY, KNOWN_ISSUES first.

---

## Notes for later releases (general)

This working folder (DUELL) stays private forever. What is public is a separate folder, `../DUELL-public`, with its own git history (one snapshot commit per release). It is filled by `export_public.py` from a whitelist. The public repository is https://github.com/oscillr/DUELL (pushed from ../DUELL-public).

## Version (one source of truth)

The file `VERSION` at the DUELL root holds `0.MINOR.BUILD` (now `0.2.1`); everything shows "DUELL 0.MINOR.BUILD beta" from it: the device (splash, ABOUT, INFO; `tools/gen_version.py` writes `build/gen/duell_version.h`, also run by `gen_only.py`, `tools/build.py`, `host_tests.sh` and `web/emu/build.sh`), the package `duell-<VERSION>-beta.fwsc`, `duell-latest.fwsc` and `duell-version.json` (written by `tools/build.py` into `felucca-base/build/`, published by `make_site.py` into `/install/`), the installer page (reads `duell-version.json` at run time) and the emulator page (baked in by `web/emu/build.sh`). `VERSION` is exported; `bump_version.py` is not.

- `python3 bump_version.py build`: 0.2.1 -> 0.2.2. Every different package or emulator build gets a new BUILD number; run it before building something to hand over, never reuse a number for different contents.
- `python3 bump_version.py minor`: 0.2.7 -> 0.3.1. Only when the owner says a new line of updates starts.
- `python3 bump_version.py show`: print it.
- Name the release assets with the same version: `duell-0.2.1-beta.fwsc`, `duell-emulator-0.2.1-beta.zip`, tag `v0.2.1-beta`, title `DUELL 0.2.1 beta`. The Pages workflow takes any version from the newest release (highest version if there are several).
- The first published emulator stays "0.1 beta" (0.1.0).

## Every release

1. Edit and test in DUELL as usual (`./host_tests.sh`). Rebuild the firmware package and the emulator if they changed.
2. Dry run: `python3 export_public.py`. It runs the safety checks (forbidden words, other email addresses, files over 2 MB) and lists new, modified and deleted files. Look at any `UNCLASSIFIED` line: add that file to the WHITELIST or BLACKLIST at the top of the script (unclassified files are never exported).
3. Snapshot: `python3 export_public.py --commit "DUELL 0.2.1 beta ..."` (the version of the `VERSION` file). (`--write` syncs without committing.)
4. Optional self-check in `../DUELL-public`: `DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib <DUELL>/.venv/bin/python gen_only.py` and `./host_tests.sh regress sounds ui select patch splash`.

## First push (once)

```bash
cd "../DUELL-public"
git remote add origin https://github.com/oscillr/DUELL.git
git push -u origin main
```

Later releases: run step 3, then `git push` in DUELL-public.

## GitHub Pages (once)

Settings > Pages > Source: **GitHub Actions**. The workflow **Pages** builds the site from the sources and takes the package and the emulator from the newest release (see `.github/workflows/pages.yml`). Site: <https://oscillr.github.io/DUELL/>.

## Firmware release (general recipe; the 0.2.13 steps are at the top)

Make the files in DUELL (they are not exported):

```bash
(cd felucca-base/build/emu && zip -r ../../../duell-emulator-$(cat VERSION)-beta.zip index.html felucca.wasm worklet.js fonts -x '*.DS_Store')
shasum -a 256 felucca-base/build/duell-$(cat VERSION)-beta.fwsc
```

On GitHub: Releases > Draft a new release; tag `v<VERSION>-beta` (e.g. `v0.2.1-beta`, target `main`); title `DUELL <VERSION> beta`; tick pre-release; description from `RELEASE_NOTES.md` (private) plus the SHA-256 and the safety words (tested on one unit only, return path status, own risk, no warranty, not affiliated with M-VAVE, link to docs/RECOVERY.md). Attach `duell-<VERSION>-beta.fwsc` AND `duell-emulator-<VERSION>-beta.zip` (the workflow reads only the newest release, so a firmware release must carry the emulator zip too) (keep this pattern: the workflow looks for `duell-*.fwsc` and `duell-emulator*.zip`, any version in the name). Publish; the Pages workflow then fills in the installer and the emulator.

## Checklist

- [ ] Dry run clean, no UNCLASSIFIED left unexplained
- [ ] Snapshot committed in DUELL-public, `git log` shows only author DUELL
- [ ] Pushed, Pages source set to GitHub Actions
- [ ] Release published with both assets and the SHA-256
- [ ] Installer and emulator pages open and work; issue chooser shows the bug form
