# XVatsim V2 Step 4 METAR Correction — Windows Release Proof

Date: 2026-08-28
Result: PASS

## Toolchain and configurations

- Generator: Visual Studio 18 2026
- MSVC: 19.51.36252.0 / toolset 14.51.36231
- Windows SDK: 10.0.26100.0
- Architecture/configuration: x64 Release
- Compatibility definition:
  `_SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS`

Fresh proof configuration:

- Directory: `build/v2-step4-correction-release-proof`
- Plugin OFF; regression harness ON; Step 4 visual proof ON; Step 3 live
  fixtures OFF.
- CMake cache SHA-256:
  `644A9B5BD7F531DE127FD66401E23F3B0B799DEF9BFBCCA37C210452F5085870`.
- Regression harness SHA-256:
  `F385A71F905E5DC66BF5B0D66387B5D10342D57AE72AC90E70DA94963A2D286F`.
- Visual proof executable SHA-256:
  `24DDD419C7F9A07BB11152F86F76CD907DAD91B443791F4EC937A8366BA34444`.

Fresh normal configuration:

- Directory: `build/v2-step4-correction-release-normal`
- Plugin ON; regression harness OFF; Step 3/Step 4 visual proof OFF; Step 3
  live fixtures OFF.
- CMake cache SHA-256:
  `761DF4EA1DE8ED5FEEA6A54BA49F15BDE72B2DC8087A6E9FE9A757213F40E136`.
- Normal fixture-off plugin size: 2,403,840 bytes.
- Normal fixture-off plugin SHA-256:
  `2511F7BA9992893378423DE1F113CBAA04971DB7DD0F17505DA06FBCA0943288`.

## Regression and transport proof

- Corrective scenarios: 14/14 PASS.
- Complete Step 4 focused set: 67/67 PASS.
- Complete saved suite: 561/561 PASS in 11.433 seconds.
- Scenario-set SHA-256:
  `6AB913C1C0658A0CDE93A84D369267DDFDCD7FDE27797BF097FDB70DD574EF94`.
- Successful loopback lifecycle: 17 ms, exact `/KDFW?format=json`, send
  completion, headers, HTTP 200, payload completion, JSON/station acceptance,
  worker harvest, one brain parse, VFR category, and complete shutdown.
- Real loopback non-200 and malformed JSON: correct terminal failure ledgers.
- Diagnostic matrix: all required transport, validation, parser, and success
  stages distinguished with one-shot terminal/disposition facts.
- Network and simulator-thread work remained separate; the focused timing proof
  recorded 2,500,000 microseconds of fixture network time and 7 microseconds of
  simulator-thread acceptance work.

## Cancellation and quiet runtime

- Five injected cancellation phases maximum: 15 ms.
- Real WinHTTP stalled-response cancellation: 1 ms.
- Worker, request handle, callback, connection, session, and result harvest:
  closed/drained.
- Lifecycle-drained terminal fact: diagnostic cleanup only and rejected before
  parser or presentation policy.
- 100,000 unchanged cycles: zero parse, fingerprint, history, wrap, raster,
  upload, publication, and terminal-diagnostic deltas.

## Visual proof

- Production-raster PNGs: 15.
- Repeat-render PNG differences: 0.
- Exact ORB string assertions: PASS.
- Main-card signature: `8949928878432326300`, unchanged in all cases.
- Visual manifest SHA-256:
  `3B4AB114943D91EC8D3610F8F7406B9FB124905F3BFFA75E11447C7118E9EE8F`.
- Performance CSV SHA-256:
  `651FC487F82A6103194F576DC0C8090D9CC25ACC3589D297F9B4AE5241131EF0`.

## Normal-binary isolation

ASCII and UTF-16 byte inspection passed:

- Required present: `metar.vatsim.net`, `VATSIM_METAR`.
- Required absent: NOAA, Aviation Weather, `aviationweather.gov`,
  `127.0.0.1`, proof endpoint/type markers, injected transport, fixture
  success/cancellation strings, and the synthetic corrective KDFW report.
- Normal payload registry SHA-256:
  `3676CA43E5AFB8A5E443FDE6D04616918029E004E17EFAA022695D91E23DB60B`.
- Normal payload audio SHA-256:
  `C7BBE97DAD356C68FDFE40F9E8C1CF4EADEBCCD125463214F963F66356F8D9F1`.

## External boundary

No deployment, staged `.xpl`, X-Plane/xPilot startup, live VATSIM request,
controlled live reproof, or V1.2.3 change occurred.
