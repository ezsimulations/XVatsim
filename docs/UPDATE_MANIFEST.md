# XVatsim Update Manifest

This folder is served by GitHub Pages from the `docs` directory.

Expected public manifest URL:

```text
https://ezsimulations.github.io/XVatsim/xvatsim_update.json
```

The plugin fetches `xvatsim_update.json`, compares `latest_version` with the
installed plugin version, and displays a notify-only update notice when a newer
version is available. The notice directs users to X-Plane.org or GitHub
Releases; the plugin does not download or install updates.

The standalone Manager also reads this file. Beginning with the first release
after V2.1.0, a release may add this optional object without changing the
plugin's notify-only behavior:

```json
"manager_payload": {
  "url": "https://github.com/ezsimulations/XVatsim/releases/download/vX.Y.Z/XVatsim_X.Y.Z_Manager_Payload.zip",
  "size_bytes": 1234567,
  "sha256": "64_HEXADECIMAL_CHARACTERS",
  "minimum_manager_version": "X.Y.Z"
}
```

The URL must be a direct anonymous HTTPS download. The byte length and SHA-256
must describe the exact Manager payload asset produced by
`scripts/build_xvatsim_manager.ps1`. The Manager verifies these public facts,
the payload inventory, and every managed file before presenting an Update
action. A compatible Manager can therefore install later plugin payloads
without replacing its own executable.

## Current Publication

V2.2.0 was published on 2026-10-22.

- Download filename: `XVatsim_2.2.0_Freeware_Windows_XP12.zip`
- Package size: `64532182` bytes
- Package SHA-256:
  `74B09EA9851118889F6617CEE5C6D28E1C3F8AEC8379E68B34E56A396E6B055D`
- Packaged plugin SHA-256:
  `ADFF5F898F1930651C90B5108CF145D84162770C0BEF79965341ABD44B1EC23E`
- Manager payload filename: `XVatsim_2.2.0_Manager_Payload.zip`
- Manager payload size: `1543201` bytes
- Manager payload SHA-256:
  `259D3A3A4F2AC84531FDCD7D851D5FA6606C9F6B9ED1D60B5DB62BF2375C0D1E`
- X-Plane.org:
  `https://forums.x-plane.org/files/file/100224-xvatsim_100_freeware_windows_xp12zip/`
- GitHub Release:
  `https://github.com/ezsimulations/XVatsim/releases/tag/v2.2.0`

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
   verified archive and the separately verified Manager payload asset.
7. Verify anonymous HTTPS access to the payload and repeat its exact length and
   SHA-256 before placing those values in `manager_payload`.
8. Push the manifest to the GitHub Pages source branch last, so installed
   plugins are not notified before both download destinations are ready.
9. Verify anonymous access to the raw manifest, GitHub Pages manifest, GitHub
   Release asset, and X-Plane.org page. Confirm an older installed version sees
   an available-update result and the new version sees a current-version
   result. Confirm the release-candidate Manager selects the online payload.

Current-version automatic checks remain silent. Manual checks may show that the
installed version is current. Anonymous HTTPS access to the manifest is
required for installed plugins.
