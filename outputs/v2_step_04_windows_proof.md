# XVatsim V2 Step 4 — Windows Release Proof

Date: 2026-08-27
Result: PASS

## Starting source state

- Branch: `v2-development`
- Starting HEAD: `cc7de155aa9ce27aaa54014dea542843b278ef50`
- Subject: `docs: prepare V2 Step 4 session handoff`
- Starting staged changes: 0
- Authorized Step 4 source and evidence changes were uncommitted during proof.

## Toolchain

- Generator: Visual Studio 18 2026
- MSVC: 19.51.36252.0 / toolset 14.51.36231
- Windows SDK: 10.0.26100.0
- Architecture: x64
- Configuration: Release
- Existing repository compatibility definition:
  `_SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS`

## Fresh proof build

Directory: `build/v2-step4-release-proof`

- Plugin: OFF
- Regression harness: ON
- Step 4 visual proof: ON
- Step 3 live fixtures: OFF
- Configure: PASS
- `XVatsimRegressionHarness` Release build: PASS
- `XVatsimStep4MetarVisualProof` Release build: PASS
- CMake cache SHA-256:
  `F4601C5E8845BCBFA4C11BE8275FB4A907DF8D043C68666C8EC340C85ECD0A14`
- Harness SHA-256:
  `094A6758177EE8B703A1DB60C28F4D387ACA3DA506F17EA6FCD10BCA0DD368EB`
- Visual-proof executable SHA-256:
  `7CA34CA2CA3DDEDA401657FDD8FFF83ACB8C98502BFF6CB4AEFB83B6A80BA0C7`

Regression results:

- Focused Step 4: 53/53 PASS
- Complete saved suite: 547/547 PASS
- Scenario-set SHA-256:
  `780ABD3A26E0E6C96ADBB3261BB6C3960B21D658F4613C21754BBDD516DF8902`

## Fresh normal build

Directory: `build/v2-step4-release-normal`

- Plugin: ON
- Regression harness: OFF
- Step 3 visual proof: OFF
- Step 4 visual proof: OFF
- Step 3 live fixtures: OFF
- Configure: PASS
- `XVatsimPlugin` Release build: PASS
- CMake cache SHA-256:
  `91669FAA145F46A3FA60BDC746CDCB421B2160885F0E3262986ADC823CBC8AB4`
- Normal fixture-off `XVatsim.xpl` size: 2,390,016 bytes
- Normal fixture-off `XVatsim.xpl` SHA-256:
  `0ADA9C12D8DCD379A5FC6B997C655C0D79F11AC3A58559D46D0D702DEFDD28F0`

## Normal-binary isolation

Byte-level ASCII and UTF-16 inspection of the fixture-off normal `.xpl` found:

- `metar.vatsim.net`: present
- `VATSIM_METAR`: present
- `NOAA`: absent
- `aviationweather.gov`: absent
- `fixture-success`: absent
- `cancelled-fixture-phase`: absent
- synthetic Step 4 report marker `KABQ 271953Z`: absent
- proof-only `InjectedTransport`: absent

Source-boundary inspection found no `VatsimMetarClient`, endpoint hostname,
parser entry point, ICAO validator, or METAR cadence constant in
`plugin/src/XVatsimPlugin.cpp`. The plugin contains only the generic worker host,
brain cycle/lifecycle binding, bounded text fact transport, and rendering path.

## Visual and performance proof

- Visual cases: 14
- Repeat PNG mismatches: 0
- Visual manifest SHA-256:
  `E75427A54FE739C7383D645FAE7B1C86352173969E5F945C456A69BD7F8478D7`
- Warm unchanged cycles: 100,000
- Recurring content/accessory work counters: all zero
- Maximum proof cancellation latency: 15 ms (limit 500 ms)
- Real stalled local-loopback WinHTTP cancellation: 0 ms, handle/callback closed
- Network wait and simulator-thread acceptance timing: reported separately

## External-state boundary

No plugin was copied from either build. X-Plane and xPilot were not started.
No live VATSIM request was made. No live proof or external rollback was
performed. V1.2.3 was not modified.
