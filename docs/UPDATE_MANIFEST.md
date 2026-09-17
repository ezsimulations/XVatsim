# XVatsim Update Manifest

This folder is served by GitHub Pages from the `docs` directory.

Expected public manifest URL:

```text
https://ezsimulations.github.io/XVatsim/xvatsim_update.json
```

The plugin fetches `xvatsim_update.json`, compares `latest_version` with the
installed plugin version, and displays a notify-only update notice when a newer
version is available. The notice directs users to X-Plane.org or GitHub
Releases; XVatsim does not download or install updates.

## Current Publication

V2.0.2 was published on 2026-09-17.

- Download filename: `XVatsim_2.0.2_Freeware_Windows_XP12.zip`
- Package size: `2042321` bytes
- Package SHA-256:
  `8BD0BE5137D2844AC64CA1FE444E05D12C43A3C6BEBCFC4CF6370B5B1A8596E9`
- Packaged plugin SHA-256:
  `31F0D5EC3766C662A474A4E113464F956AC312FB61F002130FFAE258B71FA726`
- X-Plane.org:
  `https://forums.x-plane.org/files/file/100224-xvatsim_100_freeware_windows_xp12zip/`
- GitHub Release:
  `https://github.com/ezsimulations/XVatsim/releases/tag/v2.0.2`

`download_page_url` remains the primary X-Plane.org page for compatibility.
`github_release_url` is an informational field for clients and documentation
that support a second download destination. The current parser safely ignores
unknown manifest fields, so the schema remains `1`.

## Release Workflow

1. Build and validate the new XVatsim ZIP.
2. Compute `package_sha256`, `plugin_sha256`, and `package_size_bytes` from the
   final package.
3. Update the version, date, filename, message, release notes, and download
   URLs in `xvatsim_update.json`.
4. Update the X-Plane.org file page with that exact verified archive.
5. After the X-Plane.org upload is confirmed, commit the release closeout
   locally.
6. Create and publish the matching GitHub tag and Release, then upload the same
   verified archive.
7. Push the manifest to the GitHub Pages source branch last, so installed
   plugins are not notified before both download destinations are ready.
8. Verify anonymous access to the raw manifest, GitHub Pages manifest, GitHub
   Release asset, and X-Plane.org page. Confirm an older installed version sees
   an available-update result and V2.0.2 sees a current-version result.

Current-version automatic checks remain silent. Manual checks may show that the
installed version is current. Anonymous HTTPS access to the manifest is
required for installed plugins.
