# Flight-Loop Performance and Diagnostic Record Integrity Contract Gate

Date proposed: 2026-10-28

Gate: `FLIGHT-LOOP-PERFORMANCE-DIAGNOSTIC-INTEGRITY-01`

Status: **APPROVED 2026-10-28; IMPLEMENTED; OFFLINE PROOF PASSED; LIVE
VALIDATION PENDING**

The Product Owner approved implementation and installation for live proof on
2026-10-28. This status does not authorize a commit or a change to controller
authority behavior.

## 1. Purpose

Correct two defects proven during the live FFT2791 KMCO-to-KORD flight:

1. synchronous work and possible worker-thread joining can stall X-Plane's
   flight-loop thread; and
2. several diagnostic serializers exceed the existing 4,096-byte physical-line
   contract, while the ordinary diagnostic queue can lose records through
   avoidable mutex contention.

The correction must improve runtime health without changing the currently
correct radio-scoped route-authority pipeline.

## 2. Live evidence that opens this gate

The 2026-10-28 live session remained functionally correct:

- route authority advanced `KZMA -> KZJX -> KZTL -> KZID -> KZAU`;
- JAX Center advanced from next to current and was removed after exit;
- unrelated Center and terminal candidates were rejected;
- all 24 radio-board diffs matched their complete snapshots;
- 40 authority completions were published and two stale completions were
  correctly rejected;
- route-worker starts and completions were `641 / 641`, with no failure or
  cancellation;
- xPilot 4 bridge corruption, loss, sequence-gap, and incomplete-snapshot
  failures were all zero;
- METAR completed `172 / 172`; and
- shutdown was clean.

That functional result is the authority baseline this gate must preserve.

The same session failed the performance and record-format contracts:

| Evidence | Observed |
| --- | ---: |
| Flight-loop callbacks | 44,545 |
| One-minute windows | 190 |
| Callbacks over 10 ms | 7 |
| Callbacks over 40 ms | 6 |
| Maximum callback | 196.177 ms |
| Radio-range work in the 196.177 ms callback | 109.624 ms |
| Unclassified time in that callback | 86.307 ms |
| Other established-flight outliers | 49.851, 78.323, and 149.436 ms |
| Session diagnostic bytes | approximately 4.62 MiB |
| Physical lines over 4,096 bytes | 350 |
| Largest physical line | 8,347 bytes |
| Routine records dropped by queue contention | 233 |
| Critical/timing/route records dropped | 0 |

Oversized records consisted of 346 ordinary summaries, two authority-proof
records, one slow-refresh record, and one accessory-performance record.

## 3. Confirmed structural defects

### 3.1 Transceiver source adoption is still synchronous

`TransceiverResolver` downloads and parses the global transceiver document on a
background thread, but `HarvestPendingFetch()` performs the following work on
the caller:

- joins the fetch thread;
- copies the complete parsed transceiver vector;
- rebuilds the complete callsign index; and
- clears the pending storage.

The live caller is `BuildEngineer3RadioSnapshot()` inside
`RefreshOverlayFromBrainEngineer3()`. Therefore source adoption and index
construction still occur on X-Plane's flight-loop thread. The observed
109.624 ms radio-range interval is consistent with this boundary, although the
current telemetry cannot yet divide that interval into source adoption,
controller hashing, evidence construction, compatibility projection, and
Brain commit.

### 3.2 Flight-scoped reset owns a global-source thread

`ResetSessionRuntimeCaches()` and the xPilot disconnect path call
`gTransceiverResolver.Reset()`. That reset can join an in-progress network
fetch and clears a worldwide source cache that is not flight-specific. These
paths can execute while servicing the flight loop.

A flight boundary may invalidate the Brain-owned radio board and every
flight-scoped request identity. It must not wait for or destroy a plugin-lifetime
worldwide source publication.

### 3.3 The refresh measurement has gaps

