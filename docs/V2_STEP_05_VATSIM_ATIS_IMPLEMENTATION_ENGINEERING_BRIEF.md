# XVatsim V2 — Step 5 VATSIM ATIS Implementation Engineering Brief

## Status and authority boundary

This brief records the implementation authorized by the approved **XVatsim V2 — Step 5 VATSIM ATIS Implementation and Offline Proof Contract Gate** and its Director-approved continuations.

Final engineering classification:

`STEP 5 VATSIM ATIS IMPLEMENTATION — OFFLINE-PROVEN, NOT CONTROLLED-LIVE-PROVEN`

No X-Plane or xPilot application was started, no payload was deployed or staged, and no live VATSIM request was made. The result does not authorize controlled-live proof, production deployment, staging, commit, release, or Step 5 acceptance.

## Final production data flow

```text
existing /v3/vatsim-data.json fetch and cadence
    -> bounded mechanical root atis[] decoding
    -> immutable raw ATIS records in the shared feed snapshot
    -> plugin transports const feed/context facts
    -> Brain parses callsign grammar and decides all ATIS semantics
    -> existing Brain-owned accessory presentation snapshot
    -> existing generic AccessoryPreparationWorker
    -> existing overlay commit/raster/draw/publication coordinator
    -> existing AccessoryPublicationFactQueue
    -> Brain consumes terminal roles and acknowledges the exact visible ATIS revision
```

There is no second endpoint, scheduler, network request, ATIS worker, cache, selector, snapshot type, publication queue, renderer, or drawer loop.

## Rule One ownership matrix

| Responsibility | Sole owner | Other components |
|---|---|---|
| Existing feed dispatch, 15-second cadence, 60-second failure backoff | Existing VATSIM feed/plugin path | Unchanged |
| Root `atis[]` JSON shape, primitive type checks, sanitization, byte/count limits | VATSIM data-feed decoder | Mechanical facts only |
| Callsign-to-ICAO and Departure/Arrival/Combined grammar | Brain | Zero parsing symbols outside Brain |
| Automatic target and role priority | Brain | Plugin supplies accepted workflow facts only |
| Duplicate winner and deterministic tie-break | Brain | Feed order has no authority |
| Availability, source unknown, and confirmed unavailable | Brain | Decoder reports mechanical completeness/issues only |
| Semantic revision identity and no-op detection | Brain | Source generation/timestamps do not create revisions |
| Primary, lookup ownership, lookup timers, and restoration | Brain | No network acceleration or secondary timer |
| History, unread, and exact visible-frame acknowledgement | Brain | Overlay copies opaque correlation identity only |
| ORB text/tone and drawer semantic content | Brain snapshot projection | Overlay does not reinterpret content |
| Preparation, wrapping, measurement, raster, and upload | Existing generic preparation/overlay path | Mechanical only |
| Command/attempt terminal transport and ordering | Existing publication coordinator and queue | Unchanged |
| Product diagnostics | Plugin serializer from actual fact and Brain decision | No assumed success counts |

Prohibited-owner audit results:

- callsign/role parser outside Brain: `0`
- ATIS selector outside Brain: `0`
- product availability decision outside Brain: `0`
- ATIS history/unread owner outside Brain: `0`
- dedicated ATIS worker: `0`
- ATIS scheduler/request/endpoint: `0`
- ATIS cache/repository/state machine outside Brain: `0`
- second snapshot/queue/renderer/publication path: `0`
- controller-text product fallback: `0`

## Bounded raw DTO

The shared feed snapshot now carries mechanically bounded raw records and component facts:

- root-present and root-array facts;
- component-complete flag;
- mechanical issue mask;
- retained-field and conservative-memory counts;
- rejected-record count; and
- bounded immutable raw callsign, frequency, information code, timestamps, and text lines.

Mechanical limits remain:

- root records: `256`;
- callsign: `32` bytes;
- frequency: `16` bytes;
- information code: `8` bytes;
- timestamp: `48` bytes;
- text lines: `64`;
- each text line: `512` bytes;
- joined body: `8 KiB`;
- retained ATIS field bytes: `1 MiB`;
- conservative ATIS component memory: `2 MiB`.

A 513-byte line is mechanically incomplete and produces no product authority. A 300-record root is whole-component overflow: no order-dependent prefix is retained as authoritative. The DTO exposes mechanical completeness and issue bits, never an unqualified product-usability conclusion.

