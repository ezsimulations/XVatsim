# Reconnect Workflow Recovery Contract

Date created: 2026-05-17

This contract governs XVatsim behavior when a pilot temporarily disconnects
from xPilot/VATSIM during an active flight and later reconnects, including
long-haul overnight disconnects where the original VATSIM flight plan may need
to be re-filed.

This contract is intentionally separate from the authority evidence contract.
Reconnect recovery must not weaken or bypass the controller-authority evidence
engine.

## Goal

XVatsim must treat a temporary VATSIM/xPilot disconnect as a network pause, not
as proof that the flight ended, when the aircraft remains powered and aircraft
state remains valid.

After reconnect, XVatsim should rejoin the correct workflow stage from the
current aircraft geometry and the fresh matched VATSIM flight plan instead of
getting stuck in `READY`.

## Non-Negotiable Rules

- VATSIM/xPilot disconnect does not end the flight if battery power remains on
  and aircraft state remains valid.
- Cold/dark battery-off state still resets the plugin presentation and flight
  context as before.
- A pilot callsign change remains a hard session boundary unless a future
  contract explicitly defines callsign-change recovery.
- A genuinely different filed route remains a new-flight boundary when the
  departure or destination no longer matches the preserved flight context.
- Existing `Reset Session` remains a hard destructive reset for true next-flight
  use.
- New recovery behavior must not add controller display authority shortcuts.
  Controller boards still depend on `AuthorityRelevanceSnapshot` and the
  authority evidence contract.

## Expected Automatic Recovery

When xPilot reconnects and XVatsim has either preserved flight context or a
fresh matched VATSIM flight plan:

- If the aircraft is on the ground at or near the departure airport, workflow
  should be `DEPARTURE`.
- If the aircraft is airborne and outside the arrival wake distance, workflow
  should be `ENROUTE`.
- If the aircraft is within the configured arrival wake distance, currently
  `200nm`, workflow should be `ARRIVAL`.
- If the aircraft is on the ground at or near the destination airport, workflow
  should be `ARRIVAL`.
- If XVatsim cannot safely prove a current-flight rejoin, it should fail closed
  with an explicit diagnostic reason instead of silently pretending the flight
  is healthy.

## Manual Recovery

Add a non-destructive manual recovery action:

- User-facing name: `Recover Current Flight`.
- Purpose: clear stuck runtime/presentation bits and force workflow
  re-evaluation against the current aircraft position and matched filed plan.
- It must preserve current-flight route context when the plan matches.
- It must not behave like `Reset Session`.
- It must not force `DEPARTURE` when the aircraft is already airborne or near
  arrival.
- It should produce a clear log line stating whether recovery was accepted or
  rejected and why.

## Reset Session Separation

`Reset Session` remains the destructive reset:

- Clear flight context.
- Clear workflow latches.
- Clear board caches.
- Reset VATSIM/feed/network-plan state.
- Wait for a new valid flight plan and departure confirmation.

`Recover Current Flight` is the non-destructive reset:

- Preserve matching flight context when safe.
- Clear board/display/runtime latches that can become stuck.
- Re-evaluate workflow from aircraft geometry.
- Continue using the fresh matched VATSIM plan and authority evidence engine.

## Required Implementation Shape

The implementation should have one central recovery decision path that produces
an explicit result:

- accepted/rejected.
- reason string.
- selected workflow seed: departure, enroute, arrival, or none.
- whether preserved context was used.
- whether fresh matched VATSIM plan was used.

Do not scatter ad-hoc reconnect checks across the plugin.

## Required Diagnostics

Log reconnect/recovery events with enough detail to diagnose failures:

- xPilot disconnect detected.
- Flight context preserved or cleared, with reason.
- xPilot reconnect detected.
- VATSIM plan matched or missing/stale.
- Automatic recovery accepted/rejected.
- Manual recovery accepted/rejected.
- Selected workflow stage and stage reason.
- Route identity used for recovery: callsign, departure, destination.

## Required Harness Coverage

Before this contract is considered complete, regression scenarios must cover:

- Long-haul disconnect preserves active flight context while battery remains on.
- Reconnect midflight with same re-filed plan resumes `ENROUTE`.
- Reconnect within `200nm` of destination resumes `ARRIVAL`.
- Reconnect on ground near departure resumes `DEPARTURE`.
- Existing hard `Reset Session` still clears context and requires normal
  departure confirmation.
- Battery-off/cold-dark still clears context and does not preserve a flight.
- Callsign change still clears context.
- Different departure/destination route is rejected as current-flight recovery.
- Manual `Recover Current Flight` can unstick a valid midflight plan.
- Manual `Recover Current Flight` rejects missing/stale/unmatched plan.

## Definition Of Done

This contract is complete only when:

- The contract remains documented here.
- Code implements the automatic reconnect behavior without touching or
  weakening authority evidence decisions.
- Code implements the manual `Recover Current Flight` command/menu action.
- The existing `Reset Session` behavior remains available and destructive.
- Required diagnostics are emitted.
- Required harness coverage exists and passes.
- Existing authority evidence harness scenarios still pass.
- Plugin builds successfully.
- The final implementation review confirms no session-recovery shortcut was
  added to ENROUTE/DEPARTURE/ARRIVAL controller authority display logic.

## Current Implementation Checkpoint

As of 2026-05-17, this contract is implemented at the current checkpoint:

- xPilot disconnect preserves the active flight context when the aircraft state
  is valid and battery power remains on.
- Cold/dark battery-off still clears presentation and flight context.
- Reconnect with the same callsign sets a pending automatic current-flight
  recovery and waits for a fresh matched VATSIM plan.
- Reconnect or manual recovery uses the central
  `ResolveCurrentFlightRecovery(...)` decision path.
- Recovery accepts same-route preserved context and fresh-plan in-flight attach.
- Recovery selects `DEPARTURE`, `ENROUTE`, or `ARRIVAL` from aircraft geometry,
  including the existing `200nm` arrival wake distance.
- Recovery rejects battery-off, missing/stale/unmatched plan, callsign change,
  different departure/destination, and on-ground aircraft not at a route
  endpoint.
- `Recover Current Flight` exists as a menu item and command:
  `xvatsim/recover_current_flight`.
- `Reset Session` remains the destructive next-flight reset:
  `xvatsim/reset_session`.
- Recovery logs accepted/rejected result, reason, selected stage, preserved
  context use, fresh-plan use, and route identity.
- Regression harness coverage includes 10 `recovery_*.scn` scenarios for the
  contract paths above.
- Full regression harness passed with `ALL_SCENARIOS_PASSED count=203`.
- `XVatsimPlugin` built successfully after the recovery changes.
