# Brain-Owned Incremental Live Authority Refresh Contract Gate

**Gate ID:** `BRAIN-OWNED-INCREMENTAL-LIVE-AUTHORITY-REFRESH-01`  
**Status:** Partially superseded by
`RADIO_SCOPED_ROUTE_AUTHORITY_CONSOLIDATION_CONTRACT_GATE.md`. Per-record cache,
non-cancelling work, completeness, immutable facts, and Brain reconciliation
remain binding. The ordinary roster is now the complete current radio board,
not the worldwide controller population.  
**Prepared:** 2026-10-26  
**Repository baseline:** `6b39f7b2351abc2b4c635b4d13650b62e9563d9f`
plus the current uncommitted canonical-map, terminal-relationship,
Center-selection, and Standby Assist work  
**Motivating live flight:** `GTI8274`, `KNGU` to `EDFH`, 2026-10-26  
**Scope:** VATSIM controller-roster ingestion, vNAS dynamic-ownership changes,
canonical live-authority activation, asynchronous authority work, Brain cache
admission, immutable map publication, controller boards, diagnostics, and
rollback

## 1. Objective

XVatsim must process live authority changes incrementally without allowing an
asynchronous worker, plugin host, cache, or compatibility path to become a
second decision-maker.

If the Brain has requested verification of ten controller records and two more
controllers appear while that work is running, the ten-record request must be
allowed to finish. The Brain must reconcile its returned facts against the
newest complete roster, retain every still-current verified fact, and request
verification only for the two new records. Routine controller-roster churn may
not cancel the active request, erase its mechanical cache, or restart a full
world verification.

The correction is both a performance contract and an authority contract. It
may not be implemented as a smarter worker that privately changes the live
map. The worker performs bounded mechanical work and returns immutable facts.
The Brain alone decides:

- which roster generation is admissible;
- which records are additions, removals, or changes;
- which bounded verification request exists;
- which returned facts remain current;
- whether a controller activates, conflicts, remains pending, or is unmapped;
- when a controller assignment is removed;
- which immutable live-map generation is published; and
- which controllers reach a radio board, UI row, or Standby Assist input.

Required end state:

```text
complete normalized source roster facts
    + canonical static map generation
    + current dynamic-ownership facts
        -> Brain accepts newest source generation
        -> Brain compares it with its verified controller ledger
        -> Brain issues one bounded baseline or delta request
            -> worker returns per-record mechanical fact packets
        -> Brain reconciles every returned packet with current truth
        -> Brain decides active/conflicted/pending/unmapped assignments
        -> Brain atomically publishes one immutable live authority map
            -> indexed aircraft/route/endpoint queries
            -> Brain controller decisions
            -> immutable final boards
            -> UI and Standby Assist consume only final boards
```

The governing efficiency invariant is:

```text
ordinary live work = records whose authority-semantic identity changed
```

The governing authority invariant remains:

```text
workers report facts; the Brain decides; the UI only displays Brain output
```

## 2. Governing Law

The repository Bible remains binding:

> Brain decides. Modules produce bounded mechanical facts. UI displays
> Brain-approved facts.

For this gate, that law has the following non-negotiable meaning:

1. A worker may not directly add, retain, suppress, hide, remove, or replace a
   live controller assignment.
2. A worker may not decide that absence from a roster means a controller went
   offline.
3. A worker may not decide source precedence, freshness policy, conflict
   resolution, endpoint ownership, route relevance, board eligibility, or
   display eligibility.
4. A worker cache hit is still a returned fact. It may not silently omit a
   requested controller.
5. The plugin host transports snapshots, requests, completions, lifecycle
   events, and simulator side effects. It may not own the incremental refresh
   policy.
6. The Brain owns the desired generation, verified ledger, pending delta,
   completion reconciliation, activation decision, and final publication.
7. The UI and Standby Assist consume only the Brain-published final board and
   may not inspect worker state or pending roster records to make decisions.

No performance improvement can weaken these boundaries.

## 3. Relationship to Existing Contracts

This gate is a required runtime refinement of:

- `docs/ARCHITECTURE.md`;
- `docs/CANONICAL_LIVE_AUTHORITY_POLYGON_MAP_CONTRACT_GATE.md`;
- `docs/TERMINAL_ENDPOINT_AUTHORITY_RELATIONSHIP_CONTRACT_GATE.md`;
- `docs/BRAIN_OWNED_RUNTIME_CONTRACT.md`; and
- `docs/AUTHORITY_EVIDENCE_CONTRACT.md`.

It does not change the canonical static geometry model, the typed terminal
relationship model, Center-row click policy, airport-local policy, CTAF/UNICOM
policy, or Standby Assist hierarchy. It changes how live controller and
dynamic-ownership changes are scheduled, mechanically verified, reconciled,
and published.

