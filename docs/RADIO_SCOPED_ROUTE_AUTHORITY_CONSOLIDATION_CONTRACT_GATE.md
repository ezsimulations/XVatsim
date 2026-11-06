# Radio-Scoped Route Authority Consolidation Contract Gate

Date locked: 2026-10-27

Status: approved architecture implemented; focused and repository-wide offline
proof passed; candidate installed for controlled live proof. Gate refrozen
after the approved scoped RX activity correction.

## 1. Purpose

XVatsim must have one controller-identification path. The worldwide polygon
catalog remains complete static reference data, but the live controller set is
admitted only by the Brain-owned radio board. The filed route orders the static
polygons. The Brain compares the radio-lit polygons to that route and publishes
one immutable controller board consumed by both the UI and Standby Assist.

This gate corrects the repeated failure mode in which global live activation,
radio-board relevance, endpoint augmentation, and transmitter-distance gates
could each add, retain, delay, or reject a controller for different reasons.

## 2. Governing invariant

```text
worldwide static geometry (passive)
    + filed-route polygon order
    + complete local radio-board snapshot
        -> candidate-scoped mechanical ownership facts
        -> Brain radio-lit polygon decision
        -> Brain route/phase relevance decision
        -> one immutable final board
        -> UI and Standby Assist
```

The repository Bible remains binding:

> Brain decides. Modules produce bounded mechanical facts. UI displays
> Brain-approved facts.

For this gate, the Bible has these non-negotiable meanings:

1. No live controller absent from the Brain-owned radio board may enter
   controller relevance, the UI, or Standby Assist.
2. A worker may map a radio-board candidate to static polygons and report
   source ownership facts. It may not decide display relevance.
3. vNAS, SimAware, VATGlasses, FAA/NASR, AFV, and other sources may enrich or
   disambiguate a radio-board candidate. They may not create a live candidate.
4. The Brain is the sole owner of radio-lit state, current/next relationship,
   endpoint relationship, final row publication, and removal.
5. The UI and Standby Assist consume the same immutable Brain publication.

## 3. Contract supersession

This gate preserves the static geometry, exact-source ownership, typed terminal
relationship, conflict, freshness, and Brain-publication requirements of the
existing authority contracts.

It supersedes every requirement that authorizes or requires:

- worldwide live-controller activation before radio-board admission;
- a worldwide live authority map as an ordinary flight-loop prerequisite;
- routine 15-second or 60-second authority rebuilds while inputs are unchanged;
- APP/DEP candidate injection from global transceiver or controller evidence;
- route relevance only after a worldwide live activation pass;
- a second Center transmitter-distance veto after radio-board admission;
- a feature flag capable of restoring those retired decision paths; or
- a worker, cache, compatibility survivor, or legacy module to alter the
  Brain-owned candidate set.

In particular, the global-live requirements in
`CANONICAL_LIVE_AUTHORITY_POLYGON_MAP_CONTRACT_GATE.md` and
`BRAIN_OWNED_INCREMENTAL_LIVE_AUTHORITY_REFRESH_CONTRACT_GATE.md` are retired
where they conflict with this gate. Their exact ownership, immutable fact,
conflict, freshness, per-record reconciliation, and measured-proof requirements
remain binding.

## 4. Three distinct map states

### 4.1 Worldwide static catalog

The static catalog contains every accepted Center and APP/DEP polygon and its
source metadata. It is passive reference data. Loading or indexing this catalog
does not light a polygon and does not create a live controller.

### 4.2 Flight route projection

The route projection is the ordered sequence of static polygons intersected by
the filed route. It contains stable polygon identity, entry order, entry route
distance, and the aliases or exact source positions needed for candidate
mapping. It changes only when the flight plan, route resolution, or static map
generation changes.

Aircraft movement may advance current/next position through the existing route
projection. It must not rebuild worldwide ownership.

### 4.3 Radio-lit polygon set