The current refresh frame times many major operations, but it calculates one
remaining `untracked` bucket. Among the code presently outside a complete phase
ledger are update-check harvest/request, aircraft and session boundary work,
overlay-update snapshot construction, sampled-fact commit glue, vNAS scoping,
endpoint polygon refresh, workflow and radio-phase orchestration, ATIS and
bound async-fact servicing, and portions of publication and diagnostic
scheduling.

Consequently the 49-149 ms wall outliers cannot yet be honestly classified as
plugin CPU, a wait/lock, or operating-system preemption.

### 3.4 The writer does not enforce its physical-line contract

`AsyncDiagnosticsWriter::Process()` writes the formatter's entire string as one
physical line. It has no final 4,096-byte guard. The summary serializer embeds
up to 16 verbose job reports, and the accessory serializer emits its complete
aggregate in one record.

The ordinary and critical record queues also share a mutex and use
`try_lock()`. The consumer's normal use of that mutex can make the single
flight-loop producer discard a record even when queue capacity is available.
The 233 live routine contention drops are therefore an implementation defect,
not genuine queue saturation.

## 4. Authority freeze

The authority architecture in
`RADIO_SCOPED_ROUTE_AUTHORITY_CONSOLIDATION_CONTRACT_GATE.md` remains binding.
This gate may change where and when mechanical work runs. It may not change the
meaning of any fact or decision.

The following are frozen:

- radio-board source eligibility and completeness rules;
- controller and transceiver matching;
- receivable-range mathematics and thresholds;
- candidate callsign, frequency, facility, station, and source evidence;
- radio-board diff semantics;
- static polygon identity and ownership facts;
- current, next, endpoint, phase, and route-relevance rules;
- vNAS and other source precedence, freshness, conflict, and rejection rules;
- candidate votes and final Brain reasons;
- UI row identity, color, ordering, and removal;
- Standby Assist candidate hierarchy and COM behavior;
- Center-frequency click behavior; and
- xPilot 3.2 and xPilot 4.0.0 beta 9 compatibility behavior.

No worker may accept, reject, filter, promote, remove, color, or display a
controller. A worker may only compile a complete immutable mechanical fact.
The Brain remains the sole authority that accepts a worker completion, commits
the radio board, computes authority, and publishes the final display.

If implementation requires changing an existing controller decision branch,
score, threshold, evidence meaning, or final publication rule, work stops and
this gate returns to the Product Owner for a separate amendment.

## 5. Required runtime design

### 5.1 Separate plugin-lifetime source state from flight state

Refactor the current mixed resolver into two explicit responsibilities.

`TransceiverSourceSnapshot` is a plugin-lifetime immutable publication that
contains:

- the parsed transceiver records;
- the callsign index built from those records;
- parser hygiene counters;
- source generation and content digest;
- successful-fetch and source-age facts; and
- fresh, holdover, failed, or unavailable source state.

Download, parse, normalization, and index construction all occur away from the
flight loop. The completed product is published through an immutable shared
snapshot. Flight-loop harvest is an O(1) pointer/generation exchange. It may
not copy the global vector, rebuild an index, join a thread, wait on a future,
or acquire a blocking worker mutex.

A flight reset clears only the flight-scoped radio-board request and accepted
result. It does not clear or join the plugin-lifetime source worker. Source
freshness and holdover rules remain exactly as they are today.

The source worker is started before flight-loop registration and stopped only
after flight-loop unregistration. Thread creation and joining are forbidden in
`FlightLoopCallback()` and every function reachable from it.

### 5.2 Compile the radio-board mechanical fact on one persistent worker

Add one persistent, below-normal-priority `RadioBoardFactWorker`. It consumes
an immutable request containing:

- lifecycle epoch and request sequence;
- aircraft position, altitude, and validity snapshot;
- complete controller snapshot, generation, completeness, and content digest;
- immutable transceiver-source snapshot and source identity; and
- every current tuning value required by the existing resolver.

The worker executes the existing controller hash, receivable-station evidence,
frequency resolution, compatibility projection, ordering, and
`BuildBrainRadioRangeWorkerOutput()` semantics. It publishes one complete
immutable result with exact input identities and measured substage timings.

