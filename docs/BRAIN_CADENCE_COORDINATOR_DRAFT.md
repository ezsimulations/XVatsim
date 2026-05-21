# Brain Cadence Coordinator Draft

## Status

Draft discussion seed only. Do not implement until this is finalized into a contract with Darron.

## Problem Statement

The plugin is functionally much healthier, but the brain can still behave too reactively. Route state, controller feed state, transceiver state, aircraft movement, workflow stage, board updates, and overlay updates can all request work independently. That creates bursty CPU because expensive decisions may line up in the same refresh.

The desired direction is a controlled cadence routine where the brain collects data, detects meaningful changes, schedules work in priority order, and runs expensive jobs one at a time.

## Cadence Model

- Every frame or light refresh: aircraft, radio, and display-only work.
- Every 1-2 seconds: cheap state checks and UI snapshot update.
- Every 10-15 seconds: controller feed diff handling.
- On route change, meaningful movement, or 200nm boundary: route authority window update.
- Only when scheduled: heavy authority evidence proof.
- Never more than one heavy job per refresh tick.

## Proposed Brain Flow

1. Collect inputs.
2. Detect changes and convert raw changes into flags.
3. Schedule work based on priority and cadence.
4. Run at most one expensive job per cycle.
5. Publish the latest completed truth snapshot.
6. Let UI/boards consume snapshots without triggering heavy proof work themselves.

## Darron's Orchestration Model

Core principle: the brain runs the modules; the modules do not run the brain.

Startup flow:

1. X-Plane starts.
2. XVatsim wakes when battery power and xPilot connection are valid.
3. XVatsim starts empty and waits for the VATSIM flight plan.
4. Brain tells all modules to stay quiet until called.
5. Brain receives and processes the VATSIM flight plan.
6. Brain overlays the route onto the global authority polygon source map.
7. Brain creates a route-scoped authority map containing only the polygons relevant to this flight.
8. Brain assigns route polygon sequence numbers, with polygon 1 being the departure/current polygon.

Example route:

```text
KLAX -> KPDX
polygon 1: Los Angeles
polygon 2: Oakland
polygon 3: Seattle
```

Departure phase:

1. Brain determines what Departure Module needs.
2. Brain gathers airport-local authority for DEL/GND/TWR.
3. Brain feeds Departure Module only the airport-local result.
4. Brain gathers departure TRACON/APP authority.
5. Brain feeds Departure Module only the departure TRACON result.
6. Brain gathers current/departure center authority.
7. Brain feeds Departure Module and/or Enroute Module only the relevant center result.
8. If polygon 2 is within the 200nm lookahead window, Brain prepares polygon 2 center authority for Enroute Module.

Enroute phase:

1. Departure Module hands control to Enroute Module.
2. Brain now feeds Enroute Module center authority only.
3. Airport-local and TRACON checks stay quiet unless arrival prep or recovery requires them.
4. Brain updates the current and next center polygons in sequence.
5. Brain only calls center authority work needed for the current polygon and the useful 200nm lookahead window.

Arrival phase:

1. Within 200nm of the destination, Brain wakes Arrival Module.
2. Brain continues feeding Enroute Module center authority as needed.
3. Brain gathers destination airport-local authority for TWR/GND.
4. Brain feeds Arrival Module the airport-local result.
5. Brain gathers arrival TRACON/APP authority.
6. Brain feeds Arrival Module the arrival TRACON result.
7. Work remains sequential and scheduled: center, airport-local, TRACON, board refresh.

Cadence rule:

Brain decides what it needs, calls one work item, processes the result, feeds only the modules that need it, then calls the next work item.

## Example Serialized Work

```text
tick A: route update
tick B: authority fast path
tick C: unresolved authority proof
tick D: board refresh
```

The pilot may see a short delay, but the simulator should not stutter.

## Contract Shape Candidate

- Add a central `BrainWorkScheduler`.
- Expensive modules may not self-trigger from UI or board code.
- One heavy job maximum per brain cycle.
- Work is priority ordered: safety and current-position truth first, future route work later.
- UI displays the last proven snapshot while work is pending.
- Diagnostics must say which job ran, why it ran, and whether it was deferred.
- No authority truth rules change in this pass.

## Notes To Refine With Darron

- Define which jobs are light, medium, and heavy.
- Define exact cadence buckets for ground, departure, enroute, and arrival.
- Define what must run immediately for safety/trustworthiness.
- Define what can be delayed without hurting pilot usefulness.
- Define how pending work is shown or hidden in the UI.
- Define how this interacts with the authority fast-path hybrid idea.

## Working Principle

This is not replacing the authority evidence engine. It is putting a conductor in front of the orchestra so expensive work happens in a deliberate order instead of as a panic scanner.
