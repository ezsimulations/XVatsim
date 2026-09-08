# Step 4 ORB Publication And Timing Correction — Offline Proof

## Result

- Corrective scenarios: 40/40.
- Step 3 focused scenarios: 31/31.
- Step 4 focused scenarios: 136/136.
- Complete saved suite: 630/630.
- Canonical scenario fingerprint:
  `24F3438A454DD66EF46DA334F18761F501E241884EC0E496EF2D737CBF21790C`.

## Automatic ORB publication

The pre-correction case failed because accepted KDFW/VFR changed the projected
content but requested zero rail raster/upload while the closed rail retained
neutral pixels. The corrected rendered-field signature publishes at the next
normal presentation/draw opportunity within the locked 500,000-microsecond
bound.

Every tested visible rail transition requested one rail raster and one upload:
neutral to VFR/MVFR/IFR/LIFR, VFR to MVFR, KDFW to KSAN, and successful to
neutral. An open selected drawer additionally requested one drawer raster and
upload. Lookup spotlight and identical-primary content changes requested zero
rail work. Closed-drawer acceptance requested zero drawer work.

## Timing attribution

The red timing case reproduced a 100,000-microsecond deferred interval reported
as zero preparation wait and therefore as synchronous failure. Corrected exact
accounting decomposed the same interval into:

- worker queue wait: 10,000 microseconds;
- worker preparation CPU: 10,000 microseconds;
- worker-to-publication handoff: 1,000 microseconds;
- publication-to-ready collection: 68,000 microseconds;
- ready-to-bind wait: 11,000 microseconds;
- synchronous dispatch: 10 microseconds;
- frame/draw pickup wait: 14,990 microseconds;
- unattributed/overlap: 0/0 microseconds.

The 116,000-microsecond action was classified `PreparationLimited`, not a
synchronous failure. A separate true 20,000-microsecond synchronous interval
remained `SynchronousWallFailure`. A synthetic action above 500,000
microseconds remained `LivenessFailure`.

## Stress, idle, transport, and parser proof

- Accessory stress: 1,000 compatible supersessions; final queue 0; in-flight
  false; maximum deterministic in-flight duration 1 microsecond.
- Warm unchanged: 100,000 cycles with zero parse, fingerprint, history, wrap,
  rail/drawer raster, upload, publication, input-dispatch, or terminal-diagnostic
  delta.
- Worker shutdown: five-phase maximum 15 milliseconds; limit 500 milliseconds.
- Stalled-loopback cancellation: maximum 0 milliseconds.
- Successful real-WinHTTP loopback: `/KDFW?format=json`, complete response,
  JSON harvest, and one brain parse in 22 milliseconds.
- Changed METAR parser/classifier elapsed time: maximum 23 microseconds in the
  final corrective runs, on the simulator flight-loop harvest path.
- Identical METAR content: no parse and zero parser elapsed time.

## Visual and binary proof

- Production-raster PNGs: 33.
- Repeat PNG differences: 0.
- Main-card signature: `8949928878432326300`, unchanged.
- Visual manifest SHA-256:
  `61D7ADAD181FB4D26722D5362546207E613EA604DF0C62BAB777CFA0952D3FF8`.
- Normal plugin size: 2,280,448 bytes.
- Normal plugin SHA-256:
  `D2C028255FF03F67B633E272C270F60F94949CF7B863955220A5769ED5B844BD`.
- `metar.vatsim.net` and `VATSIM_METAR`: present.
- NOAA, Aviation Weather, loopback, synthetic METAR, injected transport,
  proof-endpoint, corrective-scenario, and proof-fixture markers: absent.

No deployment, application startup, live VATSIM request, V1.2.3 modification,
or controlled-live reproof occurred.
