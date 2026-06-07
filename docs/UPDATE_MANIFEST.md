# XVatsim Update Manifest

This folder is intended to be served by GitHub Pages from the `docs` directory.

Expected public manifest URL:

```text
https://ezsimulations.github.io/XVatsim/xvatsim_update.json
```

The plugin should fetch `xvatsim_update.json`, compare `latest_version` to the
installed plugin version, and show `message` with `download_page_url` when a
newer version is available.

Release workflow:

1. Build and upload the new XVatsim ZIP to X-Plane.org.
2. Update `latest_version`, `published_date`, `message`, and `release_notes`.
3. Keep `download_page_url` pointed at the X-Plane.org file page unless the
   public download page changes.
4. Commit and push this file before announcing the release.

GitHub Pages setup:

1. Open the repository settings on GitHub.
2. Go to **Pages**.
3. Set **Source** to `Deploy from a branch`.
4. Set **Branch** to `main` and folder to `/docs`.
5. Save, then wait for GitHub to publish the site.

Note: users' plugins need anonymous HTTPS access to this JSON. If the repository
stays private, GitHub Pages must still publish a public site, or the manifest
should be moved to a separate public repository such as `xvatsim-updates`.