The radio-lit set contains only polygons mechanically mapped to controllers in
the complete current radio-board snapshot. A radio-lit polygon may be off-route
or outside the active endpoint context. Lighting proves candidate-to-polygon
ownership; it does not itself approve a UI row.

An unresolved radio-board candidate remains visible in diagnostics as
unresolved evidence. It does not light a guessed polygon.

## 5. Center contract

A Center candidate is eligible only when:

1. it is present on the current Brain-owned radio board;
2. it is operationally eligible;
3. its identity/frequency is mechanically mapped to route polygon ownership;
4. the owned polygon is the current route polygon or the immediately next route
   polygon; and
5. the Brain accepts the complete fact packet.

Publication is relational, not range-based:

- current polygon: publish green;
- immediately next polygon: publish orange;
- later or unrelated polygon: do not publish;
- one controller owning current and next: publish one green row;
- several proven controllers owning current or next: retain every distinct
  proven row with the corresponding relationship color.

There is no Center transmitter-distance display gate. The 250 NM constant,
`centerRadioRangeEnabled`, its rollback behavior, and any receipt claiming
`channel-within-250nm` are retired.

The radio-board acquisition boundary remains a separate source constraint. If
a controller first appears on the radio board after the route has already
identified its polygon as next, it is evaluated immediately when admitted. The
Brain may not invent the missing controller to guarantee an earlier display.

## 6. Terminal APP/DEP contract

APP/DEP uses the same radio-board admission boundary as Center.

- Global transceiver evidence may not append missing APP/DEP candidates.
- Exact source position, source-published parent/child ownership, endpoint
  containment, route intersection, and endpoint airport-frequency facts may
  establish or reject the relationship for an admitted candidate.
- Distance alone cannot establish terminal ownership.
- A lit terminal polygon outside the departure/arrival route and endpoint scope
  remains absent from the final board.
- Ambiguous, conflicted, stale, or incomplete ownership fails closed with an
  auditable Brain receipt.

## 7. Runtime scheduling contract

### 7.1 Initial board

The first complete usable radio board becomes the candidate baseline. Every
candidate requiring ownership proof is verified once. Reusable exact facts may
come from a valid Brain-owned cache for the same static-map generation.

### 7.2 Additions and changes

Added or authority-semantically changed board records are verified. Unchanged
records retain their accepted facts. Passing a complete scoped snapshot to a
mechanical compiler is permitted when its per-record cache proves that only the
changed records were recomputed.

### 7.3 Removals

A removal may be accepted only from a fresh, complete radio-board snapshot.
The Brain removes that candidate and every derived lit assignment and final row
for that candidate. A partial or stale source cannot prove removal.

### 7.4 Idle

When radio-board identity, route identity, phase, relevant dynamic ownership,
and static-map generation are unchanged:

- no authority dispatch;
- no worldwide roster scan;
- no ownership recomputation;
- no route rebuild; and
- the last immutable Brain publication remains in use.

There is no routine authority cadence. A bounded retry is permitted only after
a requested proof failed or was rejected and no accepted snapshot exists for
the current exact input identity.

### 7.5 Dynamic ownership

Global vNAS acquisition may remain a source adapter, but ordinary decision
identity includes only ownership facts whose exact controller identity is on
the current radio board. Unrelated vNAS roster changes cannot invalidate the
flight's authority publication.

## 8. Required observability

Every admitted candidate must be traceable through:

```text
radio-board stable key
    -> candidate-to-polygon mapping or unresolved reason
    -> current / next / endpoint / unrelated relationship
    -> Brain acceptance or rejection reason
    -> final board generation
```

Diagnostics must distinguish:

- static polygon present but unlit;
- radio candidate unresolved;
- radio candidate mapped to a lit off-route polygon;
- mapped current polygon;
- mapped next polygon;
- terminal endpoint ownership accepted or rejected; and
- final UI publication.

## 9. Prohibited shortcuts

