# Standby Assist Regional Sequence Contract Gate

**Gate ID:** `STANDBY-ASSIST-REGIONAL-SEQUENCE-01`

**Status:** Implemented, regression proven, live verified, and approved for
commit by Product Owner on 2026-10-22

**Prepared:** 2026-10-20

**Approved:** 2026-10-20

**Pre-code rollback branch:**
`rollback/standby-assist-regional-sequence-01-pre-code`

**Repository baseline:** `4e77c2698027d7a863a18d527de4ebcd7f87a6ad`

**Active engineering slice:** Add the approved Arrival CTAF/UNICOM sequence and
the Enroute UNICOM fallback to the Brain-owned Standby Assist planner

**Live verification:** Not authorized by this gate

## 1. Objective

Standby Assist currently advances through eligible controller rows and treats
CTAF or `NO CTAF / UNICOM 122.800` as a separate advisory that can be promoted
only after every controller target has been exhausted and a second feature gate
is enabled. Enroute has no independent UNICOM fallback target.

That behavior does not match the approved operational sequence.

This gate changes Standby Assist so the Brain uses these three sequences:

| Flight stage and airport type | Approved COM1 standby sequence |
| --- | --- |
| Arrival at an airport with a resolved CTAF | Center → Approach/Departure → Tower → CTAF → Ground |
| Arrival at an airport successfully resolved with no CTAF | Center → Approach/Departure → UNICOM `122.800` → Tower → Ground |
| Enroute | Eligible route controllers → UNICOM `122.800` only when no eligible Enroute controller is online |

For Arrival, CTAF or UNICOM is part of the sequence rather than a fallback
that waits behind every controller role. For Enroute, the UNICOM fallback is
universal and does not depend on the departure or arrival airport's CTAF
status.

This slice changes Standby Assist selection only. It does not create a new
radio writer, network lookup, worker decision, controller display rule, or
xPilot bridge.

## 2. Governing Bible

The Brain remains the only decision maker.

```text
Existing workers report controller, airport-radio, workflow, and radio facts
    -> Brain builds the stage-specific Standby Assist sequence
    -> Brain selects one COM1 standby target or selects none
    -> Existing radio writer attempts exactly the Brain-approved write
    -> Writer returns success or failure facts to the Brain
```

Workers and the plugin shell may not:

- decide that a controller is online or offline;
- decide whether an airport is CTAF or non-CTAF;
- reorder controller, CTAF, UNICOM, Tower, or Ground priority;
- manufacture a fallback because a lookup is pending, failed, stale, or
  unknown;
- replace the Brain's selected frequency;
- write a frequency that the Brain did not approve; or
- suppress, alter, or override a Brain decision.

The existing radio-state writer remains an actuator. It receives one target
frequency and reports whether the write succeeded. It has no authority to
choose the target.

The following documents remain governing unless this gate is more specific:

- `docs/BRAIN_OWNED_RUNTIME_CONTRACT.md`
- `docs/RADIO_BOARD_RUNTIME_CONTRACT.md`
- `docs/STANDBY_ASSIST_DIRECT_CTAF_GATE.md`
- `docs/V2_EXECUTION_PROTOCOL.md`

This gate supersedes the older direct-CTAF gate only for the Arrival and
Enroute behavior stated here. Departure behavior is unchanged.

## 3. Terms

### 3.1 Eligible online controller

An eligible online controller is a controller that has already been accepted
by the Brain and appears in the Brain-owned final display input for the active
stage with:

- an eligible controller role;
- a usable, non-guard frequency;
- no Brain-owned offline state; and
- the existing route, endpoint, phase, and display acceptance.

Raw VATSIM presence, a worker flag, a callsign suffix, or an unapproved module
row is not sufficient.

### 3.2 CTAF airport

For this gate, an Arrival airport is a CTAF airport only when the existing
airport-radio evidence has successfully resolved a non-empty CTAF frequency
and the Brain-owned CTAF advisory/display decision accepts it.

### 3.3 Non-CTAF airport