The worker queue contract is:

- one running request;
- at most one replaceable pending request;
- no cancellation or restart of useful in-flight computation;
- a newer request replaces only the pending request;
- no unbounded backlog; and
- no worker result may directly mutate Brain, UI, Assist, or plugin runtime
  state.

The old synchronous live `TransceiverResolver::Resolve()` call path is removed,
not hidden behind a setting. Any resolver helpers retained solely for harness
fixtures must be moved to test support or protected by the existing harness
compile boundary. There must be exactly one production radio-board compiler.

### 5.3 Brain-owned completion acceptance

The Brain receives the worker result and the current acceptance facts. It
accepts a result only when:

- lifecycle epoch matches;
- controller generation and content digest match the current complete
  controller snapshot;
- transceiver source generation and digest match;
- the worker reports a complete result;
- aircraft displacement is below the existing 0.05 NM resolve threshold;
- altitude displacement is below the existing 500 ft resolve threshold; and
- completion age is within the existing one-second resolve cadence.

These checks govern whether an asynchronously produced fact is current. They
do not change controller eligibility.

An out-of-date completion is retained only as diagnostic evidence and is never
published as current. The running work is not cancelled. The latest pending
request is processed next. Until a matching completion exists, the Brain may
retain its last complete accepted board under the existing complete-snapshot
continuity rules; it may not invent a new candidate or accept a removal from an
incomplete snapshot.

The existing `CommitBrainOwnedRadioBoardRefresh()` remains the semantic commit
boundary after acceptance. If a small Brain adapter is required for completion
acceptance, its only output is `accept`, `reject`, or `pending` plus an auditable
reason. It may not contain candidate-policy logic.

### 5.4 Complete flight-loop phase ledger

Replace the single residual `untracked` bucket with a fixed phase ledger that
owns the entire refresh interval. At minimum it distinguishes:

- update service;
- aircraft sample and boundary;
- xPilot poll, identity, and session boundary;
- VATSIM, radio, PDC, and manual input service;
- refresh-gate construction and decision;
- controller, flight-plan, and network-plan snapshots;
- flight-context and CTAF;
- route worker submit/harvest;
- radio-board worker submit/harvest and Brain acceptance;
- vNAS scope construction;
- authority worker submit/harvest;
- endpoint, workflow, radio-phase, relevance, and publisher orchestration;
- ATIS, METAR, and other bound async-fact service;
- Assist and publication commit;
- wake and overlay construction/update; and
- diagnostic submission and shell overhead.

The ledger uses fixed storage and stable enum tokens. It does not allocate or
format strings on every callback. Existing detailed timers may remain, but the
phase ledger is the reconciled ownership record.

Capture current-thread CPU time at callback entry and exit using the existing
Windows `GetThreadTimes` pattern. Every outlier reports wall time, thread CPU
time, every non-zero phase, and reconciliation error. This separates real
plugin work from time when Windows did not schedule the simulator thread.

No performance claim may label a delay as operating-system preemption unless
the thread-CPU evidence supports that conclusion.

## 6. Required diagnostic design

### 6.1 Readable structured records

Normal serializers must produce naturally bounded records rather than relying
on truncation.

The current summary family becomes:

- one `refresh-summary` core record, target maximum 2,048 bytes;
- zero or more `refresh-summary-job` records, one bounded job per line;
- one bounded `refresh-authority-proof` detail record when required; and
- stable `summaryId`, tick, and sequence keys joining the records.

The accessory aggregate becomes:

- one `accessory-performance-core` record;
- one record per category summary;
- one record per action/stage summary;
- bounded raster-reason records;
- one last-action record; and
- one record per captured violation.

Every part carries the same epoch, publication revision, and aggregate digest.
This preserves every field while making the log readable and queryable.

Candidate receipts retain their existing semantic splitter. No decision fact
or score may be truncated to satisfy a formatting limit.

### 6.2 Writer-owned hard physical boundary