The implementation may not:

- add airport, facility, callsign, frequency, or geographic exceptions;
- use transmitter distance to override a proven current/next Center relation;
- infer a terminal owner from proximity;
- keep both global-live and radio-scoped activation authoritative;
- hide the retired path behind a feature flag;
- let a worker silently add or remove live candidates;
- use a count-only or generation-only identity;
- treat descriptive ATIS text as ownership invalidation;
- cancel useful verification merely because an unrelated worldwide controller
  changed; or
- claim completion from compilation alone.

## 10. Offline proof gate

The following must pass before a candidate XPL is installed:

1. **Center range retirement:** a radio-board Center mapped to current or next
   remains eligible even when its selected transmitter is beyond 250 NM.
2. **Vancouver replay:** `CZVR_CTR` is accepted immediately when it appears on
   the radio board and maps to the next route polygon; no later 250 NM
   transmitter gate delays it.
3. **Boundary roll-forward:** next Center is orange, becomes green on polygon
   transition, and the new next polygon is evaluated without a world rebuild.
4. **Multiple Center staffing:** every distinct proven current/next Center row
   is retained; current takes precedence for a controller owning both.
5. **KNGU/Potomac rejection:** a radio-lit PCT terminal polygon that does not
   own the route endpoint cannot enter the final board.
6. **No terminal injection:** an APP/DEP controller absent from the raw radio
   board cannot appear after endpoint evidence processing.
7. **Unrelated-world churn:** changing a controller outside the radio board
   produces zero authority verification work and no publication change.
8. **Incremental additions:** after a ten-controller baseline, adding two
   controllers verifies the two changed records and reuses the other ten.
9. **Complete removal:** a fresh complete board removal clears exactly the
   removed candidate and its derived assignments.
10. **Idle:** unchanged inputs record no authority dispatch or recomputation.
11. **Consumer parity:** UI and Standby Assist consume the same final board
    generation.
12. **Full regression:** every saved scenario and focused contract probe passes.

Measured proof must record candidate counts, verified/reused/removed facts,
worker time, dispatch count, final row set, relation/color, and decision reason.

## 11. Live proof and rollback

The known-good XPL remains untouched during offline work. The candidate is
built to the repository output only. Before live replacement, record hashes
for the recovery XPL and the candidate XPL.

Rollback is binary/source recovery, not a second decision mode:

- preserve the prior installed XPL;
- preserve a source diff or commit boundary for this gate;
- do not retain a runtime flag that restores worldwide activation, endpoint
  injection, or the 250 NM Center veto; and
- if live proof fails, reinstall the prior binary and diagnose the single new
  pipeline rather than reactivating legacy authority.

## 12. Completion criteria

This gate is complete only when:

- the production loop has one radio-board admission boundary;
- worldwide geometry is passive and worldwide live activation is absent from
  ordinary controller decisions;
- Center current/next publication is range-independent;
- APP/DEP cannot bypass radio-board admission;
- exact scoped changes drive authority work and unchanged state is idle;
- superseded comments, fields, tests, and rollback switches are removed;
- offline proof passes with recorded measurements; and
- controlled live flights confirm correctness without performance regression.

## 13. Implementation and offline proof record — 2026-10-27

Implemented boundaries:

- the production authority feed is built from the complete current radio-board
  snapshot, not the worldwide controller feed;
- a delta verification feed cannot claim roster completeness or prove removals;
- vNAS facts are filtered to exact admitted radio-board callsigns before their
  identity can invalidate authority work;
- worldwide endpoint evidence can no longer inject an APP/DEP candidate;
- the Center transmitter-distance veto and its runtime rollback path are
  removed;
- routine 15/60-second authority rebuilds are removed; only exact-input retry
  remains when no accepted proof exists;
- obsolete production settings that advertised restoration of the retired
  terminal/Center paths are removed; and
- the old worldwide-live contracts are explicitly marked partially
  superseded rather than left as competing authority.

