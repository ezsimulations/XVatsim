# XVatsim Update Manifest

This folder is intended to be served by GitHub Pages from the `docs` directory.

Expected public manifest URL:

```text
https://ezsimulations.github.io/XVatsim/xvatsim_update.json
```

The plugin fetches `xvatsim_update.json`, compares `latest_version` to the
installed plugin version, and shows a notify-only update message with the public
release/download page when a newer version is available. Current-version
automatic checks stay silent; manual checks may show the installed version is
current.

Release workflow:

1. Build the new XVatsim ZIP.
2. Compute and update `package_sha256`, `plugin_sha256`, and
   `package_size_bytes`.
3. Copy or upload the new XVatsim ZIP to the approved public download location.
   For the 1.2.0 freeware release, the ZIP is served from GitHub Pages under
   `docs/releases/`.
4. Update `latest_version`, `published_date`, `message`, and `release_notes`.
5. Keep `download_page_url` pointed at the approved HTTPS public release or
   package URL.
6. Commit and push this file before announcing the release.

GitHub Pages setup:

1. Open the repository settings on GitHub.
2. Go to **Pages**.
3. Set **Source** to `Deploy from a branch`.
4. Set **Branch** to `main` and folder to `/docs`.
5. Save, then wait for GitHub to publish the site.

Note: users' plugins need anonymous HTTPS access to this JSON. If the repository
stays private, GitHub Pages must still publish a public site, or the manifest
should be moved to a separate public repository such as `xvatsim-updates`.