The writer is the final enforcement boundary. Every physical line, including
the `tick=` prefix and newline, must be no more than 4,096 UTF-8 bytes.

If any unexpected logical record still exceeds that boundary, the writer emits
a bounded oversize-record header and numbered payload parts containing:

- logical record sequence;
- original event token when recoverable;
- exact original byte count;
- whole-record digest;
- `part` and `partCount`; and
- UTF-8-safe Base64 payload bytes.

This fallback must reconstruct the original logical payload byte-for-byte and
verify its digest. It is a last defense, not the normal format for summaries or
accessory performance. Silent truncation and silent drop are forbidden.

The writer records separate logical-record and physical-line counters so a
split record is not mistaken for duplicate logical events.

### 6.3 Remove avoidable producer contention

Replace the mutex-contended routine and critical record queues with bounded,
preallocated single-producer/single-consumer lanes, matching the already
established timing and route-evidence design. Production enqueue calls are
owned by the X-Plane plugin thread; the writer is the sole consumer.

The producer operation must be non-blocking and may not format deferred text.
Queue-full behavior remains explicit and counted. Consumer activity must no
longer cause `droppedRoutineContention` or `droppedCriticalContention`.

PDC mailbox synchronization remains separate and may not hold a diagnostic
record lane hostage.

## 7. No-hidden-legacy rule

This gate is a replacement, not an overlay patch.

Implementation must prove all of the following:

- no production call reaches the old synchronous radio resolver;
- no session-boundary path joins or clears the global source worker;
- no second radio-board cache can publish to the Brain;
- no runtime switch can restore the synchronous path;
- no formatter can bypass the writer's 4,096-byte boundary;
- no old monolithic summary or accessory serializer remains callable in a
  production build; and
- no performance optimization changes or bypasses a Brain decision.

Source searches and link/build boundaries are part of the proof. Merely leaving
the old path unused by convention is not sufficient.

## 8. Implementation sequence

Implementation after approval must occur in this order:

1. capture the rollback artifacts in Section 11;
2. add the fixed phase ledger and callback CPU measurement;
3. add focused tests that reproduce the current synchronous source-adoption,
   session-reset, oversize-line, and queue-contention failures;
4. split global source publication from flight state;
5. add the persistent radio-board fact worker and Brain completion acceptance;
6. remove the synchronous live path and prove semantic parity;
7. replace monolithic diagnostic formats and add the writer hard boundary;
8. replace the contested diagnostic record lanes;
9. run all focused, repository-wide, stress, and binary proofs; and
10. install one complete candidate only after every offline gate passes.

No partially migrated binary may be installed for live testing.

## 9. Required automated proof

### 9.1 Authority identity

For identical captured inputs, before-and-after runs must produce semantically
identical ordered values for:

- transceiver source evidence;
- every controller and station evidence record;
- radio-board candidates and status;
- radio-board additions, changes, and removals;
- authority completion receipts and reasons;
- final display rows, colors, and order; and
- Standby Assist recommendations and COM commands.

Replay the complete FFT2791 event trace, including all 24 board diffs, JAX
Center progression, unrelated-controller rejections, two stale authority
completion rejections, and final KZAU state. A digest mismatch fails the gate.

Run the complete regression harness and every focused xPilot 3.2/xPilot 4.0.0
beta 9, authority, polygon, UI click, Assist, route, PDC, METAR, ATIS, and
lifecycle probe. The current full scenario count must not decrease.

### 9.2 Worker and lifecycle stress

Using production-shaped fixtures:

- prepare at least 50,000 transceivers and 1,000 controllers;
- prove download/parse/index work executes off the simulator thread;
- prove harvest is an immutable pointer exchange;
- submit 10,000 changing aircraft/controller requests;
- prove one running plus at most one pending request;
- prove no in-flight cancellation/restart loop;
- prove stale lifecycle, controller, source, age, displacement, and altitude
  completions are rejected by the Brain;
- prove the exact current completion is accepted once;
- reset or disconnect while source work is active and prove the callback does
  not join, wait, or clear the source publication; and