Proof results:

- `XVatsimPlugin` RelWithDebInfo build: pass;
- standalone `XVatsimRegressionHarness` RelWithDebInfo build: pass;
- controller operational eligibility/scoping probe: pass, including complete
  radio-board publication, no global terminal injection, exact vNAS board
  scope, unrelated vNAS churn ignored, and relevant vNAS change detected;
- Vancouver `CZVR_CTR` replay at a 299 NM selected transmitter: accepted as
  `center-next-polygon-match`;
- 250.0/250.1 NM Center boundary replay: both current-polygon controllers
  accepted;
- current/next UI relation and tones: current `Active`/green and both next
  rows `Next`/orange;
- polygon-transition stale tuned Center rejection: pass;
- KNGU/Potomac adjacent terminal rejection: pass;
- worldwide APP absent from the raw radio board: not injected; and
- ten legacy terminal-selection fixtures now declare their radio-board
  candidates explicitly and retain their original ownership-selection proof.

Repository suite result: `927 / 927` saved scenarios passed. The compatibility
matrix retains both xPilot generations: xPilot 3.2 uses the legacy integration,
while the current machine's xPilot 4.0.0 beta 9 uses the read-only companion.
The beta 9 X-Plane plugin (`2,257,072` bytes, SHA-256
`DEA6B12F64C0E35F9E61398B9AA659C3A8896886B4890D4EC89C50ACD6CD22A2`)
is canonicalized and hashed once, then proven to fail closed against the legacy
xPilot 3 private-dataref fingerprint gate. Synthetic exact-fingerprint tests
continue to protect the legacy qualification in both positive and negative
directions. The Manager dual-generation compatibility probe passed `40 / 40`.
The read-only xPilot 4 companion rebuilt against pinned SDK commit `3f3472f`
with zero warnings/errors and its probe passed `21 / 21`.

The candidate XPL remained in the repository build output throughout offline
proof; passing the compatibility suite did not itself install the plugin.

Rollback identities recorded without replacing the installed plugin:

- installed known-good XPL: `3,193,344` bytes, SHA-256
  `FC63F81753D0C6D657625E1AC334BA012FE283912B4B10DFDF45AD8CB4785D87`;
- repository candidate XPL: `6,761,472` bytes, SHA-256
  `5CF635446388B4BD0A676C8B5163E4FDA8262B0726E4728E51E2F5100787C5EB`.

## 14. Approved scoped RX activity correction — 2026-10-27

The gate was temporarily unfrozen to correct a radio-state defect discovered
during live observation. X-Plane's `sim/atc/com1_active` and
`sim/atc/com2_active` datarefs report activity on the tuned frequency; they do
not prove that this aircraft is currently receiving audio. The legacy sampler
incorrectly used those facts both as direct RX evidence and to refresh the
short RX latch, which could leave the XVatsim COM1 or COM2 RX indicator green
when xPilot reported no receive activity.

The correction is deliberately outside controller-authority decision logic:

- only exact xPilot receive state, X-Plane `sim/atc/com*_rx`, an explicit
  receive override, or the short latch created by one of those exact receive
  signals may assert the pilot-facing RX state;
- `sim/atc/com*_active` remains available only as tuned-frequency activity and
  radio-usability evidence; it cannot assert or refresh RX;
- COM1 and COM2 use the same pure receive-evidence decision; and
- UI rendering continues to consume the sampled radio state without a second
  interpretation path.

Proof after the correction:

- focused receive-activity probe: pass, including frequency activity alone,
  exact xPilot RX, exact simulator RX, explicit override, exact-signal latch,
  and unpowered-radio cases;
- `XVatsimPlugin` and `XVatsimRegressionHarness` RelWithDebInfo builds: pass;
- runtime activation/lifecycle probe: pass;
- controller operational eligibility/scoping probe: pass; and
- repository scenario suite: `927 / 927` passed.

