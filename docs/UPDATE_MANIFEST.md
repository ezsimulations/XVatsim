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

V1.2.3 was published on 2026-08-15.

- Download filename: `XVatsim_1.2.3_Freeware_Windows_XP12.zip`
- Package size: `1663402` bytes
- Package SHA-256:
  `80B013ADB454D6F55AD359825E7E3229BD85C12A146289B4D17A15894049497C`
- Packaged plugin SHA-256:
  `28896800BAD64A5C25933F828D0D10FD63E0ED8C1AF5471760A4F1E599CFE23C`
- X-Plane.org:
  `https://forums.x-plane.org/files/file/100224-xvatsim_100_freeware_windows_xp12zip/`
- GitHub Release:
  `https://github.com/ezsimulations/XVatsim/releases/tag/v1.2.3`

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
4. Commit and push the manifest with the release closeout.
5. Create the matching GitHub tag and Release and upload the exact verified
   archive.
6. Update the X-Plane.org file page with the same archive.

Current-version automatic checks remain silent. Manual checks may show that the
installed version is current. Anonymous HTTPS access to the manifest is
required for installed plugins.
