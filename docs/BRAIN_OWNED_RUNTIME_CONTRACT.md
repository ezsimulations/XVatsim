# Brain-Owned Runtime Contract

Date locked: 2026-05-20

Status: active top-level architecture contract.

## Purpose

XVatsim must be organized around one decision maker: the brain.

Modules are workers. They do not own truth, workflow, UI display, cadence, or
fallback decisions. They receive a specific input from the brain, produce a
specific fact set, and stop.

The brain decides:

- which modules run
- when modules run
- what data each module receives
- which facts are accepted
- which facts are rejected
- what the UI displays
- when everyone goes idle

This contract supersedes any runtime design that lets module code self-trigger,
talk to other modules, wake heavy proof paths, or publish display truth.

## Architectural Rule

`Brain decides. Modules produce facts. UI displays brain-approved facts.`

## Clean Module Extension Rule

New removable modules are the preferred clean Engineer 3 way to add a fact
source.

A new module is clean when:

- it has one clear worker purpose
- it receives explicit input from brain-owned runtime
- it returns facts and diagnostic reasons
- it does not decide workflow, relevance, display, or fallback cadence
- it does not call other live modules
- it can be removed later as a single replaceable unit if the design changes

Do not label a module dirty only because it is new. Repo dirt means decision
logic in the wrong layer, plugin-owned feature orchestration, module-to-module
shortcuts, self-triggering heavy work, or patching around a bad boundary instead
of replacing it cleanly.

## Session Contract Gate

Before any code edit, the assistant must produce a short Contract Gate and wait
for explicit user approval.

The Contract Gate must list:

- files intended to change
- which layer owns each decision
- whether `plugin/src/XVatsimPlugin.cpp` is touched, and why
- why the change is Version 1 runtime work, not Version 2 behavior
- what incorrect code will be deleted/replaced instead of patched around
- what focused regression proves the contract boundary

If the assistant skips this gate, coding must stop. The correct user response
is: `Stop. Contract violation. No code.`

No existing handoff, chat context, or prior approval carries permission into a
new edit. Each coding change needs its own Contract Gate.

## Plugin Shell Boundary

The plugin is the X-Plane shell. It samples host facts, passes facts into
brain-owned runtime entry points, applies X-Plane side effects requested by the
brain, and renders the final brain-approved display snapshot.

The plugin must not contain feature-specific decision or scheduling code. New
fact work belongs in removable modules, and the brain-owned runtime must decide
when that work is needed. Feature-specific plugin orchestration is a boundary
risk; the module itself is not the dirty part.

Correct shape:

- shell samples facts
- brain decides what work is needed
- worker modules run only from brain-owned worker requests
- modules return facts and stop
- brain accepts/rejects facts
- UI displays only the final brain-approved snapshot

Any new feature-specific plugin call is a contract violation. Migration work
must shrink or remove existing plugin feature wiring, and must be approved by
the Contract Gate before editing.

No live module may:

- call another live module
- self-wake from inside a refresh loop
- decide workflow phase
- decide UI display
- request fallback proof on its own
- scan broad/world data unless explicitly given that job by the brain
- keep rechecking completed work when its input hash has not changed

If a module needs more information, it must return an incomplete result with a
reason. The brain decides whether to schedule another worker for more data.

## Module Roles

### Radio Range Worker

Input:

- aircraft position/radio state
- xPilot/VATSIM reachable controller source

Output:

- reachable controller candidates
- callsign
- frequency
- facility group: `CTR`, `APP_DEP`, `TWR`, `GND`, `DEL`, `ATIS`, `CTAF`
- stable board hash
- source and stale/available state

Forbidden:

- deciding whether a controller belongs to the route
- deciding whether a controller goes to the UI
- waking route authority or polygon proof

### Route Polygon Worker

Input:

- filed flight plan
- aircraft position
- route/nav/sector data available to XVatsim

Output:

- route polygon sequence
- current polygon
- next polygon
- arrival polygon
- route identity hash
- stale/available state

Forbidden:

- deciding online controller relevance
- displaying controllers
- scanning controller feeds

### Route Polygon Transition Worker

Input:

- current aircraft position
- the brain-owned route polygon sequence
- last known current polygon key