If an older performance or worker contract requires routine authority work to
cancel and restart whenever a newer controller roster arrives, this gate
supersedes that requirement. Latest-only buffering remains valid for work that
has not started; it may not destroy useful in-flight authority work merely
because an ordinary roster generation arrived.

This gate does not authorize restoration of any legacy vote, split polygon
catalog, route-scoped activation rule, inference activator, compatibility
survivor, or worker-side final authority.

## 4. Confirmed Current Defect

The current runtime correctly detects roster content changes and retains at
most one newest pending request. It does not lose the newest roster
permanently. The defect is how it reacts while authority verification is
already running.

The audited path currently does the following:

1. `RefreshBrainAuthorityRelevanceSnapshot` calculates a controller-content
   digest and treats a changed digest as changed expected inputs.
2. `StartLatest` replaces any pending request with the newest full request.
3. If a request is running, `StartLatest` raises cooperative cancellation and
   marks the authority engine for reset.
4. The running result is cancelled or rejected as superseded.
5. The next request reprocesses the complete roster after the engine cache has
   been reset.

Relevant audited locations include:

- `plugin/src/XVatsimPlugin.cpp`, the current expected-identity, dispatch, and
  all-or-nothing completion path;
- `modules/route_sector/src/RouteSectorResolver.cpp`,
  `AuthorityRelevanceWorker::StartLatest`;
- the worker's `cancelRequested` and `resetEngineBeforeNext` behavior; and
- `brain/src/BrainRoutePolygonWorker.cpp`, where the authority-controller
  projection and semantic hashes are currently constructed.

The current digest also includes ATIS presence and complete ATIS text. Those
fields are not allowed to create canonical authority, yet a descriptive ATIS
change can currently contribute to invalidating authority work.

### 4.1 Preserved live evidence

During live `GTI8274 KNGU-EDFH` observation:

- authority request `44` completed and published after approximately
  `25.859 seconds`;
- subsequent authority requests `46` through `55` were dispatched as roster
  and ownership digests continued to change;
- each active replacement reported `running=1 pending=1` and a maximum pending
  depth of one;
- roster-wide controller digests changed repeatedly while the flight, route,
  and canonical dataset remained stable; and
- the UI remained safe by reusing last-proven evidence when allowed or failing
  closed, but a newly relevant controller could not be admitted until a full
  current request completed.

This proves the newest roster is remembered, but it also proves that ordinary
worldwide roster churn can repeatedly discard useful work and delay
convergence.

## 5. Product Meaning of Incremental Refresh

### 5.1 Initial baseline

The first complete usable roster for a runtime session is a baseline. The
Brain requests verification for every authority-semantic controller record in
that baseline that is not already covered by a valid restored Brain ledger.

A baseline completion is not accepted wholesale merely because the worker
finished. The Brain reconciles every returned record against the newest source
truth and accepts only records whose complete semantic identity and applicable
source identities remain current.

### 5.2 Addition

A controller is an addition when the newest complete roster contains a stable
controller key that the Brain's verified ledger does not contain.

An addition remains `PENDING_VERIFICATION` until the Brain accepts its exact
fact packet. A worker may not light or display it directly.

### 5.3 Removal

A controller is a removal only when a newer complete, fresh, usable roster
proves that its stable key is absent. A stale, partial, unavailable, malformed,
or disconnected feed cannot prove removal.

The Brain decides the removal. No geometry or mapping worker is required to
verify that a source-proven online record is no longer present. The Brain must
remove the affected live assignment from the next publication without waiting
for an unrelated full-map rebuild.

### 5.4 Changed record

A controller record is changed when its stable key remains but an
authority-semantic field changes. It is processed as:

```text
Brain retires the prior accepted semantic record
    + Brain requests verification of the new semantic record
```

Until the new record is verified, the old assignment may not be presented as
if it still describes the new frequency, facility, position, or ownership.

### 5.5 Unchanged record

An unchanged controller record retains its accepted fact packet and canonical
assignment without worker re-execution. A transport generation change, record
ordering change, timestamp change, controller count elsewhere, or ATIS text
change does not make an unchanged authority-semantic record dirty.

## 6. Required Brain-Owned Runtime State

Names may follow repository conventions, but the Brain must own equivalents of
the following state.

### 6.1 Desired roster snapshot

The latest normalized source snapshot accepted for consideration, including:

- immutable controller records;
- transport generation and fetch identity;
- content/semantic generation;
- source availability, freshness, and completeness;
- stable controller keys and per-record semantic signatures; and
- a deterministic whole-roster semantic digest used for diagnostics, not as a
  command to discard every cached record.

### 6.2 Verified controller ledger