## Brain semantic model

Only the Brain recognizes:

- `<ICAO>_D_ATIS` as Departure;
- `<ICAO>_A_ATIS` as Arrival; and
- `<ICAO>_ATIS` as Combined.

The ICAO token is exactly four ASCII alphanumeric characters. Bounded malformed forms reach the Brain and receive deterministic rejection.

Automatic selection:

- Departure workflow: Departure, then Combined;
- Enroute/Arrival workflow: Arrival, then Combined;
- IFR and VFR use the same accepted endpoint rule;
- no accepted flight endpoints leaves automatic ATIS idle;
- dedicated root ATIS does not require a matching controller record; and
- controller `text_atis` cannot create product ATIS.

Duplicate selection uses role priority, newest valid source time, then a stable lexical/content key. Reversed container order and equal times therefore produce the same decision.

Manual lookup validates one four-character ICAO in the Brain. It uses the current shared snapshot, never changes the automatic primary, prefers Combined, otherwise shows Departure then Arrival, and represents one-sided split service truthfully. Pending, success, and failure/source-unknown ownership use the approved `20,000 / 8,000 / 4,000 ms` intervals. Newer selection or lifecycle authority prevents late completion or expiry from stealing the drawer.

## Availability, revision, history, and unread

The Brain classifies:

- fresh complete applicable record: `Available`;
- fresh complete source with no applicable record: `ConfirmedUnavailable`;
- cacheless, missing, wrong-type, stale, failed, over-limit, or incomplete source: `SourceUnknown`.

Semantic identity includes ICAO, service role, normalized callsign/frequency/code, and SHA-256 of sanitized text lines with boundaries. Source generation and timestamp-only changes are no-ops. Code, text, frequency, callsign, role, or selected-content changes create one revision.

Only the automatic primary and successfully displayed lookup revisions enter history. Background non-primary churn creates zero history, unread, semantic generation, commands, resets, preparation, raster, upload, publication, or terminal work. History remains newest-first with the established `32-entry / 8-KiB body / 64-KiB aggregate` bounds.

Unread is tied to one exact opaque revision identity. It clears only when the Brain accepts a current-lifecycle first qualifying visible-frame terminal for the ATIS drawer carrying that exact identity. Older, other-drawer, hidden, failed, cancelled, superseded, stale, or duplicate facts cannot acknowledge it.

ORB semantics are:

- idle/unavailable/unknown: neutral gray `ATIS`;
- available read: cyan ICAO plus `INFO A` or `ATIS`;
- available unread: amber ICAO plus `NEW A` or `NEW`.

## Lifecycle contract

Reset XVatsim, confirmed new flight, callsign change, and cold/dark clear ATIS product state, lookup ownership, unread, and history at the existing boundary. Recover Current Flight preserves accepted state. Temporary xPilot disconnect/reconnect, overlay sleep/wake, and Plugin Admin suspend/resume preserve context/history while shared-feed restart can truthfully make source status unknown. Old-lifecycle facts are stale-rejected without new-lifecycle mutation.

Reset and Recover semantics were not redesigned.

## Presentation and publication integration

The Brain projects ATIS through the existing single accessory snapshot. The generic preparation worker receives that exact immutable command. The overlay applies the existing preparation key, presenter, raster, visible-publication coordinator, and queue.

The only renderer correction makes the Brain-owned `drawerStateText` visible before the generic empty text. This permits truthful pending/source-unknown state to render without creating a second semantic path.

The overlay copies the snapshot's opaque `atisVisibleRevisionIdentity` into the existing publication fact. The Brain alone decides whether that accepted visible terminal acknowledges unread.

## File-level implementation inventory

### Existing production/build files

