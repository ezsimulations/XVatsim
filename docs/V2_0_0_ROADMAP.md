# XVatsim V2.0.0 Roadmap

Status: **FEATURE-COMPLETE — ENTERING EXTENDED BETA; NOT PUBLICLY RELEASED**

Approved: 2026-08-26

## Starting Point

XVatsim V1.2.3 is the closed, proven Version 1 baseline. It is the current
public freeware Windows/X-Plane 12/xPilot release. V2 begins from release
commit `e4a6269` and tag `v1.2.3` on the `v2-development` branch.

V1.2.3 passed its Release plugin and regression-harness builds, eight focused
route/update guardrails, all `451 / 451` saved regression scenarios, package
smoke validation, and user-guide review.

V1.2.3 remains the current public release. The live-proven V2 beta candidate
deliberately retains embedded `1.2.3` metadata so extended beta observes the
same bytes that passed offline and live proof. Version assignment, packaging,
release certification, and publication require a later approved gate.

## V2 Product Boundary

XVatsim V2.0.0 remains a Windows-only companion plugin for:

- Windows x64
- X-Plane 12
- xPilot

Mac and Linux support are deferred. They are not V2.0 acceptance requirements
because the project does not currently have the native hardware and live-test
resources required to prove those releases. Future work should avoid needless
platform coupling, but V2 must not spend product risk on untestable ports.

## Accepted V2.0.0 Feature Baseline

Steps 1 through 6 are accepted, complete, and frozen. The feature-complete
baseline provides:

1. the accepted persistent Brain-owned IFR/VFR mode-selection foundation;
2. the three-ORB accessory rail and one shared expandable drawer;
3. the completed METAR ORB and drawer;
4. the completed shared-feed VATSIM ATIS ORB and drawer;
5. the completed one-shot xPilot waiting-message PDC ORB and drawer; and
6. bounded scheduling, asynchronous source work, render-on-change presentation,
   visible-publication acknowledgement, and measurable performance gates.

The accepted Step 6 implementation is
`94825f07248dafc038d4c29e06ea61e40f49cfc3`; proof and closeout is
`380039a4494f0b373542eced807345471f214036`. Final proof is visual `14/14`,
focused `61/61`, complete regression `861/861` twice, and protected evidence
`80 manifests / 4,695 rows / 0 mismatches`. The accepted corpus is `861`
scenarios with fingerprint
`227121979F59CB3BB359B96DE57AC7DF601E0BE3736F82F38C19EE08437431D9`.

The Product Owner normal-use live flight passed `IDLE -> NEW 1 -> OPEN ->
MSG 1`, immutable reopen, exact accounting, normal shutdown, and private-safe
records without Plugin Admin manipulation, reset, recovery, artificial
ordering, retry, perceived lag, or visual defect. The exact `3/3` candidate
remains installed without rollback as the extended-beta baseline.

## Non-Goals For V2.0

- Mac support
- Linux support
- X-Plane 11 support
- replacing or forking xPilot
- owning VATSIM network or audio-client behavior
- SimBrief or Navigraph AIRAC ingestion
- second-monitor or standalone desktop mode
- TAF, weather-radar, or moving-map features
- general VATSIM chat duplication

Deferred work may return in a later V2.x milestone only when it has its own
source-of-truth, test resources, Contract Gate, and release proof.

## Architecture Contract

V2 does not loosen the governing runtime rule:

`Brain decides. Modules produce bounded mechanical facts. UI displays brain-approved facts.`

The plugin host may sample X-Plane/xPilot facts, execute brain-approved X-Plane
side effects, and render the final view model. It must not become the owner of
VFR authority, METAR/ATIS targeting, PDC admission, feature cadence, or display
policy.

## Accepted Implementation Sequence

Steps 1 through 6 were completed through bounded, approved, proven, and
committed slices. They now form one frozen feature baseline.

### Step 1 - Windows V2 Proof Baseline

Create a repeatable development validation entry point that:

- performs fresh Release builds of the plugin and regression harness
- runs every saved regression scenario
- reports expected and executed scenario counts
- records elapsed time and output hashes
- fails visibly on any build or scenario failure
- produces a reviewable proof receipt

This step must not change live plugin behavior.

### Step 2 - Explicit IFR/VFR Mode Foundation

Add brain-owned operating-mode state, menu selection, persistence/session rules,
diagnostics, and regression coverage. Selecting VFR must not yet change which
controllers display.

### Step 3 - ORB Rail And Information Drawer

Add the METAR, ATIS, and PDC ORB interaction shell without live feature data.
Preserve the main card design. Only one drawer may be open at a time. Prove
layout, scaling, clipping, scroll behavior, and render-on-change performance.

### Step 4 - METAR

Add a removable METAR fact worker, bounded airport cache, parser/classifier,
brain-owned target and freshness decisions, manual target/revert controls, and
METAR ORB/drawer publication.

### Step 5 - VATSIM ATIS

Extend the existing VATSIM feed facts to include the dedicated ATIS records,
then add brain-owned departure/destination targeting, freshness/change state,
and ATIS ORB/drawer publication. Do not add a second ATIS network poll.

### Step 6 - One-Shot PDC ORB

The qualified xPilot bridge reports bounded mechanical facts only while the
Brain commands acquisition for a complete flight identity. The Brain accepts
the first stable-connected positive-sequence observation with a successfully
read non-empty bounded body, atomically retains one flight-bound snapshot,
marks it unread, and closes all further private-source sampling for that
flight. Sender, controller roster, message wording, labels, IFR/VFR selection,
and workflow stage do not classify or veto the mechanically valid observation.