The ledger is the Brain's accepted cache list. Each entry contains:

- stable controller key;
- accepted controller semantic signature;
- canonical static-map identity;
- applicable dynamic-ownership identity;
- immutable worker fact packet or Brain-owned normalized projection;
- exact matched and rejected source-position facts;
- activation/conflict/unmapped decision and reason;
- accepted Brain generation; and
- last source generation in which the record was proven present.

The ledger is Brain-owned semantic state. A worker may maintain mechanical
memoization, but its private cache is not this ledger and is not authority.

### 6.3 Desired-versus-verified delta plan

The Brain produces one explicit plan containing:

- additions requiring verification;
- changed records requiring retirement and verification;
- removals proven by a complete current source;
- unchanged records eligible for reuse;
- records affected by a scoped dynamic-ownership change;
- records invalidated by a canonical static-map change;
- deferred records and the reason for deferral; and
- the exact input-generation tuple against which completion will be judged.

### 6.4 Accepted live-map snapshot

Only the Brain publishes the immutable accepted live-map snapshot. It contains
the exact source and semantic generations, all accepted live assignments,
pending records, conflicts, unmapped records, health state, and indexes needed
by bounded queries.

## 7. Stable Identity and Semantic Signatures

Incremental processing is safe only if identity is explicit.

### 7.1 Stable controller key

The preferred key is a provider-issued stable live-session or position
identity joined to normalized exact callsign. If the provider does not expose a
stable session identity, normalized exact callsign is the fallback live-record
key. Frequency alone is never an identity.

Same-frequency controllers must remain distinct. A controller reconnect that
creates a new provider session must not inherit a stale accepted record without
Brain reconciliation.

### 7.2 Authority-semantic signature

The per-controller signature includes only fields that can change canonical
activation facts, such as:

- exact normalized callsign or provider identity;
- live primary frequency;
- facility/role;
- actionable/online qualification;
- exact source-position identity when supplied by the feed; and
- applicable provider-owned semantic fields formally accepted by the
  canonical-map contract.

The following must not invalidate authority verification by themselves:

- transport generation;
- fetch timestamp;
- input ordering;
- ATIS text;
- controller name or descriptive metadata;
- visual presentation state;
- radio-board ordering; or
- aircraft position and route progress.

If a future field is added to the semantic signature, the closeout must state
which Brain decision it can change and add a focused regression.

### 7.3 Dynamic-ownership signature

Dynamic ownership is indexed by exact affected facility, position, controller,
sector, or polygon identities. A change invalidates only ledger entries whose
accepted facts depend on the changed ownership records.

A global vNAS content generation may be retained for provenance, but it may not
force a worldwide controller rebuild when an exact affected-key set can be
proven.

### 7.4 Canonical static-map signature

A canonical geometry, position-definition, topology, or source-precedence
change can require a staged full re-verification. That is a structural
invalidation, not ordinary roster churn. Old and new static-map generations may
not be mixed in one accepted live-map snapshot.

## 8. Brain Scheduling State Machine

The Brain must own an explicit state machine equivalent to:

```text
IDLE
    -> BASELINE_REQUESTED
    -> VERIFYING
        -> FOLLOW_UP_DIRTY       when newer usable facts arrive
        -> COMPLETION_RECEIVED
            -> RECONCILE
            -> PUBLISH_ACCEPTED_SUBSET
            -> DELTA_REQUESTED   when work remains
            -> IDLE              when desired and verified state converge
```

Required behavior:

1. While `VERIFYING`, ordinary roster additions, removals, changes, and scoped
   ownership changes update Brain desired state and mark follow-up work dirty.
2. They do not cancel the running request.
3. The Brain retains only the newest complete desired source snapshot; it does
   not queue an unbounded generation history.
4. When completion arrives, the Brain examines every returned record.
5. Returned records still identical in the newest desired roster may populate
   the verified ledger even if the completion's whole-roster generation is no
   longer current.
6. Returned records now removed or changed are rejected with explicit reasons.
7. Source-proven removals are applied by a Brain decision.
8. The Brain publishes the largest internally consistent current subset and
   marks still-unverified additions/changes pending.
9. The Brain then issues exactly one delta request for the remaining dirty
   records.
10. Convergence is reached when the desired roster, verified ledger, applicable
    ownership generation, and published live map agree.

No worker may transition this state machine autonomously.

## 9. Worker Contract

The authority worker receives one immutable Brain request containing:

- request and lifecycle identity;
- canonical static-map identity and read-only indexed catalog;
- exact controller records to process;
- exact applicable dynamic-ownership facts;
- explicit operation kind: baseline, addition, changed-record verification,
  ownership re-evaluation, or staged static-map rebuild;
