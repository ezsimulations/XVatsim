# XVatsim V2 — Step 4 Plugin Suspend/Resume Flight-Context Preservation Engineering Brief

## Defect

Plugin Admin disable and enable both used the cold-dark/session-reset helper.
Although that helper initially retained accessory, METAR, flight-context, and
sampled state, its final presentation reset erased the flight context, workflow
stage, sampled xPilot identity, and recovery tracking. A valid Enroute KSAN
primary therefore became semantically ineligible and the rail truthfully fell
back to neutral after re-enable.

The unchanged-source production-seam reproduction began with accepted KSAN IFR
state and exact publication accounting, stopped the worker, then observed
`context=0`, `stage=NONE`, `primary=UNAVAILABLE`, and a neutral rail without any
Reset or Recover action.

## Correction

The Brain now owns one event-latched Plugin Admin lifecycle boundary:

- `SuspendBrainOwnedRuntimeForPluginAdmin()` closes the active accessory
  drawer, advances accessory and METAR lifecycle authority, cooperatively
  cancels/drains the existing METAR worker, rejects a drained late result, and
  marks the authoritative runtime suspended in place.
- `ResumeBrainOwnedRuntimeFromPluginAdmin()` re-enables the retained runtime
  without copying flight state into a shadow object or creating another
  scheduler, worker, queue, timer, or recovery path.
- The plugin stops its flight loop, input, UI, worker, VATSIM feed client, and
  mechanical links while disabled. It no longer invokes the cold-dark reset.
- On enable, the plugin samples the current xPilot identity first and applies
  the existing callsign boundary when necessary. It then resumes the retained
  Brain state, recreates mechanical presentation resources, and projects one
  coherent current snapshot.
- A fresh retained observation honors the existing next-eligible deadline. A
  due observation produces one ordinary `PrimaryRefresh`; no fabricated
  immediate request is introduced.

## State survival

| State | Plugin Admin suspend/resume | Reset XVatsim / confirmed new flight |
|---|---|---|
| Flight context and workflow stage | Preserved | Cleared by existing hard boundary |
| Normalized callsign/session identity | Preserved, then validated on enable | Replaced by existing callsign/new-flight boundary |
| Accepted METAR, history, primary, cadence | Preserved | Cleared by existing METAR hard boundary |
| Active drawer | Closed on suspend; newest retained content available on later selection | Cleared |
| In-flight METAR work | Cancelled/drained; late result rejected | Cancelled/drained and accepted state cleared |
| Pending text and click input | Discarded | Discarded |
| Publication ordering | New lifecycle; old facts cannot enter resumed epoch | New lifecycle and cleared state |
| Overlay/preparation resources | Stopped and recreated mechanically | Stopped and recreated |
| Disabled recurring work | Zero | Not applicable |

## Call graphs

Before:

```text
XPluginDisable
  -> stop flight loop / hide UI / drain publication
  -> DisableBrainOwnedAccessoryRuntime
  -> ApplyBrainOwnedAsyncWorkerLifecycleBoundary(false)
  -> ResetPluginRuntimeState(... preserveAccessory=true)
     -> ResetBrainOwnedRuntimeCachePreservingFlightContext
     -> ResetPresentationStateForColdDark
        -> clear workflow, flight context, sampled facts, xPilot tracking

XPluginEnable
  -> ResetPluginRuntimeState(... preserveAccessory=true)
  -> EnableBrainOwnedAccessoryRuntime
  -> create/synchronize presentation
```

After:

```text
XPluginDisable
  -> stop flight loop / hide UI / drain publication / discard incomplete input
  -> SuspendBrainOwnedRuntimeForPluginAdmin
     -> DisableBrainOwnedAccessoryRuntime
     -> ApplyBrainOwnedAsyncWorkerLifecycleBoundary(false)
  -> stop VATSIM feed client and mechanical plan/transceiver links

XPluginEnable
  -> poll current xPilot identity
  -> existing HandleXPilotSessionBoundary (same/callsign-change authority)
  -> ResumeBrainOwnedRuntimeFromPluginAdmin
  -> recreate/synchronize one current presentation
  -> restart flight loop
```

Reset XVatsim, Recover Current Flight, cold-dark, confirmed-new-flight,
callsign-change, METAR transport/parser/history, overlay publication, viewport,
and targeting behavior were not redesigned or weakened.