Output:

- route progress distance
- current polygon key/index
- next polygon key
- final route polygon key
- whether a polygon transition occurred
- whether the transition is into the final route polygon

Forbidden:

- rebuilding the route
- scanning controller feeds
- deciding controller relevance
- publishing UI snapshots
- waking arrival before the 200nm arrival rule

### Terminal Authority Worker

Input:

- departure or arrival airport ICAO
- airport coordinates when available
- current source generation/time context

Output:

- endpoint terminal owner tokens
- endpoint terminal polygon keys
- source and stale/available state
- diagnostic status and cache status

Forbidden:

- deciding whether any live controller displays
- reading the radio board directly
- calling the airport frequency worker
- publishing UI snapshots

### Airport Frequency Catalog Worker

Input:

- active departure airport ICAO
- active arrival airport ICAO
- brain-supplied/source-adapter FAA/NASR `FRQ.csv` fact source

Output:

- departure airport frequency facts
- arrival airport frequency facts
- role classification such as `ATIS`, `GND`, `TWR`, `DEL`, `APP_DEP`,
  `CTAF`, or `UNICOM`
- source and stale/available state
- diagnostic status and cache status

Forbidden:

- deciding controller relevance
- talking to terminal authority, route polygon, radio range, or UI modules
- scanning unrelated airports after the brain has scoped the request to the
  active departure/arrival pair
- publishing UI snapshots

### Controller Relevance Worker

Input:

- current workflow phase
- current/next/arrival polygon context from the brain
- reachable controller candidates from the brain
- route polygon sequence from the brain
- endpoint terminal authority facts from the brain
- endpoint airport frequency facts from the brain

Output:

- accepted controllers with polygon assignment
- rejected controllers with reason
- completed candidate count
- input hash
- stale/available state

Forbidden:

- pulling controller feeds directly
- pulling route polygons directly
- changing workflow phase
- publishing UI snapshots
- waking heavy fallback on its own

### UI Worker

Input:

- final brain-approved display snapshot

Output:

- rendered overlay

Forbidden:

- deciding controller relevance
- triggering authority work
- changing workflow phase

## Brain-Owned Runtime Flow

1. Brain waits for valid aircraft state, battery, xPilot connection, and filed
   flight context.
2. Brain schedules Route Polygon Worker when a scoped route map is needed.
3. Brain schedules Radio Range Worker when a reachable controller board is
   needed.
4. Brain schedules endpoint Terminal Authority and Airport Frequency workers
   only when their scoped airport-pair inputs are missing, stale, or changed.
5. Brain compares the radio board hash, route hash, terminal authority hashes,
   airport frequency hash, polygon index, and workflow phase against the last
   completed state.
6. If nothing changed, brain republishes the last proven display snapshot and
   all workers remain idle.
7. If something changed, brain sends only changed, phase-relevant candidates to
   Controller Relevance Worker.
8. Controller Relevance Worker marks every candidate as accepted or rejected.
9. Brain publishes only accepted, phase-relevant controllers to the UI.
10. Brain records completed work so the same candidates are not reprocessed.
11. Brain sleeps until a valid wake trigger occurs.

## Phase Rules

### Departure Polygon

Polygon 1 always wants reachable:

- departure airport local authority: `DEL`, `GND`, `TWR`
- departure terminal authority: `APP_DEP`
- current/reachable center authority: `CTR`
- CTAF/UNICOM if no controlled local frequency is available

### Enroute Polygons

Intermediate route polygons want reachable:

- center authority: `CTR`

They do not need:

- random local airport controllers
- unrelated approach/departure controllers
- arrival airport local authority before arrival wake distance

### Arrival Polygon

The final route polygon becomes arrival-prep at 200nm from destination.

Arrival prep wants reachable:

- destination local authority: `DEL`, `GND`, `TWR`
- destination terminal authority: `APP_DEP`
- arrival/current center authority: `CTR`
- CTAF/UNICOM if no controlled local frequency is available

## Version 1 Runtime Boundary

Version 1 must not display unproven frequency rows as an "unknown" state and
must not add pilot visible/hidden controls for unresolved controller candidates.
Those ideas belong to a possible Version 2 discussion only.