For this gate, an Arrival airport is a non-CTAF airport only when the existing
airport-radio evidence has successfully resolved that no CTAF is available and
the Brain-owned advisory decision accepts the existing UNICOM fallback at
`122.800`.

A pending, failed, stale, corrupt, empty, or otherwise unknown lookup is not a
non-CTAF result. Unknown evidence may not be converted into UNICOM.

### 3.4 Every controller is offline in Enroute

The Enroute fallback condition is true only when the Brain-owned Enroute board
contains zero eligible online controller targets after all existing route and
frequency decisions have completed.

The fallback condition does not depend on a worker's count, a raw feed count,
or the airport CTAF lookup.

## 4. Arrival Selection Contract

### 4.1 CTAF Arrival sequence

When the Arrival airport has an accepted CTAF frequency, Standby Assist must
construct this ordered sequence:

1. Center rows in their existing Brain-owned relation order.
2. Approach and Departure rows in their existing intra-role order.
3. Tower rows in their existing intra-role order.
4. The accepted Arrival CTAF frequency.
5. Ground rows in their existing intra-role order.

Existing Delivery behavior remains after Ground and is outside the requested
five-role sequence. ATIS remains ineligible for Standby Assist.

### 4.2 Non-CTAF Arrival sequence

When the Arrival airport is successfully resolved with no CTAF, Standby Assist
must construct this ordered sequence:

1. Center rows in their existing Brain-owned relation order.
2. Approach and Departure rows in their existing intra-role order.
3. UNICOM `122.800`.
4. Tower rows in their existing intra-role order.
5. Ground rows in their existing intra-role order.

Existing Delivery behavior remains after Ground. ATIS remains ineligible.

This means that when Center and Approach/Departure are absent, UNICOM
`122.800` is selected before an online Tower or Ground row. Tower and Ground
remain accepted online-controller facts; the Brain changes only when they
occur in the Standby Assist sequence.

### 4.3 Sequence advancement

The existing tuned-frequency progression remains in force:

- Standby Assist selects the first eligible item after the currently tuned
  item in the approved sequence.
- If earlier controller roles are absent, the Brain advances directly to the
  next available item.
- Once the pilot tunes the selected CTAF or UNICOM frequency, the Brain may
  advance to the next eligible Tower or Ground item according to the sequence.
- Multiple Center, Approach/Departure, Tower, or Ground rows retain their
  current deterministic order.

The overlay's visual row order is not changed by this gate. The Brain may use a
separate immutable Assist sequence while preserving the accepted display
snapshot.

## 5. Enroute Selection Contract

1. Eligible Enroute controller targets retain their existing Brain-owned
   relation and route order.
2. If at least one eligible Enroute controller target exists, the normal
   controller selection wins and UNICOM is not selected as the fallback.
3. If zero eligible Enroute controller targets exist, the Brain creates one
   explicit Enroute UNICOM advisory target at `122.800`.
4. The Enroute target is independent of the departure and arrival CTAF lookup.
   The Brain may not select a departure CTAF, arrival CTAF, or airport-specific
   fallback as the Enroute target.
5. The Enroute UNICOM decision does not require a synthetic controller or
   controller display row.
6. This gate does not add a visible Enroute UNICOM row or an Enroute standby
   marker. It adds the COM1 standby target and its decision receipt.
7. When an eligible Enroute controller later appears, normal controller
   priority resumes on the next valid Brain evaluation.

## 6. Master Switch and Existing Direct-CTAF Gate

For the Arrival and Enroute behavior in this gate:

- `standby_assist=true` is the user authorization and master enable;
- `standby_assist=false` prohibits every write;
- the separate `direct_ctaf_standby_assist` compatibility setting may not
  block the approved Arrival CTAF, Arrival UNICOM, or Enroute UNICOM sequence;
  and
- existing parsing and persistence of that compatibility setting may remain
  so older settings files do not require migration.

Departure is outside this change. Its current direct-CTAF feature-gate
behavior remains unchanged.