- prior mechanical cache handles explicitly admitted by the Brain, when
  applicable; and
- cancellation token limited to the cases defined below.

For every requested controller key, the worker returns exactly one immutable
fact packet containing:

- input semantic signature and generation tuple;
- exact source-position matches;
- candidate canonical polygon assignments;
- rejected matches and typed mechanical reasons;
- relevant dynamic-ownership joins and provenance;
- conflicts discovered mechanically without resolving them;
- cache-hit status and cache key;
- elapsed and bounded-work counters; and
- completion/cancellation state.

The worker output must not contain a final UI/display instruction. If existing
types retain fields named `accepted`, `relevant`, `active`, `hidden`, or
`display`, those fields must either be removed or explicitly converted to
mechanical candidate facts that cannot reach publication without a Brain
decision.

### 9.1 Allowed cancellation

Cancellation is allowed for:

- plugin unload or process shutdown;
- explicit full runtime reset;
- invalid lifecycle/session identity;
- replacement of the canonical static-map generation when the active request
  cannot produce reusable per-record facts; and
- a proven source-integrity failure that makes continued reads unsafe.

### 9.2 Prohibited routine cancellation

Cancellation is not allowed merely because:

- a controller came online;
- a controller went offline;
- a controller frequency or facility changed;
- ATIS text changed;
- the roster transport generation advanced;
- an unrelated worldwide controller changed;
- aircraft position or route progress changed; or
- a newer ordinary delta is waiting.

## 10. Cache Contract

### 10.1 Brain semantic cache

The verified controller ledger is the only semantic authority cache. Cache
admission and eviction are Brain decisions with logged reasons.

### 10.2 Worker mechanical cache

A worker may memoize deterministic parsing, indexed lookup, geometry, and join
results when keyed by every semantic input that can alter those results.

A mechanical cache must:

- return the same complete fact packet as fresh execution;
- expose cache hit/miss and full key identity to diagnostics;
- never omit a requested record;
- never apply source freshness or removal policy;
- never publish or mutate Brain state;
- be bounded in retained entries and bytes;
- retire large objects off the simulator thread; and
- be safe to delete without changing product decisions.

### 10.3 No cache reset on ordinary churn

Ordinary roster or scoped ownership deltas may not erase unaffected mechanical
cache entries. Full cache invalidation requires a typed structural reason and
must be visible in the Brain receipt.

## 11. Source Completeness, Removal, and Failure Rules

Absence is authoritative only from a fresh, complete roster generation.

If a source is stale, incomplete, malformed, or unavailable:

- the source worker reports that fact without inventing removals;
- the Brain decides whether the last-proven assignment may temporarily remain
  usable under the existing source-health contract;
- a new controller cannot be admitted from incomplete evidence;
- a prior controller cannot be silently removed merely because a partial list
  omitted it; and
- the UI receives only the Brain's final health-aware publication.

If a complete roster proves a controller signed off, the Brain must prevent
the old assignment from appearing in the next accepted map generation even if
a stale worker completion still contains it.

## 12. Atomic Publication and Consumer Consistency

Each accepted live-map generation is immutable and internally consistent.

One publication must never mix:

- controller semantic facts from different signatures for the same key;
- old and new canonical static-map identities;
- incompatible dynamic-ownership facts;
- removed controllers with a newer complete roster that proves absence; or
- worker candidate facts with Brain decisions from a different request.

Route, aircraft, departure, arrival, controller-evidence, final-board,
diagnostic, UI, and Standby Assist consumers must preserve the accepted map and
decision generation in their receipts. A consumer may query or project; it may
not repair, supplement, or reinterpret a pending delta.

## 13. Required Diagnostics

Every authority-refresh cycle must make the following observable:

- desired roster generation and semantic digest;
- source availability, freshness, and completeness;
- accepted live-map generation;
- verified-ledger entry count;
- added, removed, changed, unchanged, ownership-affected, and pending counts;
- active request ID, kind, and record count;
- whether newer desired facts arrived during execution;
- per-record completion admission or rejection reason;
- follow-up request ID and exact record count;
- structural versus ordinary invalidation reason;
- worker cache hits, misses, retained entries, and retained bytes;
- ordinary cancellations and structural cancellations as separate counters;
- time from source observation to Brain convergence;
- simulator-thread dispatch, reconciliation, and publication time;
- worker wall/CPU time without relabelling it as simulator-thread work; and
- final map, board, UI, and Standby Assist generation parity.

Required reason examples include:

- `unchanged-ledger-reuse`;
- `new-controller-verification-pending`;
- `changed-controller-old-fact-retired`;
- `complete-roster-controller-removed`;
- `completion-record-still-current-accepted`;
- `completion-record-superseded-rejected`;
- `scoped-ownership-delta`;
- `static-map-structural-rebuild`;
- `source-incomplete-removal-not-proven`; and
- `lifecycle-cancellation`.