- `brain/include/XVatsim/brain/BrainTypes.h` — bounded raw DTO and mechanical issue types.
- `modules/vatsim_data_feed/include/XVatsim/modules/vatsim_data_feed/VatsimDataFeedClient.h` — shared snapshot ATIS component facts.
- `modules/vatsim_data_feed/src/VatsimDataFeedClient.cpp` — behavior-neutral decoder seam and bounded root `atis[]` decoding.
- `brain/include/XVatsim/brain/BrainOwnedRuntime.h` — Brain ATIS state, decisions, revision/lookup interfaces, snapshot correlation.
- `brain/src/BrainOwnedRuntime.cpp` — accessory projection/lifecycle binding and visible acknowledgement integration.
- `brain/CMakeLists.txt` — registers the sole new Brain ATIS implementation unit.
- `plugin/src/XVatsimPlugin.cpp` — const fact transport, Brain-cycle invocation, text-entry routing, diagnostics.
- `modules/overlay/include/XVatsim/modules/overlay/OverlayAccessoryCore.h` — opaque ATIS visible-revision transport field.
- `modules/overlay/src/OverlayAccessoryCore.cpp` — render state-text ordering, terminal correlation, diagnostic serialization.
- `modules/overlay/src/OverlayWindow.cpp` — copies exact snapshot correlation through production terminal facts.
- `tools/regression_harness/CMakeLists.txt` — links the authorized production seams.
- `tools/regression_harness/src/main.cpp` — 24 unique Step 5 probes, legacy fixture migrations, stack-pressure heap migrations, and corrected callback timing boundary.
- `CMakeLists.txt` — registers the Step 5 visual-proof option/tool.

### New source/tool files

- `brain/src/BrainAtisRuntime.cpp` — sole ATIS semantic implementation unit.
- `tools/step5_atis_visual_proof/CMakeLists.txt`
- `tools/step5_atis_visual_proof/src/main.cpp`

### Director-authorized legacy Step 3 migrations

Six legacy scenarios were migrated without weakening their original generic behavior:

- ATIS placeholder expectation -> truthful no-accepted-endpoint state;
- two generic scroll/layout fixtures moved to PDC;
- three generic injected-history fixtures retained raw-history assertions while no longer treating synthetic history as Brain-owned product ATIS.

Their before/after hashes are recorded in the final receipt.

## Test infrastructure corrections and preserved stops

All mandatory-stop receipts remain unchanged. The material proof corrections were:

- 512-byte/513-byte and 300-record fixtures aligned to the approved mechanical limits;
- Scenario 24 settled-baseline and 100,000-generation background proof;
- six obsolete Step 3 ATIS fixture expectations migrated by Director authority;
- automatic large Step 4 and Step 5 harness fixtures moved to scoped heap storage while retaining the one-MiB PE stack guardrail; and
- Scenario 23 wall-clock timing ended before test-only assertion-message construction. Receipt 21 preserves the earlier `653 us` proof-boundary result; the corrected boundary measured at most `39 us` in the mandated final sequence.

No product fallback was added for any legacy-test conflict.

## Production-seam and proof quality

The Step 5 end-to-end seam executes bounded JSON decoding, immutable feed transport, Brain cycle/selection, Brain snapshot projection, generic preparation worker, presenter commit/render, visible publication, real queue, Brain terminal consumption, and production diagnostic serialization.

Final quality audit:

- Step 5 scenarios: `24`;
- unique probes: `24`;
- duplicate probe mappings: `0`;
- source-string-only behavioral proofs: `0`;
- literal claimed-success ledgers: `0`;
- direct expected terminal-fact injection as production-seam proof: `0`;
- production-test seam bypasses: `0`.

Directly constructed terminal facts are limited to isolated mechanical queue-capacity setup or stale-negative classification; production-seam success uses the production terminal builder and real queue/Brain consumer.

## Final proof result

- Step 5: `24/24`;
- Step 3: `31/31`;
- relevant Step 4: `276/276`;
- complete regression: `800/800` twice;
- canonical scenario lineage: `776 + 24 = 800`, with six Director-authorized legacy expectation migrations;
- canonical fingerprint: `609BE47845CE65F16B2478716439B9330A895BDF7AB9B06EF47D6BD51CEDAF8A`;
- visual proof: `48/48` twice, zero names/hashes/bytes/pixels/dimensions different;
- 1,000 clicks: exact accounting, callback max `39 us`, click-to-terminal max `100 us`;
- bounded 256-record Brain evaluation max observed: `394 us`;
- 100,000 background generations and 100,000 settled cycles: zero product/idle work;
- worker cancellation max: `15 ms`;
- real WinHTTP loopback: `10 ms`;
- fixture-off payload isolation: passed;
- protected evidence: `61 manifests / 2,420 rows / 0 mismatches`.

The detailed proof, preserved red result, mandatory-stop lineage, and measured receipts are in `outputs/v2_step_05_vatsim_atis_implementation_offline_proof/`.