- prove worker start/stop counts, request leases, and pending depth return to
  zero at shutdown.

### 9.3 Flight-loop performance

The controlled established-flight proof must satisfy:

- callback wall-time P99 at or below 2.5 ms;
- callback thread-CPU maximum at or below 5 ms after bootstrap;
- no plugin-CPU, lock, wait, thread-lifecycle, I/O, parse, global-index, or
  radio-board compile operation causes a callback over 10 ms;
- radio-board submit, harvest, validation, and commit on the simulator thread
  each remain below 500 microseconds;
- every callback over 3 ms has a complete phase/CPU outlier record;
- phase sum plus measured shell overhead reconciles to total wall time within
  250 microseconds or 5 percent, whichever is larger; and
- any wall outlier attributed to external scheduling has thread-CPU evidence
  proving that the plugin did not consume the missing interval.

Run calm, controller-churn, source-publication, lifecycle-reset, accessory-use,
diagnostic-saturation, and clean-shutdown profiles. Averages alone cannot pass
this gate.

### 9.4 Diagnostic integrity

Test records at 4,095, 4,096, 4,097, 8,347, and at least 65,536 bytes,
including multi-byte UTF-8, quotes, backslashes, and embedded event-like text.
Prove:

- every physical line is at most 4,096 UTF-8 bytes including prefix/newline;
- normal summary and accessory records use the readable structured schemas;
- fallback parts reconstruct the exact original bytes and digest;
- no field from the current summary, authority-proof, or accessory aggregate is
  lost;
- logical and physical counters reconcile;
- `droppedRoutineContention` and `droppedCriticalContention` remain zero;
- critical, timing, route-evidence, and bounded routine records have zero loss
  in the contracted replay and saturation profiles;
- no formatting or storage failure occurs; and
- the existing 8 MiB per-date and three-date retention limits still hold.

## 10. Required live proof

After offline proof and installation approval, perform at least one complete
online flight with the production diagnostic build.

The live acceptance report must include:

- installed XPL SHA-256;
- xPilot version in use;
- route and controller progression;
- complete radio-board/authority semantic parity report;
- callback wall and CPU percentiles and maxima;
- every callback over 3 ms with phase classification;
- radio worker request/completion/stale/accepted/queue-depth counters;
- maximum physical diagnostic line size;
- logical/physical record and split counters;
- every writer drop/failure counter; and
- clean worker and writer shutdown evidence.

One symptom-free flight is necessary but not sufficient. The evidence above
must pass.

## 11. Rollback plan

Before modifying production code:

1. preserve the currently installed candidate XPL byte-for-byte as the primary
   rollback binary and record its SHA-256
   `5CF635446388B4BD0A676C8B5163E4FDA8262B0726E4728E51E2F5100787C5EB`;
2. retain the existing older rollback XPL with SHA-256
   `FC63F81753D0C6D657625E1AC334BA012FE283912B4B10DFDF45AD8CB4785D87`;
3. copy every source file this gate will modify into a timestamped rollback
   tree with a manifest and SHA-256 hashes;
4. capture the full pre-change working-tree diff and the untracked files in the
   declared implementation scope; and
5. record the current offline proof outputs and live FFT2791 audit as the
   comparison baseline.

Rollback restores the primary current candidate and the exact scoped source
files. It does not use `git reset`, discard unrelated working-tree changes, or
restore an old authority implementation.

There is no runtime rollback flag and no dormant legacy path. If the candidate
fails, restore the complete prior binary/source boundary, diagnose offline, and
open a gate amendment before another installation.

## 12. Expected implementation scope after approval

Expected production files or modules:

- `modules/transceiver_resolver/` for immutable source publication and pure
  mechanical compilation boundaries;
- one focused persistent radio-board fact worker under `modules/runtime_workers/`
  or the transceiver module;
- `plugin/src/XVatsimPlugin.cpp` for non-blocking orchestration, lifecycle, and
  phase telemetry;
- the smallest Brain-owned worker-completion acceptance adapter necessary to
  preserve final authority ownership;
