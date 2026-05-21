# Radio Board Runtime Contract

Date locked: 2026-05-19

Status: active Engineer 3 runtime cleanup contract.

## 2026-05-19 Clean Runtime Reset

The live simulator results proved that adding Engineer 3 as another layer inside
the existing `RefreshOverlayFromBrain()` loop is not enough. The repository now
contains mixed Engineer 1, Engineer 2, and Engineer 3 runtime seams, and that
mix is itself a release blocker.

The reset decision is:

- Engineer 3 owns the live runtime path.
- Engineer 2 is not a live discovery engine.
- Engineer 2 evidence code may remain only as an explicit, one-shot verifier
  called by Engineer 3 for a changed radio-board candidate.
- Engineer 1 string matching may remain only as cheap supporting metadata, not
  as an authority pass/fail engine.
- Old route, authority, terminal, arrival, and enroute proof paths are
  quarantined from ordinary UI refresh.

This is a runtime wipe, not a source deletion. Working UI, X-Plane integration,
session recovery, radio sampling, CTAF lookup, flight-plan identity, and build
infrastructure are preserved. Heavy authority discovery is removed from the
normal flight loop.

## Purpose

XVatsim should behave like a filtered, route-aware version of the controller
board the pilot can already reach through xPilot.

xPilot's board answers: what controllers are reachable now?

XVatsim answers: which of those reachable controllers are relevant to this
flight phase?

Everything else is fallback, not the engine.

## Contract Requirements

- `RefreshOverlayFromBrain()` is display-first and cheap: aircraft state,
  radios, session state, and UI snapshot work only.
- `RefreshOverlayFromBrain()` must not call route authority, airport coverage,
  enroute authority, arrival authority, transceiver geography, or broad
  controller proof directly.
- A clean `RadioBoardRuntimeV3` seam owns reachable-board diffing, phase gating,
  last-proven phase boards, and verifier requests.
- `RadioBoardSnapshot` owns the reachable controller list: callsign,
  frequency, facility group, source, and stable hash.
- `RadioBoardDiff` decides whether the reachable board actually changed.
- The phase filter decides which changed controller groups matter now.
- Authority verification receives only radio-board-driven, phase-relevant
  candidates.
- Heavy authority fallback is single-job, scheduled, logged, and never
  self-triggered from display code.
- Diagnostics must prove idle behavior with an explicit
  `board-unchanged-no-authority-work` record.
- Any old route, authority, or world scan firing during unchanged board state is
  a contract failure.

## Normal Runtime

1. Build the active flight map from the filed flight plan.
2. Build or consume a radio-reachable controller board.
3. If the board hash and phase are unchanged, do no authority work.
4. If the board changes, phase-gate the reachable list.
5. Verify only the small gated reachable list against the active flight map.
6. Publish the last proven phase board to the UI.
7. Keep heavy proof and source refreshes outside ordinary display refresh.

## Quarantine Rule

Engineer 1 string-only authority and Engineer 2 broad authority/world scans may
remain in the codebase during transition, but the normal flight loop must not
call them directly.

The only permitted entry point is a brain-scheduled fallback for a changed,
phase-relevant radio-board candidate that cannot be resolved cheaply.

The following live-loop calls are explicitly forbidden after the reset unless
they are behind the `RadioBoardRuntimeV3` one-shot verifier API:

- `ResolveBrainScheduledAuthorityVerification` without an explicit
  brain-scheduled reason
- `ResolveAirportCoverage`
- `CollectEnrouteBoardCached`
- `CollectArrivalBoardCached`
- route movement driven full authority refresh
- shadow scheduler work that performs or summarizes heavy authority proof
- any full controller feed proof when the radio board is unchanged

## Performance Rule

Unchanged radio board state must behave like idle:

- no route rebuild
- no airport coverage rebuild
- no authority proof
- no broad controller feed proof
- no world polygon scan

The UI may continue to update aircraft/radio/display fields while the authority
state remains the last proven safe snapshot.

## Live Proof

Diagnostics for healthy idle should show:

- `RadioBoardRuntime` with `board-unchanged-no-authority-work`
- no `AuthorityRelevance` proof duration
- no `RouteResolve` rebuild
- no `DepartureAirportCoverage` or `ArrivalAirportCoverage` rebuild unless the
  phase/route/radio board changed

## Completion Criteria

- Idle connected state has low microsecond flight-loop cost.
- Adding/removing a reachable controller triggers exactly one controlled
  relevance update.
- No reachable-board change means no authority verification.
- PluginAdmin steady-state flight-loop cost must remain close to the cheap
  radio/UI path and must not show persistent millisecond-scale XVatsim load.
- The live UI still displays departure, enroute, and arrival controllers
  correctly.
- Five consecutive live battle tests pass before streamer handoff.
- Ten consecutive live battle tests pass before final legacy audit/store build.
