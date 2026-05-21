# Preflight Route Cache Contract

Date created: 2026-05-17

This contract governs the future XVatsim preflight route cache builder.

The preflight builder is an optional desktop companion app that prepares route
and polygon workload before X-Plane launches. It must feel like a polished
XVatsim product component, not a developer script.

This contract is intentionally separate from controller authority. The preflight
cache is a performance seed only. It must never become a truth source for live
controllers, frequencies, VATSIM flight-plan validity, or authority ownership.

## Goal

Give pilots a simple, trustworthy way to preload a SimBrief/X-Plane `.fms`
flight plan before launching X-Plane so XVatsim can reduce heavy route and
polygon preparation during simulator startup or early flight-loop operation.

The pilot workflow should be simple:

1. Open the XVatsim Preflight Builder desktop app.
2. Select a flight plan from the default X-Plane FMS plans folder.
3. Press one clear build/prepare button.
4. Receive a professional confirmation that the route cache is ready.
5. Launch X-Plane and fly normally.

## Pilot Instructions

The desktop app must explain how the pilot gets the `.fms` file:

- From the SimBrief website, download/export the X-Plane flight plan format.
- Or, when using SimBrief Downloader, enable the `X-Plane 11/12` format.
- The expected SimBrief Downloader target folder is:
  `C:\X-Plane 12\Output\FMS plans`
- The app should explain that the selected `.fms` file lets XVatsim prepare
  route geometry before X-Plane starts, reducing workload later.
- The app must not imply the cache files the VATSIM flight plan for the pilot
  or replaces filing a flight plan on VATSIM.

## Product Standard

This must be a small, presentable Windows desktop `.exe` app, not a PowerShell
script and not a console-only utility.

The app should communicate product quality:

- Clear XVatsim branding in the header.
- Title identifies it as `XVatsim Preflight Builder`.
- Friendly instructions explain exactly what the pilot should do.
- Default file list opens to `C:\X-Plane 12\Output\FMS plans` when available.
- The pilot can select one `.fms` file from a visible list.
- A single primary action prepares the cache.
- The success state is clear, calm, and professional, such as:
  `Preflight route cache ready. You can now launch X-Plane and enjoy your flight.`
- Error messages should be plain-language and actionable.
- No raw debug dumps or developer jargon in the normal UI.

## Inputs

Primary input:

- X-Plane 11/12 `.fms` files exported by SimBrief Downloader.

Default folder:

- `C:\X-Plane 12\Output\FMS plans`

The app may support a custom folder later, but the first version should make the
default SimBrief/X-Plane flow effortless.

## Selected FMS Handling

When the pilot selects an `.fms` file, the app should treat that file as a
read-only source document:

- Do not move, rename, modify, or delete the selected `.fms` file.
- Read the file content.
- Compute a content hash.
- Record the file path and modified timestamp.
- Parse the X-Plane 11/12 FMS structure.
- Extract `CYCLE` when present.
- Extract `ADEP` and `ADES`.
- Preserve runway/procedure metadata such as `DEPRWY`, `SID`, `DESRWY`,
  `STAR`, and approach fields when present.
- Extract waypoint rows including type, ident, airway/direct token, altitude,
  latitude, and longitude.
- Build a canonical waypoint sequence from the parsed rows.
- Build route geometry from waypoint latitude/longitude rows.
- Produce a route identity hash from departure, destination, cycle when
  present, waypoint idents, and waypoint coordinates.

The selected `.fms` file is not the cache. It is the input used to build a
separate XVatsim-owned preflight cache.

If the `.fms` file is missing required route geometry, the app should reject it
with a plain-language message and not write a partial cache that could mislead
the plugin.

## Outputs

The app writes a cache file into the XVatsim plugin-owned data/cache location.

The cache file should be designed for XVatsim to load safely after X-Plane
starts. Missing, stale, unreadable, or incompatible cache files must not prevent
XVatsim from functioning normally.

## Cache Contents

The cache may include:

- Selected `.fms` file path.
- Selected `.fms` modified timestamp.
- Selected `.fms` content hash.
- Parsed FMS cycle, when present.
- Departure ICAO.
- Destination ICAO.
- Runway/procedure metadata, when present.
- Route waypoint sequence.
- Route waypoint coordinate sequence when available.
- Route identity hash.
- Authority source registry hash/commit.
- Boundary data hash/version.
- AIRAC/source-data metadata when available.
- Route-intersecting authority polygon IDs.
- Route entry distances for route-relevant polygons.
- Cache builder version.
- XVatsim compatibility version.

