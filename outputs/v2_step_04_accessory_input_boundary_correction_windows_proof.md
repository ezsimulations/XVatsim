# Step 4 Accessory Input Boundary Correction — Windows Release Proof

Date: 2026-08-28

## Builds

Fresh Windows x64 Release configurations used Visual Studio 18 2026, MSVC
19.51.36252.0, and Windows SDK 10.0.26100.0. The configure-only compatibility
define `_SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS` was required by
the current MSVC library for the repository's existing experimental-coroutine
dependency; no product source was changed for that toolchain condition.

- Fixture-on proof cache SHA-256:
  `77F22425FAA568202A59457F9EE78E0CA39EAFEF7AF350A5BCA9856B3CD0C376`.
- Fixture-off normal cache SHA-256:
  `C1FB9F9158CFA957D9043CE9A9F027B72CE3C19B450FEF90B92083390D54CFF4`.

The proof build enabled the regression harness and Step 4 visual proof with the
plugin disabled. The normal build enabled only the plugin; regression,
preflight-builder, visual-proof, and Step 3 live-proof fixtures were disabled.

## Regression and timing

- Corrective: 26/26.
- Step 3: 31/31.
- Step 4: 206/206.
- Complete: 700/700.
- Canonical fingerprint:
  `C1367F24170283074D7D7371EFD4EDD1C687D7903F959CC3FA19FE3D807AAE8B`.
- 1,000-click maximum terminal time: 100 microseconds.
- Synchronous limit: 16,700 microseconds; no failure.
- End-to-end limit: 500,000 microseconds; no failure.
- Worker shutdown maximum: 17 ms.
- Successful real-WinHTTP loopback: 17 ms.
- 100,000 warm cycles: zero recurring work.

## Visual proof

The production raster path generated 43 PNGs. All 43 PNG hashes matched a
second complete render; PNG differences were zero. Timing-bearing
`performance.csv` and its dependent aggregate manifest varied as expected and
are not pixel evidence. Main-card signature remained
`8949928878432326300`.

The set includes populated METAR, truthful empty ATIS/PDC, all three switch
directions, active close, double-ATIS final close, rapid alternating final
state, and re-enable state. Visual manifest SHA-256:
`7D4CB9371EF4F3E7D6125D7546DE5D5CE46A38619F74AFA0EB8697F4181F33CF`.

## Normal plugin

- Path:
  `build/v2-step4-accessory-input-boundary-release-normal/dist/XVatsim/win_x64/XVatsim.xpl`
- Size: 2,293,248 bytes.
- SHA-256:
  `694D1B70E40ACEB8F033D6985D3517CB20A76E3981D2165FDCABED4E90950CD3`.

Required `metar.vatsim.net` and `VATSIM_METAR` markers are present. NOAA,
Aviation Weather, loopback, synthetic METAR, injected transport, proof
endpoint, corrective scenario, and proof-fixture markers are absent.

The accompanying registry and audio hashes remain respectively:

- `3676CA43E5AFB8A5E443FDE6D04616918029E004E17EFAA022695D91E23DB60B`
- `C7BBE97DAD356C68FDFE40F9E8C1CF4EADEBCCD125463214F963F66356F8D9F1`

No binary was deployed.
