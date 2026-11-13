# Release checklist (launcher)

Pushing and publishing happen only with Miko's explicit go-ahead for that release. The go-ahead names what may be pushed; it does not carry over to the next release.

## 1. Build and package
1. Wait for the game session's `PROMOTED:` note (game repo `docs\NOTE_FOR_LAUNCHER_*.md`); take the exe from `ste-recomp\mission-l6\artifacts\` and check its version and SHA-256 against the note.
2. `scripts\package.ps1 -Version X -GameExe <exe> -Out E:\GodzillaSTE\release -BuildDir <dir>`. Move old outputs of the same version to `release\_to_delete\` first (the script refuses to overwrite).
3. Smoke-test an unzipped copy in a new scratch folder: each Button prompts value starts the game, no console window, `--game-console` works, no `[CRASH]`, nothing written outside the package and its data folder.
4. Write the release notes (`RELEASE_NOTES-vX.md`): what's new, known issues, source links, SHA-256 of the zip and of the game exe. Keep DRAFT until the tests Miko names are done.

## 2. Screenshots (standing rule, every release)
1. **Launcher:** capture the main screens with the release build: Overview, Video, Controllers (with button pictures), Mods, the file browser, and the in-game overlay once it ships. Put them in `publish\launcher\docs\screenshots\`.
2. Before replacing them, move the previous set to `docs\screenshots\archive\<previous version>\` (never delete).
3. Update the **Screenshots** section of the launcher `README.md` so every image it shows is a current one, and check that `PLAYER_GUIDE.md` and `MODDING.md` still point at files that exist.
4. **Release page:** put 3 to 5 of the best current shots (game and launcher) into the release notes, taken from both repos' `docs\screenshots\`. Game screenshots must not contain game imagery that cannot be shown; use only ones the game session has cleared.
5. Screenshots show no personal text (user name, folders outside the package).

## 3. Checks before publishing
- Personal-text and no-game-data scan on both publish repos (`scripts\sync_publish.ps1` scans; also check tracked file types and sizes).
- Release notes have no leftover DRAFT marks; the zip's SHA-256 in the notes matches the file.

## 4. Publish (only after the explicit go-ahead)
- Back up the existing remote first if anything will be replaced.
- Push the repos, then create the release (pre-release while the project is alpha) with the zip and its `.sha256` on both repos so the update check sees it.
- Record what was pushed in `.agent-state\CHECKPOINT.md`.