- `modules/runtime_workers/AsyncDiagnosticsWriter.*` for record lanes and the
  physical-line boundary; and
- focused harness probes, scenarios, and gate evidence.

No controller-policy rewrite, route-policy rewrite, polygon-policy rewrite,
UI behavior rewrite, or Assist behavior rewrite is authorized.

## 13. Approval statement

Approval authorizes implementation and proof of this exact design. It does not
authorize a commit or final release. The gate refreezes after implementation;
the Product Owner's live validation is still required before commit.

## 14. Implementation and offline proof receipt

Implementation completed on 2026-10-28 with the authority freeze intact.

- Radio-board fetch, parse, global index construction, range compilation, and
  Brain input construction now execute on one persistent below-normal worker.
- The worker owns one running request and one replaceable pending request. It
  cannot publish to Brain, UI, Assist, or radio state.
- Brain completion acceptance now checks lifecycle, controller generation and
  digest, transceiver-source generation and digest, age, aircraft movement,
  and altitude movement before the existing Brain commit boundary.
- Flight/session invalidation clears only flight-scoped request identity and
  accepted board continuity. It does not reset or join the source resolver.
- The update checker was also converted to a persistent pre-registered worker
  after the no-thread-lifecycle source audit found its former callback-reachable
  create/join path.
- The callback now records wall time, current-thread CPU time, a fixed phase
  ledger, and reconciliation error for outlier classification.
- Summary, authority-proof, and accessory diagnostics use bounded structured
  records. The former monolithic accessory event is removed.
- Routine and critical diagnostics use fixed SPSC lanes. Consumer activity can
  no longer create a mutex-contention drop.
- The writer is the final 4,096-byte physical-line boundary and losslessly
  splits unexpected oversized logical records into digest-verified Base64
  parts. Files are opened in binary mode so Windows newline translation cannot
  exceed the physical-byte contract.

Offline proof passed:

- Release plugin and regression harness compiled successfully.
- Complete saved-scenario replay: `927 / 927` passed; scenario count did not
  decrease.
- Focused performance, calm, lifecycle, authority, endpoint polygon, COM RX,
  Center/Assist, PDC, and xPilot 4 bridge probes passed.
- Diagnostic records of 4,095, 4,096, 4,097, 8,347, and 65,536 bytes,
  including UTF-8, quotes, backslashes, newlines, and embedded event-like text,
  reconstructed byte-for-byte with matching digest; every physical line was
  at most 4,096 bytes.
- The 50,000-transceiver / 1,000-controller source-and-index fixture completed
  off the caller thread with exact source identity.
- The 10,000-request radio-worker stress proved bounded one-pending depth,
  replacement accounting, complete harvest accounting, and clean shutdown.
- Exact completions were accepted and stale lifecycle, controller, source,
  age, displacement, and altitude identities were rejected with stable Brain
  reasons.
- Production-source searches found no plugin-owned synchronous resolver, no
  old monolithic accessory serializer, and no runtime rollback switch.

Candidate XPL before installation:

- path: `build/dist/XVatsim/win_x64/XVatsim.xpl`
- bytes: `3,251,712`
- SHA-256:
  `5A7224FC6870B2046F96F8E9D9CB26E25BF93CE3868C3385FB19CF39BB1E602E`

Installation verification:

- X-Plane process check: not running;
- installed path:
  `C:/X-Plane 12/Resources/plugins/XVatsim/win_x64/XVatsim.xpl`;
- installed bytes: `3,251,712`;
- installed SHA-256 matched the candidate exactly:
  `5A7224FC6870B2046F96F8E9D9CB26E25BF93CE3868C3385FB19CF39BB1E602E`;
  and
- rollback SHA-256 was reverified before installation:
  `5CF635446388B4BD0A676C8B5163E4FDA8262B0726E4728E51E2F5100787C5EB`.

Live validation remains mandatory. The Product Owner's next long flight must
provide the Section 10 callback, worker, diagnostic, authority-parity, and
clean-shutdown evidence before this gate may be committed.