No installed XPL was changed during implementation or offline proof. The
known-good recovery fingerprint above remains valid; the rebuilt repository
candidate fingerprint above includes this correction. The authority gate is
refrozen with no additional scope expansion.

Controlled-live deployment record:

- X-Plane shutdown was verified before replacement;
- the candidate was installed to
  `C:\X-Plane 12\Resources\plugins\XVatsim\win_x64\XVatsim.xpl`;
- installed candidate: `6,761,472` bytes, SHA-256
  `5CF635446388B4BD0A676C8B5163E4FDA8262B0726E4728E51E2F5100787C5EB`;
- the prior known-good binary was preserved as
  `XVatsim.rollback-FC63F81753D0C6D6.xpl`, SHA-256
  `FC63F81753D0C6D657625E1AC334BA012FE283912B4B10DFDF45AD8CB4785D87`;
  and
- controlled live proof remains pending.

## 15. Deferred post-flight amendment candidates — UPS344, 2026-10-28

Status: **NOT APPROVED FOR IMPLEMENTATION; RECORD ONLY UNTIL THE LIVE FLIGHT
ENDS AND A NEW CONTRACT GATE IS REVIEWED.**

The live `UPS344` flight from `PANC` to `KONT` retained three post-flight gate
items.
Initial flight-plan activation exposed a synchronous endpoint-polygon query on
the simulator thread. Later, the Vancouver-to-Seattle transition exposed a
conflict between the route-polygon Center contract and the older 300 NM
radio-board candidate envelope. Vancouver Center was the current route polygon
and offline. Seattle Center was online and owned the immediately next route
polygon, but its selected transmitter was outside the 300 NM envelope, so it
was removed before the Brain could apply the already-approved next-polygon
rule.

The post-flight contract gate must address all three items together:

1. **Synchronous endpoint-polygon queries on X-Plane's simulator thread.** At
   initial flight-plan activation, confirmed plugin CPU blocked the simulator
   thread for `164.993 ms`. The callback invokes both endpoint refreshes from
   `plugin/src/XVatsimPlugin.cpp`; the nominal Brain "worker" call executes
   `QueryEndpointPolygons(...)` synchronously; and each endpoint query scans
   the complete `59,244`-polygon catalog. The first scan also performs polygon
   intersection work. The new gate must move endpoint query preparation,
   point containment, intersection discovery, and any required spatial-index
   construction off the flight loop. The module may publish only an immutable
   mechanical fact. The Brain must validate request identity, source
   generation, flight identity, endpoint coordinates, freshness, and
   lifecycle before accepting it. The old synchronous production path must be
   removed rather than hidden behind a cache, setting, or fallback. Required
   proof includes the full `59,244`-polygon fixture, first-activation and
   changed-flight-plan cases, exact result parity, stale-completion rejection,
   bounded worker depth, clean shutdown, and measured flight-loop wall/CPU
   time showing that neither full-catalog scans nor polygon intersection run on
   the simulator thread.