Diagnostics must state which policy selected the target. An Arrival or Enroute
target authorized by this gate may not be falsely reported as having required
or passed the older Departure-oriented compatibility gate.

## 7. Radio and Safety Rules

Every target remains subject to the existing Brain-owned radio protections:

- write only to COM1 standby;
- never write when Standby Assist is off;
- never write an empty, malformed, invalid, or guard frequency;
- do not rewrite a frequency already in COM1 standby;
- do not select Arrival CTAF or UNICOM when that frequency is already active
  on COM1 or COM2;
- do not select Enroute UNICOM when it is already active on COM1; COM2 active
  on `122.800` does not satisfy the approved COM1 standby target and therefore
  does not block the Enroute write;
- require the existing valid workflow, plan identity, and radio-state context;
- use the existing one-shot latch and reset rules;
- report writer failure without pretending the standby load succeeded; and
- do not add a direct simulator write outside the existing writer path.

If an Arrival CTAF lookup is pending or failed, the Brain must omit the CTAF or
UNICOM sequence item. It may continue through proven online controller rows in
their existing safe order, or select no target when none exists.

## 8. Existing Behavior That Must Remain Unchanged

This gate must preserve:

- Brain-only decision ownership;
- current controller online/offline and relevance decisions;
- current Center relation ordering and route progression;
- current multiple-controller deterministic ordering;
- current COM1 standby writer validation and failure reporting;
- current Assist enable/disable UI and saved master setting;
- current latch, plan-change, stage-change, and radio-change behavior;
- Departure Standby Assist behavior;
- ATIS ineligibility;
- guard-frequency rejection;
- xPilot, PDC, information drawer, overlay, and Manager behavior;
- CTAF/UNICOM lookup caching and retry behavior;
- no guessed UNICOM on lookup failure for Arrival; and
- no new polling, thread, timer, queue, callback, or network request.

## 9. Explicitly Out of Scope

This gate does not authorize:

- changing how controllers are declared online;
- changing the controller operational-eligibility work from the prior gate;
- changing the visual order of overlay rows;
- adding a visible Enroute UNICOM row;
- changing Departure radio order;
- changing the CTAF lookup service or its network endpoint;
- adding country tables or hard-coded ICAO-prefix region tables;
- adding another xPilot bridge or reading xPilot's displayed board;
- writing COM1 active or COM2;
- automatically swapping COM1 active and standby;
- simulator deployment, live VATSIM testing, packaging, or release; or
- Manager changes.

## 10. Required Pre-Code Rollback

After approval and before implementation:

1. Record the exact approved baseline commit.
2. Create a named rollback branch at that baseline.
3. Save the pre-change status and patch outside the repository.
4. Preserve all unrelated dirty and untracked files without staging,
   modifying, deleting, or absorbing them.
5. Reproduce and save the required red cases from Section 12.

No implementation source may be edited before these rollback steps are
complete.

## 11. Authorized Change Boundary

The expected implementation manifest is limited to:

- `brain/include/XVatsim/brain/BrainOwnedRuntime.h`
- `brain/src/BrainOwnedRuntime.cpp`
- `docs/STANDBY_ASSIST_REGIONAL_SEQUENCE_CONTRACT_GATE.md`
- `docs/STANDBY_ASSIST_DIRECT_CTAF_GATE.md`
- `docs/user_guide/XVatsim_User_Guide.md`
- `tools/regression_harness/src/main.cpp`, only for focused receipt formatting
  or the required regional-sequence performance and repeatability probe
- `tools/regression_harness/scenarios/standby_assist_arrival_uses_brain_display_order_center_before_app_ground.scn`
- new scenario files whose names begin with
  `standby_assist_regional_sequence_`

The plugin shell, radio writer, settings store, CTAF lookup module, controller
feed, xPilot bridge, Manager, installer, package, and release files are not
authorized to change.

If implementation proves that another production file is required, stop and
amend this gate before editing it.

## 12. Required Pre-Fix Red Proof