The cache must not include or imply live controller truth.

## Invalidation Rules

XVatsim must reject or ignore the cache when any required identity input no
longer matches:

- `.fms` file path changed.
- `.fms` modified timestamp or content hash changed.
- Departure or destination changed.
- Route waypoint sequence changed.
- Authority source registry hash changed.
- Boundary data hash changed.
- AIRAC/source-data identity changed, when available.
- Cache schema version is unsupported.
- Plugin compatibility version is unsupported.

Rejected cache should fall back to normal runtime behavior with a clear
diagnostic log line.

## Runtime Behavior

XVatsim may use a fresh cache to reduce startup or early-flight route/polygon
work.

XVatsim must still:

- Read the live VATSIM feed.
- Match the live VATSIM filed flight plan.
- Compare the fresh VATSIM flight plan against the cache route identity as a
  validation step where practical.
- Build/verify authority evidence.
- Use live controller frequencies.
- Use live transceiver/AFV geometry when available.
- Apply `AuthorityRelevanceSnapshot` before showing controllers.
- Fall back normally if the cache is absent or stale.

The cache may accelerate route preparation. It must not decide which
controllers appear in the UI.

If the VATSIM flight plan and selected `.fms` cache clearly disagree on
departure, destination, or route identity, XVatsim must reject the cache and use
normal runtime preparation.

## Safety Rules

- The cache cannot light a controller polygon.
- The cache cannot display an online controller.
- The cache cannot override a live VATSIM flight plan.
- The cache cannot bypass `AUTHORITY_EVIDENCE_CONTRACT.md`.
- The cache cannot bypass `RECONNECT_WORKFLOW_RECOVERY_CONTRACT.md`.
- The cache cannot hide route parse failures.
- The cache cannot be required for normal XVatsim operation.
- The plugin must remain stable and usable when the desktop app is never used.

## Desktop App UX Requirements

The first polished version should include:

- XVatsim branded header.
- Subtitle: `Preflight Builder`.
- Short instructions:
  `Select the SimBrief/X-Plane flight plan you want XVatsim to prepare before launch.`
- A file list showing `.fms` files from the default folder.
- Each file row should show enough identifying data when available:
  filename, route/departure/destination, and modified time.
- Refresh button for reloading the folder.
- Primary button, for example: `Prepare Selected Flight`.
- Disabled primary button until a valid `.fms` file is selected.
- Success confirmation with the selected route.
- Error panel for invalid `.fms`, missing folder, failed write, or stale source
  data.
- Professional empty state if no `.fms` files are found:
  `No X-Plane FMS plans found. Export an X-Plane 11/12 plan from SimBrief Downloader, then refresh.`

## Required Implementation Shape

The implementation should be split cleanly:

- Shared route/FMS parsing library or reusable parser path.
- Cache model and schema version.
- Desktop app UI shell.
- Cache writer.
- Plugin cache reader.
- Plugin cache validator.
- Plugin runtime integration.

Do not bury the cache parser inside the desktop UI. The parser and validator
must be testable by the regression harness.

## Required Diagnostics

The desktop app should show user-friendly messages.

The plugin should log technical diagnostics:

- cache file found/missing.
- cache accepted/rejected.
- rejection reason.
- cache route identity.
- source registry/boundary/AIRAC identity used.
- whether runtime fell back to normal route preparation.

## Required Harness Coverage

Before this contract is complete, regression coverage must include:

- Valid `.fms` parses into the expected route identity.
- Invalid `.fms` is rejected cleanly.
- Missing default FMS folder is handled cleanly.
- Fresh cache is accepted.
- Stale file hash cache is rejected.
- Stale route identity cache is rejected.
- Stale source registry/boundary identity cache is rejected.
- Unsupported cache schema is rejected.
- Missing cache falls back to normal behavior.
- Cache does not create authority relevance by itself.
- Cache does not display controllers without live authority evidence.

## Definition Of Done

This contract is complete only when:

- The contract remains documented here.
- A presentable Windows desktop `.exe` exists.
- The desktop app can list and select `.fms` files from the default X-Plane
  folder.
- The desktop app can write a versioned cache file.
- XVatsim can read and validate the cache.
- XVatsim rejects stale or incompatible cache safely.
- XVatsim falls back normally without the cache.
- No controller display decision depends only on the cache.
- Required diagnostics exist.
- Required harness coverage exists and passes.
- Existing authority evidence and reconnect recovery harness scenarios still
  pass.
- Plugin builds successfully.
- The final review confirms the cache is performance-only and not a truth
  source.