2. **Center admission and polygon publication.** Retire the fixed 300 NM
   candidate envelope as a Center admission or rejection rule. A live Center
   that is exactly mapped to the current route polygon must publish green; a
   live Center exactly mapped to the immediately next route polygon must light
   that polygon and publish orange, regardless of transmitter distance.
   Transmitter distance may remain a mechanical audio/reachability and
   diagnostic fact, but it may not veto proven current/next Center ownership.
   An online Center such as Memphis that owns neither the current nor next
   route polygon remains absent from the UI because of its route relationship,
   not because of an arbitrary range threshold. The design must remain
   route-scoped and indexed; it must not restore worldwide live-polygon work.
   Required proof must replace the misleading Vancouver fixture that claims to
   ignore transmitter distance while using only `299 NM`; it must include an
   online next-polygon Center beyond 300 NM (including the observed Seattle
   case at approximately 460 NM), an unrelated distant Center negative case,
   current-to-next boundary advancement, polygon lighting, green/orange UI
   publication, and confirmation that UI and Standby Assist consume the same
   immutable Brain result.

   Additional live proof from the same flight identifies a second ambiguity
   inside this item. At tick `125428`, the fresh accepted radio-board fact
   admitted `LAX_251_CTR` and `LAX_25_CTR` on `126.525` because their selected
   AFV transmitter distance was logged as `299 NM`. The UI distance later seen
   near `342 NM` was a different measurement: remaining route distance to the
   `KZLA` entry (`entryNm=1964` minus current route progress near `1621 NM`).
   The range figures therefore did not bypass the 300 NM check. The route
   decision was nevertheless wrong: the authoritative transition state was
   `current=KZSE`, `next=KZOA`, `final=KZLA`, yet both LAX candidates were
   accepted as `NEXT_POLYGON` matched to `KZLA`.

   The structural cause is that the legacy `RouteSectorSnapshot.nextSectors`
   collection contains all future route sectors, while the newer publication
   contract uses "next" to mean only the immediately following polygon.
   `BrainRoutePolygonWorker` correctly selects the first future key as
   `nextPolygonKey`, but Center evaluation scans the complete future-sector
   collection and labels any match `NEXT_POLYGON`. The new gate must replace
   this ambiguous shared meaning with typed immediate-next and later-future
   facts, require exact equality with the Brain-owned current or immediate-next
   polygon identity, and prove that a later/final route Center cannot publish
   early. The same proof must determine whether the two same-frequency LAX
   callsigns are distinct source-owned positions or aliases requiring one
   Brain-owned identity/coalescing result; neither a UI-only dedupe nor a
   callsign-specific exception is permitted.

3. **xPilot 4 controller-board source parity.** Determine whether the official
   xPilot 4 Plugin SDK can expose the exact filtered Nearby ATC/controller board
   shown to the pilot. The existing companion already consumes
   `IBroker.GetControllers()` plus controller add, delete, frequency-change,
   and location-change events, but the SDK contract describes that snapshot as
   every controller xPilot currently tracks from first position update until
   deletion. It does not promise that the snapshot equals xPilot's displayed
   board. XVatsim currently uses this snapshot only as neutral correlation
   evidence after a controller has survived XVatsim's separate AFV/range
   candidate gate, so it cannot restore a controller discarded before Brain
   relevance. The preferred design is an official read-only xPilot board
   snapshot or event stream that becomes the sole candidate-admission source
   for xPilot 4; the Brain must remain sole owner of operational eligibility,
   exact polygon ownership, current/next relationship, UI publication, and
   Standby Assist. AFV distance then remains audio/diagnostic evidence rather
   than a competing admission gate. If the official SDK cannot expose the
   displayed board, the gate must first run a controlled live parity study of
   `GetControllers()` versus xPilot's visible board before authorizing that raw
   tracked set as the xPilot 4 candidate source. No reflection, private API,
   binary hook, or inferred internal xPilot state is permitted. xPilot 3.2 must
   retain its independently qualified adapter, with both generations producing
   one normalized immutable controller-source fact for the Brain.

   Required proof includes startup and mid-session complete snapshots,
   add/delete/frequency/location changes, reconnect and snapshot recovery,
   exact visible-board parity when an official board source exists, deliberate
   source disagreement, stale/incomplete-source fail-closed behavior, xPilot 3
   regression protection, and confirmation that no bridge or adapter decides
   polygon ownership, display color, ordering, Assist selection, or COM writes.

The new gate must explicitly reconcile and supersede Sections 5 and 10 of this
contract wherever the preserved radio-board acquisition boundary conflicts
with route-polygon Center admission. It must identify and remove the old
distance-veto code rather than adding a rescue branch or feature flag. A
rollback snapshot, focused red/green tests, complete saved-scenario replay,
performance comparison, and controlled live proof are mandatory before any
commit.