Version 1 behavior:

- proven relevant frequencies display
- unproven or irrelevant frequencies are rejected/hidden
- every rejection needs a diagnostic reason
- no magenta/unknown/unresolved UI state is allowed in the live runtime
- no pilot "mark visible" or "mark hidden" controls are allowed in the live
  runtime

### Engineer 3 Display Relation Rule

Controller row color is brain relation truth:

- `CURRENT_POLYGON` displays as the current-polygon/green row.
- `NEXT_POLYGON` and `ARRIVAL_PREP` display as the outside/current-future
  polygon orange row.
- `UNKNOWN`, `FILTERED`, and `HIDDEN` are not pilot-facing relevant controller
  colors.

Radio tuning does not decide polygon color. Dialing a controller in COM1 or
COM2 may add an `Active` text badge, but it must not promote a row to
current-polygon/green.

Standby assist does not decide polygon color. It may add a `Standby` text badge
only when the brain-selected target frequency is actually loaded into COM1
standby.

The only text badges allowed on controller frequency rows in Version 1 are:

- `Active`
- `Standby`

Do not display textual `NEXT`, `ONLINE`, sector `ACTIVE`, or `OFFLINE` badges
on controller frequency rows. Orange already communicates next/outside-polygon
state. Green already communicates current-polygon state. Online/offline and
sector-active facts may remain internal worker/relevance facts, but they are
not display-state labels in the Engineer 3 UI contract.

APP/DEP terminal authority must be proven by endpoint-scoped evidence for the
current departure or destination context: terminal owner facts, exact FAA/NASR
endpoint APP/DEP frequency facts, or both. Distance alone must never prove
terminal authority.

Airport local authority (`DEL`, `GND`, `TWR`) may use airport-root callsign
proof such as `KONT_TWR`, `KONT_W_TWR`, `ONT_GND`, or `ONT_DEL`. Nearby local
airport controllers must not pass because they are close by distance.

### Controller Identity Evidence Rule

Controller identity must not regress to Engineering 1 string-only pass/fail.
Callsign text is a clue, not proof.

The live runtime must make controller decisions only for candidates that are
already on the brain-owned radio board. The radio board candidate supplies the
live callsign, facility group, frequency, and reachability/transceiver evidence.

Minimum Version 1 proof shape:

- radio-board candidate exists for the live controller
- candidate frequency is carried through the decision
- candidate facility group matches the phase being evaluated
- callsign/service tokens narrow the possible authority owner
- endpoint terminal authority or route polygon authority approves the candidate
- rejected candidates record the callsign, frequency, facility group, and reason

Do not build or maintain a global airport/TRACON/center alias table as the
authority engine. Airport tokens such as `ONT`, `SCT`, `PANC`, or `ANC` are
only clues used inside a scoped radio-board decision. The brain does not need
to solve every controller worldwide up front; it needs to prove whether the
currently presented frequencies belong to the current flight.

### Three-Source Airport/Terminal Evidence Rule

Airport local and terminal APP/DEP decisions use three scoped evidence sources:

- the live brain-owned radio board candidate
- endpoint terminal/airport authority facts, currently derived from SimAware
  source data
- endpoint FAA/NASR airport frequency facts

The radio board candidate is mandatory. If a controller is not on the radio
board, Controller Relevance does not evaluate it.

When FAA/NASR endpoint role facts exist for the candidate role:

- exact endpoint frequency plus compatible role is high-confidence evidence
- exact endpoint frequency plus terminal/airport authority match is
  extremely-high-confidence evidence
- an endpoint frequency miss blocks a broad/shared text-only terminal pass

When FAA/NASR endpoint role facts are missing or unavailable:

- do not fail solely because that source is absent
- fall back to the existing endpoint terminal/airport authority proof
- record the missing frequency proof in diagnostics when possible

Frequency alone is not global proof. Callsign and frequency together still need
scoped endpoint or route authority context before display.

## Wake Triggers

The brain may request worker updates only when one of these changes:

- xPilot connection state
- filed flight plan identity
- aircraft current polygon
- next polygon boundary/prewake state
- arrival 200nm wake state
- radio board stable hash
- controller feed generation that changes the radio board
- radio tuning state needed for active/standby display
- manual recovery/reset command