Diagnostics describe decisions; they do not make them.

## 14. Mandatory Pre-Fix Red Proofs

Before changing production behavior, the current production seams must
preserve these failures:

1. **Ten-plus-two restart:** start a delayed ten-record authority request, add
   two records, and prove the running request is cancelled or superseded and a
   full twelve-record request is started.
2. **Cache reset:** prove the ordinary replacement marks the authority engine
   for reset and prevents useful ten-record mechanical work from feeding the
   next delta.
3. **Burst churn:** deliver multiple complete roster generations faster than a
   full authority pass and prove repeated cancellation/replacement can delay
   convergence.
4. **Unrelated worldwide change:** change a controller outside the active
   flight's query scope and prove the current path still invalidates the whole
   roster identity.
5. **ATIS-only churn:** change only ATIS descriptive content and prove the
   current authority-controller content digest changes.
6. **All-or-nothing completion:** prove a completion containing still-current
   useful controller facts is rejected as a whole when the newest roster digest
   differs.
7. **Live receipt:** preserve the `GTI8274 KNGU-EDFH` request sequence showing
   a roughly 25.9-second completion followed by repeated full dispatches.

If a claimed red condition cannot be reproduced through production code, this
gate must be amended before implementation continues.

## 15. Required Focused Regression Matrix

### 15.1 Baseline and additions

1. A stable ten-controller baseline verifies exactly ten records.
2. Two controllers arriving during that run do not cancel it.
3. The ten still-current returned records populate the Brain ledger.
4. The follow-up request contains exactly the two additions.
5. Final publication contains all twelve after Brain acceptance.
6. A second unchanged source generation performs zero authority verification.
7. An ATIS-only change performs zero authority verification.
8. Source record reordering performs zero authority verification.

### 15.2 Removals and changes

9. One controller signing off in a complete roster is removed by the Brain
   without a full rebuild.
10. Absence from an incomplete roster does not remove a controller.
11. A frequency change retires the old semantic entry and verifies exactly one
    new record.
12. A facility/role change retires the old semantic entry and verifies exactly
    one new record.
13. A callsign/session replacement cannot inherit the prior controller's fact
    packet accidentally.
14. A stale completion cannot resurrect a removed controller.
15. A stale completion cannot overwrite a changed frequency.

### 15.3 Ownership and static data

16. A scoped vNAS ownership change re-evaluates only affected ledger entries.
17. An unrelated vNAS facility change performs no work for unaffected entries.
18. A canonical static-map identity change performs a staged structural
    rebuild and never mixes map generations.
19. A structural rebuild retains the prior accepted snapshot only when the
    Brain's existing source-health policy explicitly permits it.
20. Source loss produces named pending/unavailable state, never invented
    removals or ownership.

### 15.4 Concurrency and convergence

21. One running request plus one newest Brain desired snapshot is the maximum
    ordinary backlog.
22. One hundred burst roster generations do not create an unbounded queue.
23. Ordinary burst churn produces zero worker cancellations.
24. After churn stops, the Brain converges to the newest roster without
    re-verifying unchanged records.
25. Lifecycle reset cooperatively cancels and leaves zero worker threads.
26. Plugin unload cannot publish a late completion.
27. A worker failure returns explicit per-request failure facts and leaves the
    Brain in a recoverable pending state.

### 15.5 Brain authority and consumers

28. A worker candidate assignment cannot directly light a polygon.
29. A worker rejection cannot directly hide or remove a controller.
30. Only the Brain can admit or reject returned per-controller facts.
31. Only the Brain can decide active, conflict, pending, and unmapped states.
32. Every displayed Center/APP/DEP row traces to a Brain-accepted map and board
    generation.
33. Terminal endpoint relationships remain Brain-owned and unchanged.
34. Center-click selection remains Center-only and tunes COM1 as already
    approved.
35. Standby Assist consumes the final Brain board and cannot observe pending
    worker candidates.
36. PCT/KNGU remains hidden for the canonical unrelated reason throughout
    incremental refresh.
37. A newly online genuinely relevant controller appears after its exact delta
    is Brain-accepted.

### 15.6 Cache equivalence

38. Cold execution and worker mechanical cache-hit execution return byte- or
    field-equivalent fact packets apart from timing/cache diagnostics.
39. Incremental final state equals a clean full rebuild from the same latest
    source tuple.
40. Randomized sequences of add/remove/change/ownership events converge to the
    same map as a clean oracle rebuild.

## 16. Performance and Threading Contract

The closeout must prove improvement rather than infer it from code structure.

Required measurements include:

- initial baseline record count and verification time;
- each delta's added/removed/changed/unchanged counts;
- actual records mechanically processed per request;
- avoided-record count relative to the old full-restart behavior;
- source-observation-to-Brain-convergence latency;
- worker wall and CPU time;
- Brain dispatch, reconciliation, and atomic-publication time;
- flight-loop and UI-thread P50/P95/P99/max cost;
- ordinary and structural cancellation counts;
- cache hit rate, retained entries, and peak retained bytes;
- bounded backlog depth; and
- final full-rebuild parity hash.

The existing authority submission P99 contract remains below `250 us` unless a
separately approved performance gate changes it. Lifecycle cancellation remains
nonblocking and must satisfy the existing contract. Ordinary roster churn must
produce zero active-request cancellations.

For the deterministic ten-plus-two fixture:

```text
records processed = 10 baseline + 2 delta = 12
```

The corrected path may not process 10, cancel, and then process all 12. It may
not claim success merely because the last request eventually completes.

No network, global polygon traversal, large destruction, or unbounded
allocation may run on the simulator or UI thread.

## 17. Required Legacy-Path and Ambiguity Audit

Before closure, every current restart or parallel-authority seam must receive a
final disposition:

| Current seam | Current effect | Required disposition |
|---|---|---|
| `AuthorityRelevanceWorker::StartLatest` active replacement | cancels running ordinary authority work | replace for ordinary refresh; retain cancellation only for typed structural/lifecycle causes |
| `cancelRequested` on every newer mailbox sequence | supersedes useful in-flight work | remove as ordinary roster policy |
| `resetEngineBeforeNext` on ordinary replacement | erases reusable mechanical cache | restrict to typed structural invalidation |
| whole-roster `controllerDigest` completion equality | rejects useful still-current per-record facts | replace with Brain per-record reconciliation |
| ATIS fields in authority semantic content digest | descriptive churn invalidates authority work | remove from authority-semantic identity |
| plugin-owned authority dispatch policy | scheduling resides in host integration code | move the scheduling decision/state to the Brain; plugin remains binder/transport |
| worker-built final relevance/activation snapshot | risks worker-side semantic authority | split into immutable candidate/mechanical fact packets plus explicit Brain decision/publication |
| accepted-snapshot continuity path | preserves last proof during refresh | retain only as a Brain-owned, generation-explicit policy |
| legacy compatibility authority paths | could bypass the new map | remain removed/quarantined with zero publication authority |

The static audit must prove:

- no ordinary roster update calls a path that cancels active authority work;
- no ATIS-only change dirties authority-semantic verification;
- no worker publishes or mutates the accepted live map;
- no plugin-host branch decides delta membership or cache admission;
- no completion is accepted without a Brain reconciliation decision;
- no removed controller can survive through a stale completion;
- no pending addition can reach a board before Brain acceptance;
- no old whole-roster path remains capable of parallel publication; and
- all UI and Standby Assist inputs originate from the final Brain board.

No entry may be closed as “unused” without reference evidence and a regression
proving it cannot affect production authority.

## 18. Rollback Plan

Rollback is a reviewed source and artifact recovery procedure, not a runtime
switch that restores worker-side authority or a parallel decision path.

Before implementation begins:

1. preserve the complete current tracked diff as a full-index binary patch;
2. preserve approved untracked files in a verified archive;
3. record a manifest and SHA-256 hashes;
4. preserve the current regression inventory and green summary;
5. preserve the current Release plugin hash; and
6. create a dedicated output directory for this gate's red, green,
   performance, static-audit, and rollback receipts.

After every major phase, preserve an incremental patch and focused-test
receipt. Recovery must occur in a separate directory or by reviewed affected
file restoration. The active dirty worktree may not be destructively reset.

If an authorized live build fails:

- preserve its diagnostics and exact binary first;
- stop X-Plane before replacing the plugin;
- restore the separately backed-up previous XPL only after Product Owner
  authorization;
- verify the restored binary hash; and
- rerun the minimum startup, controller-board, Center-click, Standby Assist,
  PCT/KNGU, and authority-worker smoke tests.

No rollback flag may reactivate the old cancellation/restart algorithm beside
the new path. Source rollback may restore the prior reviewed build, but one
binary may contain only one production authority-refresh owner.

## 19. Implementation Sequence

After explicit Product Owner approval of this gate:

1. create and verify the rollback checkpoint;
2. preserve the live request sequence and mandatory pre-fix red proofs;
3. add typed per-controller stable keys and authority-semantic signatures;
4. separate transport generation, descriptive ATIS content, authority
   semantics, dynamic ownership, and canonical static-map identities;