Before the fix, production-seam tests must demonstrate these existing failures:

1. **Non-CTAF Arrival gap:** Center and Approach/Departure are absent while
   Tower and Ground are online; current Assist selects Tower instead of UNICOM
   `122.800`.
2. **CTAF Arrival ordering gap:** upper controllers and Tower are exhausted or
   absent while CTAF and Ground are available; current Assist selects Ground
   before CTAF or produces no CTAF target.
3. **Arrival master-switch gap:** Standby Assist is on and the Arrival
   CTAF/UNICOM fact is resolved, but the separate compatibility gate prevents
   the required target.
4. **Enroute fallback gap:** every eligible Enroute controller is absent;
   current Assist produces no COM1 standby target instead of UNICOM `122.800`.

The saved red output must include the actual target source, frequency, writer
eligibility, and reason for each case.

## 13. Required Green Proof

### 13.1 CTAF Arrival sequence

A production-seam scenario must prove this progression:

```text
Center -> Approach/Departure -> Tower -> CTAF -> Ground
```

It must also prove direct advancement when one or more roles are absent and
that Ground does not displace the CTAF item.

### 13.2 Non-CTAF Arrival sequence

A production-seam scenario must prove this progression:

```text
Center -> Approach/Departure -> UNICOM 122.800 -> Tower -> Ground
```

It must explicitly prove that UNICOM is selected before an online Tower when
Center and Approach/Departure are absent.

### 13.3 Enroute fallback

Production-seam scenarios must prove:

- a valid Enroute controller wins while it exists;
- zero eligible Enroute controllers selects UNICOM `122.800`;
- the Enroute fallback does not select a departure or arrival CTAF;
- a newly available Enroute controller restores controller priority;
- Assist off produces no write;
- UNICOM already active produces no write;
- UNICOM active on COM2 still loads `122.800` into COM1 standby;
- UNICOM already in COM1 standby produces no redundant write; and
- writer failure is reported without a false standby marker or success.

### 13.4 Unknown and failure states

Production-seam scenarios must prove:

- pending Arrival CTAF lookup does not guess UNICOM;
- failed Arrival CTAF lookup does not guess UNICOM;
- empty or invalid CTAF frequency is blocked;
- a valid resolved no-CTAF result produces exactly `122.800`; and
- no raw worker flag can bypass the Brain selection receipt.

### 13.5 Regression protections

Focused verification must include the existing scenarios for:

- Arrival Center before Approach/Ground ordering;
- tuned stale Center rejection after a polygon transition;
- controller target selection and writer success;
- Standby Assist disabled;
- pending, failed, empty, guard, and already-active frequency blocks;
- direct CTAF/UNICOM Departure compatibility-gate behavior;
- latch reset on plan, stage, and radio changes; and
- Brain-owned writer result accounting.

## 14. Decision Receipts

Each evaluation must expose one Brain-owned receipt containing:

- workflow stage;
- policy: `arrival-ctaf-sequence`, `arrival-unicom-sequence`,
  `enroute-controller-sequence`, or `enroute-unicom-fallback`;
- whether controller candidates exist;
- airport-radio resolution state when Arrival uses CTAF or UNICOM;
- the ordered candidate roles and frequencies considered;
- the selected target source and frequency, or the exact no-target reason;
- master Assist enable state;
- already-active and already-standby facts;
- latch key and latch-consumed state;
- writer requested, attempted, and succeeded facts; and
- the final reason.

An Enroute UNICOM receipt must explicitly state that zero eligible Enroute
controllers caused the fallback and that no airport CTAF fact was used.

## 15. Static Bible Proof

The evidence report must prove:

- the stage-specific sequence is built only in Brain code;
- workers still report facts without choosing order or target;
- the plugin performs no target selection;
- the existing writer receives only the Brain-approved frequency;
- Arrival CTAF/non-CTAF classification comes from accepted Brain evidence;
- Enroute UNICOM is created by the Brain only after the eligible-controller
  count is zero;
