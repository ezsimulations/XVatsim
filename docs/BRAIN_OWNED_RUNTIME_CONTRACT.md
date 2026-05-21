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

No live module may:

- call another live module
- self-wake from inside a refresh loop
- decide workflow phase
- decide UI display
- request fallback proof on its own
- scan broad/world data unless explicitly given that job by the brain
- keep rechecking completed work when its input hash has not changed

If a module needs more information, it must return an incomplete result with a
reason. The brain decides whether to ask another module for more data.

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

### Controller Relevance Worker

Input:

- current workflow phase
- current/next/arrival polygon context from the brain
- reachable controller candidates from the brain
- route polygon sequence from the brain

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
2. Brain asks Route Polygon Worker to build the flight's scoped route map.
3. Brain asks Radio Range Worker for the reachable controller board.
4. Brain compares the radio board hash, route hash, polygon index, and workflow
   phase against the last completed state.
5. If nothing changed, brain republishes the last proven display snapshot and
   all workers remain idle.
6. If something changed, brain sends only changed, phase-relevant candidates to
   Controller Relevance Worker.
7. Controller Relevance Worker marks every candidate as accepted or rejected.
8. Brain publishes only accepted, phase-relevant controllers to the UI.
9. Brain records completed work so the same candidates are not reprocessed.
10. Brain sleeps until a valid wake trigger occurs.

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

The correct answer is not always to write more code. The correct answer is to
preserve the architecture: brain-owned decisions, worker-owned facts, and
diagnostics that prove every controller decision.
