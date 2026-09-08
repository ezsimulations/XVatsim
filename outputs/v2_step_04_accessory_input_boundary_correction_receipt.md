# V2 Step 4 Accessory Input Boundary Correction Receipt

Date: 2026-08-28

## Status

`STEP 4 ACCESSORY INPUT BOUNDARY CORRECTED AND OFFLINE-PROVEN — CONTROLLED LIVE REPROOF NOT AUTHORIZED`

## Commit lineage

- Starting HEAD: `f7ea81790a118998ddb9863d965726080d6a7937`.
- Implementation and evidence:
  `55dd147476f0da786eae4a27dde26912a6227d26`.
- Receipt-only commit: this document's commit; resolve from Git because a
  commit cannot contain its own identity.

## Exact correction

The production X-Plane mouse callback no longer drains input or invokes the
brain. It mechanically hit-tests, captures the exact drawer in an immutable
bounded FIFO fact, publishes a notification sequence, requests next-cycle
service from the already registered flight loop, and returns. The next safe
brain/plugin cycle consumes facts FIFO with a bounded per-cycle budget, obtains
exactly one brain selection decision per valid fact, issues the immutable
presentation command, and consumes its terminal publication fact.

The temporary next-cycle cadence is active only while bounded accessory work
is pending and returns to 0.25 seconds after terminal accounting. No input
worker, input thread, debounce engine, new scheduler, or polling loop was
introduced. The legacy render-bound dispatcher is not a production gate or
authoritative accounting source.

## Proof receipt

- Preserved red reproductions: 5/5 failed before production correction and
  pass afterward.
- Corrective scenarios: 26/26.
- Step 3 focused: 31/31.
- Step 4 focused: 206/206.
- Complete saved suite: 700/700.
- Canonical scenario fingerprint:
  `C1367F24170283074D7D7371EFD4EDD1C687D7903F959CC3FA19FE3D807AAE8B`.
- 1,000-click stress: 1,000 produced/consumed/decided/commanded/terminal;
  zero drops; queue depths zero; no behavioral in-flight state; maximum
  terminal time 100 microseconds.
- 100,000 unchanged cycles: zero recurring work; settled cadence 0.25 seconds.
- Worker cancellation/join maximum: 17 ms.
- Real WinHTTP loopback lifecycle: 17 ms, successful KDFW response, parser
  invoked once.
- Visual proof: 43 production-raster PNGs; repeat PNG differences zero;
  accepted main-card signature unchanged.
- Visual manifest SHA-256:
  `7D4CB9371EF4F3E7D6125D7546DE5D5CE46A38619F74AFA0EB8697F4181F33CF`.

## Normal fixture-off binary

- Size: 2,293,248 bytes.
- SHA-256:
  `694D1B70E40ACEB8F033D6985D3517CB20A76E3981D2165FDCABED4E90950CD3`.
- Required markers `metar.vatsim.net` and `VATSIM_METAR`: present.
- NOAA, Aviation Weather, loopback, synthetic METAR, injected transport,
  proof endpoint, corrective scenario, and proof-fixture markers: absent.

The current MSVC library required the documented configure-time
`_SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS` compatibility define for
the repository's pre-existing coroutine dependency. This did not change
product source or behavior.

## Protected scope

- Original protected inventory: 116 rows, zero mismatches.
- First failed-live manifest:
  `7CA785AC6367AD9C32D6F89C8CAB8A6205BE3E622C343AB847A44ADB6D939ED0`,
  zero mismatches.
- Second corrected-live manifest:
  `E3355DED8FFFD76BB904D12ABF7BA8013BD1901E2349181D9C8C34861D1D58BA`,
  zero mismatches.
- Accessory-liveness failed-live manifest:
  `FE96E66CB2225268B226812F9FDDA88F2CF0BC9B6922CA8422A7BCCBA1D98EAD`,
  zero mismatches.
- ORB-publication failed-live manifest:
  `DA1A0BC736E73044F9EE925B6ACE7296291B07D72ABFB794E3E6C89E93D868D9`,
  zero mismatches.
- Brain-exclusive failed-live manifest:
  `DB83F946C4A5586DAF73A3AC4951DB308F0B5CCB82B1ECDE1E6673EBBD3677ED`,
  zero mismatches.
- Latest protected backup manifest:
  `382661FF037602AF076B8E448BF1CBA9327BAC124B2CDC7E362940FF30A3CE3F`,
  zero mismatches.
- Protected `V2 Test`: 50 files/29 directories, zero mismatches.
- X-Plane/xPilot processes: 0/0.
- Active/staged `.xpl`: 0/0.
- V1.2.3: untouched.

No deployment, application startup, live VATSIM request, controlled-live
proof, protected-evidence mutation, or unauthorized production change occurred.
Contract deviations: none.