PDC projects exact `IDLE`, `SOURCE`, `CHECK`, `NEW 1`, `OPEN`, and `MSG 1`
states. The drawer title is `PDC — <DEPARTURE ICAO>` and the warning is
`CAPTURED SNAPSHOT — CHECK XPILOT FOR REVISIONS`. xPilot remains authoritative
for later amendments. XVatsim has no general private-message inbox or amendment
monitor.

### Optional Version 3 - Dedicated VFR Model

The Step 2 IFR/VFR mode foundation remains, but a dedicated VFR evidence engine
and live VFR controller projection are not part of V2.0.0. They are an optional
Version 3 decision only, and Version 3 is not promised. Any proposal must first
justify pilot value against geometry, prediction, CPU, testing, and maintenance
cost and requires its own roadmap decision and Contract Gate.

### Future Windows V2 Release Certification

V2.0.0 is feature-complete and entering extended multi-flight beta. Public
release certification is future work, not accomplished by feature closeout. A
separate gate must authorize version assignment, exact-binary build and smoke
proof, focused/full regression, visual and performance review, multi-flight
live evidence, packaging, user-guide updates, hashes, and publication.

## Feature Boundaries

### VFR

- VFR mode is explicitly selected; absence of a flight plan does not select it.
- The accepted mode-selection and persistence foundation remains in V2.0.0.
- V2.0.0 makes no promise to create dedicated VFR evidence, prediction, current
  or projected authority, or live controller projection.
- Any such model is an optional Version 3 decision requiring separate product,
  architecture, performance, proof, and live-test authority.

### METAR

- IFR Departure monitors the departure airport.
- IFR Enroute and Arrival monitor the flight-plan destination.
- A manual airport target can be selected and explicitly reverted.
- Initial VFR behavior uses a pilot-selected airport; automatic VFR targeting
  is deferred until separately approved.
- Network work is asynchronous and cached. Updated observations, including
  special observations, must not depend only on fixed `:00`/`:30` checks.
- Category colors are green VFR, blue MVFR, red IFR, and magenta LIFR; category
  text must also be shown. Unprovable state is gray/unknown, never guessed.

### ATIS

- IFR Departure monitors only departure VATSIM ATIS.
- IFR Enroute and Arrival monitor only destination VATSIM ATIS.
- ATIS comes from the existing VATSIM data feed and has no offline substitute.
- Information letter, changed/unread state, freshness, and unavailable state
  are brain-owned display facts.

### One-Shot PDC

- xPilot connection, sequence, sender bytes, body bytes, and read outcomes are
  bounded mechanical source facts; no module classifies their meaning.
- Acquisition requires a complete Brain-owned flight identity and stable xPilot
  connection.
- The Brain accepts the first positive-sequence non-empty bounded observation
  exactly once, independent of sender, roster, wording, labels, operating mode,
  and workflow stage.
- Exactly one immutable flight-bound snapshot is retained. Capture stops all
  further private-source sampling for that flight.
- A rare first administrative message is an accepted truthful limitation.
- Later PDC revisions are intentionally ignored by XVatsim; xPilot is
  authoritative and the exact drawer warning tells the pilot to check xPilot.
- There is no general xPilot/VATSIM chat reproduction, combined inbox, semantic
  message classifier, or private-message history.

### UI

- The current main card design remains intact.
- The ORBs are an attached accessory rail, not a replacement card.
- At most one information drawer is open.
- Long content wraps and scrolls; it is not silently truncated.
- Color is never the only indication of state.
- Static/unchanged content behaves like idle and is not rerasterized per frame.

## Proof Standard

No task is complete because an engineer reports that code was written. Every
behavior-changing slice requires:

1. An approved Contract Gate before edits.
2. Focused regression coverage that fails without the intended behavior.
3. Passing focused tests after implementation.
4. Passing full saved regression coverage.
5. Release plugin and regression-harness build proof.
6. Visual output and text-bound proof for UI changes.
7. Before/after timing evidence for frame-path or worker changes.
8. Live X-Plane/xPilot evidence when simulator integration changes.
9. A clean, narrowly scoped commit with no unrelated changes.
10. Director/user acceptance before the next task begins.

Documentation-only and audit-only work must still be reviewed for consistency,
scope, and clean commit history.

## Performance Invariants

- No network operation may block the X-Plane frame thread.
- No broad/world geometry scan may run from an ordinary UI refresh.
- Heavy proof is brain-scheduled and bounded.
- Source generations, stable keys, and input hashes control recomputation.
- METAR and ATIS histories are bounded; PDC retains one bounded flight-bound
  snapshot and resets only at defined Brain-owned lifecycle boundaries.
- Overlay textures rerender only when their content or appearance changes.
- A new feature must identify its idle cost, refresh cost, worst observed cost,
  and failure/backoff behavior before acceptance.

Extended beta will monitor initial flight/route/authority preparation across
multiple flights and require stable, calm, low-churn settled operation. The one
observed `1,713 ms` route/authority refresh warning is a nonblocking beta
observation: it occurred after successful PDC capture, was unrelated to PDC
sampling or rendering, caused no Product Owner-perceived lag, and tripped no
accessory threshold. Any correction, beta version/package work, release
certification, public release, or Version 3 roadmap requires a new Contract Gate.