- the Enroute path does not consume departure or arrival CTAF as its target;
- pending or failed Arrival lookup cannot create UNICOM;
- Departure behavior is unchanged;
- no country or ICAO-prefix table was added;
- no network, thread, timer, queue, or callback was added; and
- the changed-file list matches Section 11 exactly.

## 16. Build, Regression, Repeatability, and Performance

After implementation:

- configure a fresh Release build from the approved source;
- build `XVatsimRegressionHarness` and `XVatsimPlugin` in Release;
- run every discovered `.scn` file in sorted order twice with zero failures;
- record the pre-change scenario count of `888`, the number of new scenarios,
  and the final discovered total;
- run every focused case in Sections 13.1 through 13.5;
- run the regional sequence proof at least 100 consecutive times with identical
  output;
- review the complete diff for unrelated changes; and
- hash the Release harness and plugin with SHA-256.

The Release performance proof must demonstrate:

- `10,000` Arrival sequence evaluations in no more than `250 ms` after warm-up;
- `10,000` Enroute no-controller fallback evaluations in no more than `250 ms`
  after warm-up;
- process working-set growth no greater than `4 MiB` across the repeated
  evaluations;
- zero added worker threads, timers, queues, callbacks, and network requests;
- no repeated per-frame logging; and
- deterministic target and receipt output.

Compilation alone, one passing scenario, or a visual screenshot is not proof.

## 17. Evidence Receipt

The final evidence package must contain:

- approved baseline and ending source commit;
- rollback branch and external rollback packet;
- exact changed-file manifest;
- all four preserved pre-fix red cases;
- focused green output for every required sequence and safety case;
- both complete regression passes and exact totals;
- Release build output;
- 100-run repeatability output;
- performance measurements;
- static Bible ownership proof;
- representative Arrival CTAF, Arrival UNICOM, and Enroute UNICOM receipts;
- proof that Departure behavior did not change;
- harness and plugin SHA-256 hashes;
- proof that no live simulator installation was modified;
- known limitations; and
- tested source-level rollback instructions.

If any proof is missing or fails, the result remains `not proven`.

## 18. Rollback Boundary

This change must be one narrow source commit. It has no data migration, settings
migration, bridge payload, Manager payload, installer, package, or release
change.

Rollback is the removal of that one source commit and its untracked evidence
directory. The pre-code rollback branch must reproduce the saved red cases so
the boundary is proven.

## 19. Stop Conditions

Stop and return to the Product Owner if:

- the implementation requires a file outside Section 11;
- a worker or the plugin must decide order or target;
- a failed or unknown lookup must be treated as no CTAF;
- Enroute fallback requires an airport CTAF lookup;
- a new network call, thread, timer, queue, or callback is required;
- Departure, controller eligibility, overlay ordering, or active-radio behavior
  must change;
- a live simulator or VATSIM test becomes necessary before offline proof;
- any focused, regression, build, repeatability, memory, or performance check
  fails; or
- the result can only be described as compiled rather than proven.

## 20. Approval Boundary

Product Owner approval authorizes only the implementation, documentation, and
offline verification described in this gate. It does not authorize simulator
deployment, live VATSIM verification, packaging, release, Manager work, bridge
work, or another engineering slice.

The correct completion claim is:

```text
Existing facts preserved
    -> Brain builds the approved stage-specific sequence
    -> Brain selects one COM1 standby target or none
    -> existing writer attempts only that target
    -> Brain records the result
    -> focused and complete offline proof pass
```

Until the authorized proof is complete, the status remains `not proven`.

## 21. Implementation Closeout — 2026-10-22

The approved Arrival CTAF/UNICOM sequences and Enroute `122.800` fallback were
implemented in the Brain. The existing writer only attempted the frequency
selected by the Brain. Focused sequence, latch, writer-failure, repeatability,
memory, and performance proofs passed, followed by the complete applicable
`911 / 911` regression set. The Product Owner confirmed Standby Assist during
the controlled live flight and authorized the combined source commit.
