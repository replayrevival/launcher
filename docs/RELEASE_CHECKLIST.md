# Release checklist (launcher)

Pushing and publishing happen only with Miko's explicit go-ahead for that release. The go-ahead names what may be pushed; it does not carry over to the next release.

## 1. Build and package
1. Wait for the game session's `PROMOTED:` note (game repo `docs\NOTE_FOR_LAUNCHER_*.md`); take the exe from `ste-recomp\mission-l6\artifacts\` and check its version and SHA-256 against the note.
2. `scripts\package.ps1 -Version X -GameExe <exe> -Out E:\GodzillaSTE\release -BuildDir <dir>`. Move old outputs of the same version to `release\_to_delete\` first (the script refuses to overwrite).
3. Smoke-test an unzipped copy in a new scratch folder: each Button prompts value starts the game, no console window, `--game-console` works, no `[CRASH]`, nothing written outside the package and its data folder.
3b. Overlay (when the package has `ste_overlay.dll`): `scripts\smoke_overlay.ps1 -Pkg <unzipped package> -Game <extracted game folder>` (Back+Start chord opens it, a setting applies live, Return to launcher exits with code 20; the pad script must press at 60 s or later). Guide and F1 are keys the game reads: check by hand once.
4. Write the release notes (`RELEASE_NOTES-vX.md`): what's new, known issues, source links, SHA-256 of the zip and of the game exe. Keep DRAFT until the tests Miko names are done.

## 2. Screenshots (standing rule, every release)
1. **Launcher:** capture the main screens with the release build: Overview, Video, Controllers (with button pictures), Mods, the file browser, and the in-game overlay once it ships. Put them in `publish\launcher\docs\screenshots\`.
2. Before replacing them, move the previous set to `docs\screenshots\archive\<previous version>\` (never delete).
3. Update the **Screenshots** section of the launcher `README.md` so every image it shows is a current one, and check that `PLAYER_GUIDE.md` and `MODDING.md` still point at files that exist.
4. **Release page:** put 3 to 5 of the best current shots (game and launcher) into the release notes, taken from both repos' `docs\screenshots\`. Game screenshots must not contain game imagery that cannot be shown; use only ones the game session has cleared.
5. Screenshots show no personal text (user name, folders outside the package).

## 2b. READMEs (standing rule, every release)
- Both READMEs (`publish\ste-recomp\README.md`, `publish\launcher\README.md`) advertise the project first and are written for someone who has never heard of a recompilation: logo, one-line pitch, badges, hero image, what you get, play in 3 steps, what you need, what's new, FAQ, disclaimer. Technical detail stays, in the "For developers" part at the bottom.
- Every release: update **What's new in vX** and "Coming next", and refresh the screenshots they show (section 2). Check the links to the new release and the badges.
- Show Miko the README text before pushing; push only after her approval.

## 3. Checks before publishing
- Personal-text and no-game-data scan on both publish repos (`scripts\sync_publish.ps1` scans; also check tracked file types and sizes).
- Release notes have no leftover DRAFT marks; the zip's SHA-256 in the notes matches the file.

## 3c. Release media (standing rule, every release; Miko 2026-11-17)
For every release make a **trailer** (16:9, 60 to 90 s, 1080p60 and 4K60), a **Short** (9:16, 1080x1920, 60 fps, 30 to 45 s, under 60 s) and a **thumbnail** (1280x720) for the Replay Revival channel (https://www.youtube.com/@ReplayRevivalGames), each with a `.txt` (title, description, tags, pinned comment; the description has the download link and the disclaimer: unofficial fan project, no game files, you need your own copy). Output: `E:\GodzillaSTE\release\media\<version>\`. Miko reviews and uploads; we never upload.
1. Ask the game session for clips (`docs\MEDIA_CLIPS_REQUEST.md`; for the first one after a PROMOTED note) and put them in `media\<version>\clips\` with a `CLIPS.txt`.
2. Copy `scripts\media\manifest.example.json` to `media\<version>\manifest.json`, set clip names and start times, write the release-specific beats and the "what's new" text. Every caption must be true for that release (it says what the release notes say).
3. `python scripts\media\make_media.py media\<version>\manifest.json --draft` (fast 540p check), look at frames of every scene, then without `--draft` (1080p60 and 4K60 trailer, Short, thumbnail, chapters, audio stem, `.txt`). Check: no personal text on screen, no game logos or game files (rendered footage only), loudness about -18 LUFS (trailer) so a music track can be added, Short under 60 s and the first 2 s are the hook.
4. Report the folder and what to review. Tooling and what we do better than the first videos: `docs\MEDIA_PLAN.md`.

## 4. Publish (only after the explicit go-ahead)
- Back up the existing remote first if anything will be replaced.
- Push the repos, then create the release (pre-release while the project is alpha) with the zip and its `.sha256` on both repos so the update check sees it.
- Record what was pushed in `.agent-state\CHECKPOINT.md`.