5. introduce the Brain-owned desired roster and verified controller ledger;
6. introduce the Brain-owned delta planner and scheduling state machine;
7. change the worker request/output to bounded per-record mechanical facts;
8. stop ordinary `StartLatest` replacement from cancelling running work;
9. implement Brain per-record completion reconciliation;
10. implement Brain-owned removal and changed-record retirement;
11. implement scoped dynamic-ownership invalidation;
12. implement atomic incremental live-map publication;
13. migrate route, endpoint, board, diagnostics, UI, and Standby Assist
    consumers without adding a second interpretation;
14. remove or quarantine every old whole-roster publication and worker-side
    decision seam;
15. run the static ambiguity audit;
16. run focused delta, authority, parity, lifecycle, and performance probes;
17. run the complete environment-independent regression suite;
18. build the Release plugin;
19. review all exclusions, timing distributions, cache bounds, and parity
    hashes;
20. only after separate authorization, back up and install the XPL for
    controlled live proof; and
21. request explicit authorization before commit.

At each intermediate phase, the code must compile and there must remain exactly
one testable production decision path.

## 20. Prohibited Shortcuts

The implementation may not:

- put the incremental policy inside the worker or plugin host;
- allow the worker to activate, hide, remove, rank, or display controllers;
- compare only roster counts;
- use frequency alone as controller identity;
- treat ATIS text as an authority-semantic invalidation;
- ignore removals while optimizing additions;
- infer removal from a partial or stale roster;
- retain a signed-off controller until the next full rebuild;
- publish a stale changed frequency under the prior verified record;
- keep multiple pending generations in an unbounded queue;
- cancel active work for ordinary roster churn;
- reset all caches for one unrelated controller change;
- route-scope the global activation map to make tests faster;
- rebuild the worldwide map because aircraft position or route progress changed;
- accept a whole stale completion without per-record reconciliation;
- hide the previous full-restart path behind a feature flag;
- let a rollback flag restore retired authority logic;
- add hardcoded airport, facility, callsign, or frequency exceptions;
- weaken source freshness, completeness, terminal relationship, or conflict
  rules; or
- declare success from code review without measured red/green, parity,
  performance, and live evidence.

## 21. Offline Exit Criteria

All of the following are required before live installation is requested:

- rollback checkpoint and hashes verified;
- mandatory pre-fix red proofs preserved;
- focused regression matrix green;
- ten-plus-two fixture processes exactly twelve total records;
- ordinary roster churn produces zero active-request cancellations;
- additions, removals, changes, and ownership deltas converge correctly;
- incomplete-feed absence cannot remove a controller;
- cold/full and incremental final-state parity proven;
- randomized delta-sequence oracle parity green;
- worker cache equivalence and memory bounds green;
- Brain-only authority static audit green;
- no parallel whole-roster or legacy publication path remains;
- existing canonical-map, terminal-relationship, Center-click, and Standby
  Assist focused suites remain green;
- complete environment-independent suite green with every exclusion named;
- Release build green;
- simulator/UI-thread performance contracts green; and
- closeout artifacts reviewed before any XPL replacement.

## 22. Controlled Live Exit Criteria

After separate live-install authorization:

- a naturally changing VATSIM roster does not cancel ordinary active authority
  work;
- diagnostics show a baseline completion followed by bounded deltas rather
  than repeated full dispatches;
- newly online controllers are not forgotten and are admitted only after Brain
  verification;
- signed-off controllers are removed from a complete roster without stale
  resurrection;
- unchanged controllers retain stable accepted facts across feed generations;
- PCT remains lit in the global map only according to its actual active
  assignment and remains absent from the KNGU board when unrelated;
- a genuinely route- or endpoint-relevant controller appears with the correct
  canonical proof;
- final board, UI, and Standby Assist generations match the Brain publication;
- no simulator stutter, unbounded memory growth, or worker backlog appears;
- source-observation-to-convergence latency materially improves over the
  preserved restart baseline; and
- the Product Owner performs normal-use testing and records PASS or a named
  failure.

## 23. Required Closeout Artifacts

The gate closeout must preserve:

- approved contract and rollback checkpoint;
- live `GTI8274 KNGU-EDFH` pre-fix diagnostic excerpt;
- every mandatory red proof;
- focused green outputs;
- ten-plus-two and burst-churn detailed receipts;
- add/remove/change/ownership reconciliation receipts;
- cold/full versus incremental parity hashes;
- randomized oracle test output;
- worker cache equivalence and memory report;
- static ambiguity audit and legacy disposition table;
- complete regression summary and exclusions;
- Release build output;
- performance distribution and convergence report;
- exact candidate, backup, and installed XPL hashes if live proof is
  authorized;
- controlled live diagnostics; and
- Product Owner approval to commit.

## 24. Definition of Done

This gate is complete only when:

