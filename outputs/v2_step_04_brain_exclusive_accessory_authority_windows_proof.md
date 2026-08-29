# V2 Step 4 Brain-Exclusive Authority Windows Proof

Date: 2026-08-28

## Toolchain

- Generator: Visual Studio 18 2026.
- Platform: x64.
- Configuration: Release.
- Compiler: MSVC 19.51.36252.0.
- Windows SDK: 10.0.26100.0.
- Established flags:
  `/D_SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS /EHsc`.

## Fresh build directories

- Proof:
  `build/v2-step4-brain-exclusive-authority-release-proof`.
- Fixture-off normal:
  `build/v2-step4-brain-exclusive-authority-release-normal`.

The proof build enabled the regression harness and Step 4 production-raster
visual proof with plugin and live fixtures off. The normal build enabled only
the fixture-off plugin and disabled harness, visual proof, preflight builder,
and Step 3 live proof fixtures.

Both fresh Release builds passed.

## Regression

| Set | Passed | Failed |
| --- | ---: | ---: |
| Brain-exclusive correction | 44 | 0 |
| Step 3 focused | 31 | 0 |
| Complete Step 4 focused | 180 | 0 |
| Complete saved suite | 674 | 0 |

Canonical scenario fingerprint:
`EE5B15EAE725EBA23AEA1CE9F9FEB82D0D93578C0A18EBA6129AE8B898E645C3`.

## Focused timing

- 1,000 actions: 1,000 terminal, zero dropped/queued/pending, no behavioral
  in-flight state, deterministic maximum 100 microseconds, zero failures.
- 100,000 warm cycles: all recurring work counters zero.
- Real WinHTTP loopback success: 17 ms.
- Five-phase METAR worker shutdown maximum: 15 ms.
- Stalled loopback cancellation observation: 0 ms.
- Limits retained: 16.7 ms synchronous work, 500 ms terminal liveness.

## Visual

- Production-raster PNGs: 33.
- Repeat PNGs: 33.
- PNG differences: 0.
- Main-card signature: unchanged in every case.
- Transition counts are recorded in
  `v2_step_04_brain_exclusive_accessory_authority_visual_evidence/publication_counts.csv`.

## Normal payload

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `XVatsim.xpl` | 2,422,784 | `F6C5B2B35CBBDB83BCE1FBD6D9A8029F3E7A258BFC4B420F8F87A208FCFCB13C` |
| `authority_source_registry.json` | 40,340 | `3676CA43E5AFB8A5E443FDE6D04616918029E004E17EFAA022695D91E23DB60B` |
| `ui_transition.mp3` | 27,116 | `C7BBE97DAD356C68FDFE40F9E8C1CF4EADEBCCD125463214F963F66356F8D9F1` |

Binary isolation passed. Required VATSIM markers are present; alternate-source,
loopback, synthetic, injected, proof-endpoint, corrective-scenario, and proof
fixture markers are absent.

No payload was deployed.
