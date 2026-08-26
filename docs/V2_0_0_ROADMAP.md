# XVatsim V2.0.0 Roadmap

Status: locked for execution

Approved: 2026-08-26

## Starting Point

XVatsim V1.2.3 is the closed, proven Version 1 baseline. It is the current
public freeware Windows/X-Plane 12/xPilot release. V2 begins from release
commit `e4a6269` and tag `v1.2.3` on the `v2-development` branch.

V1.2.3 passed its Release plugin and regression-harness builds, eight focused
route/update guardrails, all `451 / 451` saved regression scenarios, package
smoke validation, and user-guide review.

## V2 Product Boundary

XVatsim V2.0.0 remains a Windows-only companion plugin for:

- Windows x64
- X-Plane 12
- xPilot

Mac and Linux support are deferred. They are not V2.0 acceptance requirements
because the project does not currently have the native hardware and live-test
resources required to prove those releases. Future work should avoid needless
platform coupling, but V2 must not spend product risk on untestable ports.

## Primary V2 Objectives

1. Add an explicit, dedicated VFR operating mode that does not require a
   VATSIM flight plan.
2. Add a METAR ORB with flight-category state, automatic IFR airport targeting,
   manual airport targeting, caching, and an expandable information drawer.
3. Add a VATSIM-only ATIS ORB with departure/destination targeting, change
   indication, and an expandable information drawer.
4. Add a PDC/private-message ORB that accepts the approved subset of xPilot
   private-message facts without reproducing general xPilot chat.
5. Preserve the current main XVatsim card design while adding the three-ORB
   accessory rail and one expandable information drawer.
6. Protect simulator frame time through brain-owned scheduling, bounded caches,
   asynchronous network work, render-on-change behavior, and measurable
   performance gates.

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

`Brain decides. Modules produce facts. UI displays brain-approved facts.`

The plugin host may sample X-Plane/xPilot facts, execute brain-approved X-Plane
side effects, and render the final view model. It must not become the owner of
VFR authority, METAR/ATIS targeting, message classification, feature cadence,
or display policy.

## Locked Implementation Sequence

Work proceeds through one bounded, approved, proven, and committed slice at a
time. A later step does not begin until the current step is accepted.

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

### Step 6 - PDC And Private Messages

Recover the existing disabled xPilot private-message prototype behind a new
approved classification contract. Define controller-PDC, pilot-private, and
VATSIM-system acceptance rules; retain bounded message state; reject general
chat; and prove long-message layout and sequence behavior.

### Step 7 - VFR Evidence Engine Preview

Build VFR controller evidence and decisions in diagnostic-only preview mode.
Use aircraft position, AGL/MSL altitude, groundspeed, stable track, reachable
transceivers, center/terminal geometry, and source freshness. Prove current and
projected Center choices, terminal traversal, uncertainty, hysteresis, and
accepted-but-not-visible accounting before changing live output.

### Step 8 - VFR Live Projection

Promote the proven VFR preview decisions to live brain-owned controller output.
Retain explicit fail-soft behavior: ambiguous evidence should prefer an extra
plausible controller over silently hiding one that may belong to the pilot.

### Step 9 - Windows V2 Release Certification

Complete focused and full regression proof, visual-output review, performance
comparison, live Windows/X-Plane/xPilot battle tests, package smoke validation,
hash capture, user-guide updates, and public release closeout.

## Feature Boundaries

### VFR

- VFR mode is explicitly selected; absence of a flight plan does not select it.
- Current containing authority and the safely projected next authority are
  brain-owned decisions.
- Track projection is bounded, cached, and protected by hysteresis.
- Altitude and distance rank evidence but must not become careless hard hides.
- APP/DEP and Tower candidates require reachable/source facts plus geographic
  evidence appropriate to low-altitude terminal traversal.
- Missing, stale, or conflicting evidence remains visible in a decision ledger.

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

### PDC And Private Messages

- xPilot private-message sequence, sender, and body are source facts.
- The brain owns acceptance, classification, unread/cache state, and display.
- General xPilot/VATSIM chat is not reproduced.
- Controller-origin messages require the separately approved PDC policy because
  the xPilot facts do not provide a structured PDC message type.

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
- METAR, ATIS, and message caches are bounded and reset at defined boundaries.
- Overlay textures rerender only when their content or appearance changes.
- A new feature must identify its idle cost, refresh cost, worst observed cost,
  and failure/backoff behavior before acceptance.
