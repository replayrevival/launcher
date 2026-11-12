# What the launcher sends over the network

Nothing, except one optional request: the **update check**.

- Once a day at start-up (and only when "Check for updates" on the Overview tab is ON) the launcher asks `https://api.github.com/repos/<owner>/<repo>/releases`
  (the repository named in `config/profile.json` under `update.repo`) for the list of releases. It sends the usual HTTPS request with the
  user agent `recomp-launcher/<version>` and no other identifying information; GitHub sees the IP address, as with any web request.
- It reads the newest release tag and page link. If the tag is newer than the launcher's own version the Overview tab says "A new version is
  available" and offers a button that opens the release page in your browser. Only `https://github.com/...` links are ever opened.
- Nothing is downloaded or installed. When you are offline, behind a blocking proxy, or GitHub answers anything but a release list, the launcher
  stays quiet and tries again the next day. The request has a 5 second limit and runs on a background thread, so it never delays the start.
- Switch it off with the "Check for updates" row (saved in `launcher_state.json`).
- Developer overrides: environment variable `LAUNCHER_UPDATE_URL` (a different URL, or `off`).

The crash report is a zip file on your desktop that **you** send; the launcher never uploads it.
