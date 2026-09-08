# Step 4 ORB Publication And Timing Correction — Windows Proof

## Toolchain and configurations

- Visual Studio generator: `Visual Studio 18 2026`, x64.
- Compiler: MSVC 19.51.36252.0.
- Windows SDK: 10.0.26100.0.
- Configuration: Release.
- Established experimental-coroutine deprecation warning silence enabled.

Fresh proof build:

`build/v2-step4-orb-publication-timing-release-proof`

- plugin off;
- regression harness on;
- Step 4 production-raster visual proof on;
- live proof fixtures off.

Fresh normal build:

`build/v2-step4-orb-publication-timing-release-normal`

- plugin on;
- regression harness and visual proof off;
- live proof fixtures off.

Both configurations and Release builds completed successfully. Existing C4530
exception-semantics warnings remained non-fatal; no new build error occurred.

## Regression and deterministic proof

- Corrective: 40/40 in 8,136 milliseconds.
- Step 3: 31/31 in 1,980 milliseconds.
- Step 4: 136/136 in 9,533 milliseconds.
- Complete saved suite: 630/630 in 19,972 milliseconds.
- Scenario fingerprint:
  `24F3438A454DD66EF46DA334F18761F501E241884EC0E496EF2D737CBF21790C`.
- Visuals: 33 PNGs; repeat differences 0.
- Warm unchanged: 100,000 cycles; every recurring-work delta 0.
- Accessory stress: 1,000/1,000 compatible supersessions completed; queue 0;
  in-flight false; maximum 1 microsecond.
- Cancellation/join maximum: 15 milliseconds.
- Successful WinHTTP loopback: 22 milliseconds.
- Maximum observed changed-METAR parse/classify time: 23 microseconds.

## Normal payload and isolation

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `XVatsim.xpl` | 2,280,448 | `D2C028255FF03F67B633E272C270F60F94949CF7B863955220A5769ED5B844BD` |
| `authority_source_registry.json` | 40,340 | `3676CA43E5AFB8A5E443FDE6D04616918029E004E17EFAA022695D91E23DB60B` |
| `ui_transition.mp3` | 27,116 | `C7BBE97DAD356C68FDFE40F9E8C1CF4EADEBCCD125463214F963F66356F8D9F1` |

Normal-build cache SHA-256:
`B6F5A08F5A36EA2C695F7956BEAB3A5B4F11D6ED342056DEADB590664E1B37EE`.

The binary contains the required `metar.vatsim.net` and `VATSIM_METAR`
markers. It contains none of the tested NOAA, Aviation Weather, loopback,
synthetic METAR, injected transport, proof endpoint, scenario probe, or proof
fixture markers.