1. ordinary roster changes never cancel useful in-flight authority work;
2. an initial baseline is processed completely;
3. subsequent work is limited to semantic additions, removals, changes, and
   exactly affected ownership records;
4. unchanged records are reused from a Brain-owned verified ledger;
5. ATIS, ordering, timestamp, and transport-only churn do not trigger
   authority verification;
6. source-proven removals are Brain decisions and stale completions cannot
   resurrect them;
7. every worker request is Brain-issued and every completion is
   Brain-reconciled;
8. the worker returns complete immutable facts and cannot hide, alter, remove,
   activate, or display controllers;
9. one immutable Brain publication is the sole live authority map;
10. every downstream controller row traces to that publication and a final
    Brain decision;
11. no legacy whole-roster restart or parallel publication authority remains;
12. incremental output equals a clean full rebuild for the same latest source
    tuple;
13. performance improvement is measured under baseline, delta, and burst
    workloads;
14. all offline, build, static, lifecycle, and performance gates pass;
15. separately authorized live proof passes; and
16. the Product Owner reviews the closeout and explicitly authorizes commit.

Until every item is proven, this gate remains open.

## 25. Approval Record

**Product Owner decision:** Approved 2026-10-26  
**Implementation authorized:** Yes  
**Live installation authorized:** Yes, 2026-10-26 after X-Plane shutdown  
**Commit authorized:** No

## 26. Offline Implementation Receipt

The offline implementation candidate completed on 2026-10-26. Ordinary roster
updates now preserve the running authority request and retain only one newest
pending request. The worker's cache performs mechanical reuse only; it does not
publish authority. The duplicated worker conflict policy was removed.
`BuildBrainOwnedLiveAuthorityMapSnapshot` is now the sole active/conflict
decision, route relevance queries only that Brain-approved set, and APP/DEP
endpoint admission requires the `brainDecisionsApplied` guard.

The deterministic incremental audit passed with counts shown as
`verified/reused/removed`:

- ten-controller baseline: `10/0/0`;
- ten-plus-two follow-up: `2/10/0`;
- reordered source: `0/12/0`;
- descriptive ATIS text change: `0/12/0`;
- incomplete eleven-controller observation: `0/11/0`;
- restored complete twelve-controller roster: `0/12/0`;
- one proven sign-off: `0/11/1`;
- one frequency change: `1/10/0`;
- one facility change: `1/10/0`; and
- unchanged complete generation: `0/11/0`.

The 10+2 fixture processed exactly twelve controller records. The running
baseline completed before the newest follow-up, ordinary churn caused zero
active-request cancellations, maximum pending depth remained one, and the one
reported cancellation was the explicit lifecycle-cancellation fixture.
Twenty deterministic randomized add/remove/change/reorder sequences matched a
clean rebuild oracle `20/20`.

Focused global activation, conflict fail-closed, vNAS ownership invalidation,
KNGU/Potomac rejection, LEPA-EDDB and WSSS-VHHH battle tests, Center-only
selection, next-tier Standby Assist, and UNICOM exhaustion all passed. The
complete suite passed `927/927`. The installed xPilot 4.0.0 beta 9 plugin is
now a positive generation-boundary fixture: it is canonicalized and hashed,
then proven to fail closed against the separate legacy xPilot 3 private-dataref
fingerprint gate. The exact legacy fingerprint remains covered synthetically.

Performance Gate A and both calm-state probes passed. Submission P99 was
`4 us`, worst-case package-and-submit P99 was `3 us`, harvest P99 was `2 us`,
and the 5,000-controller bound remained green. Static audit found no ordinary
`StartLatest` cancellation/reset call, no conflict-state writer outside the
Brain, and no terminal endpoint path without the Brain-decision guard.

The Release candidate is:

`outputs/brain_owned_incremental_live_authority_refresh_01/candidate/XVatsim.xpl`

Its size is `3,193,344` bytes and SHA-256 is
`FC63F81753D0C6D657625E1AC334BA012FE283912B4B10DFDF45AD8CB4785D87`.
At offline closeout, the X-Plane-installed binary was deliberately not replaced
while X-Plane was running. After the Product Owner reported shutdown and
authorized installation, that prior binary was preserved at
`outputs/brain_owned_incremental_live_authority_refresh_01/live_install_backup/XVatsim.before_incremental_refresh.2DF8260140A0176D.xpl`
with SHA-256
`2DF8260140A0176D808646F155532EBE1A0B5129DA1E7553BEA626DD677925DA`.
The installed XPL now matches the candidate at SHA-256
`FC63F81753D0C6D657625E1AC334BA012FE283912B4B10DFDF45AD8CB4785D87`.

No commit was made. The gate remains open for controlled live proof and Product
Owner commit approval.