Ordinary UI refresh, aircraft movement inside the same polygon, or unchanged
controller feed state must not wake authority/relevance work.

## Completion Records

For every radio-board candidate processed, the brain must be able to know:

- input board hash
- workflow phase
- current polygon id
- candidate callsign
- candidate frequency
- candidate facility group
- accepted or rejected
- rejection reason if rejected
- displayed or hidden

If all candidates for the current board hash have completion records, there is
no more controller work to do.

## Heavy Fallback Rule

Heavy Engineer 2-style authority proof is not a live discovery engine.

It may run only when all are true:

- the brain explicitly schedules it
- the radio board changed
- the candidate is phase relevant
- the cheap relevance worker returned `needs_verification`
- only one fallback job is running
- diagnostics log why the fallback was needed

No heavy fallback may run because the UI refreshed.

## Diagnostics Requirements

Healthy idle diagnostics must prove:

- `board-unchanged`
- `route-polygon-unchanged`
- `phase-unchanged`
- `candidates-complete`
- `no-authority-work`
- `no-heavy-fallback`

When a controller is accepted, diagnostics must show:

- source radio-board candidate
- facility group
- polygon assignment
- acceptance reason
- display phase

When a controller is rejected, diagnostics must show:

- callsign/frequency/facility group
- rejection reason
- polygon/phase evaluated

## Contract Failure Conditions

Any of these are failures:

- a module calls another live module directly
- a module publishes UI truth
- the UI triggers authority/relevance work
- unchanged radio board causes controller relevance work
- unchanged route polygon causes controller relevance work
- heavy fallback runs without a brain-scheduled request
- broad route/world authority proof runs in the normal refresh loop
- a candidate disappears without a logged accept/reject reason
- a controller is displayed without a brain-approved completion record
- the plugin adds feature-specific authority/relevance/display scheduling
- Version 2 unknown/unresolved/magenta/manual-visibility behavior appears in
  the Version 1 runtime path

## Implementation Blocks

### Block 1: Brain Runtime State

Create one brain-owned runtime state object that stores:

- route polygon snapshot
- radio board snapshot
- candidate completion records
- final display snapshot
- last hashes and wake reasons

### Block 2: Worker Interface Boundaries

Define narrow worker input/output structs so modules cannot reach sideways into
other systems.

### Block 3: Radio Board Worker

Make reachable-frequency discovery a pure worker output. It returns the board
and hash only.

### Block 4: Route Polygon Worker

Make route polygon sequencing a pure worker output. It returns polygon context
and hash only.

### Block 5: Relevance Worker

Make controller relevance evaluate only the candidates and polygon context the
brain gives it.

### Block 6: Brain Publisher

Make the brain assemble the final UI snapshot from accepted completion records.

Implementation note:

- Relevance workers must not build a final display board.
- Relevance workers must not mark a candidate as displayed.
- The brain publisher owns accepted-completion filtering, display intent, final
  board assembly, and the displayed/hidden completion state.

### Block 7: Quarantine Old Runtime Paths

Old Engineer 1/2 logic may remain compiled only if it cannot self-trigger in
the live loop. Any remaining old path must be behind an explicit brain fallback
API.

### Block 8: Diagnostics and Battle Tests

Add diagnostics and live battle tests proving:

- KCOS -> KLAS: reachable `DEN_CTR` and `KCOS_APP` are accepted for polygon 1
- unchanged board goes idle
- unrelated reachable controllers are rejected with reasons
- arrival 200nm wake accepts destination/local/TRACON candidates
- no heavy fallback runs unless the brain schedules it

## Collaboration Rule

If the implementation starts drifting back toward broad scans, module-owned
decisions, string-only pass/fail logic, or quick patches that violate this
contract, stop and realign before coding.

If the proposed change touches runtime behavior, no code may be written until
the assistant produces the Session Contract Gate and the user explicitly
approves editing.

The correct answer is not always to write more code. The correct answer is to
preserve the architecture: brain-owned decisions, worker-owned facts, and
diagnostics that prove every controller decision.
