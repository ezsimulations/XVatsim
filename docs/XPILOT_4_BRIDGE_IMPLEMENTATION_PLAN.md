# xPilot 4 Bridge Implementation Plan

**Contract:** `XPILOT4-BRIDGE-01`
**Baseline:** `v2.0.2` / `b1f968f9854223b99f22e1c8f9f5fdf9bc170b51`
**Worktree branch:** `xpilot4-bridge`
**Status:** Approved gate implementation; no install, commit, merge, or release authorized

## Design boundary

The implementation adds an optional xPilot 4 read-only companion and local
named-pipe transport. It does not replace or modify the xPilot 3 log reader.

The companion and XVatsim transport client are workers. They copy, sequence,
frame, transport, and report facts. A new Brain-owned runtime performs source
selection, flight binding, message admission, cross-source correlation,
controller evidence interpretation, snapshot recovery, and loss response.

xPilot 4 controller observations are from the same FSD evidence family as the
VATSIM feed. They will appear in controller decision receipts as neutral
correlation evidence and will not add a second independent positive vote.

## Exact touched-file manifest

### New design and build files

- `docs/XPILOT_4_BRIDGE_CONTRACT_GATE.md` — approved governing contract and rollback boundary carried into the isolated worktree.
- `docs/XPILOT_4_BRIDGE_IMPLEMENTATION_PLAN.md` — this manifest and predeclared limits.
- `docs/XPILOT_3_4_COMPATIBILITY_NOTE.md` — preserved compatibility research and live finding.
- `docs/XPILOT_4_SDK_TECHNICAL_QUESTIONNAIRE.md` — preserved maintainer questionnaire that established the SDK facts.
- `integrations/xpilot4_bridge/README.md` — build, install, remove, and independent-project wording.
- `integrations/xpilot4_bridge/XVatsim.XPilot4Bridge.csproj` — `.NET 10` companion project with a non-copying SDK project reference.
- `integrations/xpilot4_bridge/BridgeProtocol.cs` — versioned binary protocol and mechanical chunking.
- `integrations/xpilot4_bridge/BoundedObservationQueue.cs` — content-blind bounded FIFO and explicit capacity-loss reporting.
- `integrations/xpilot4_bridge/BridgeTransport.cs` — current-user-only named-pipe server and background transport.
- `integrations/xpilot4_bridge/XVatsimXPilot4Bridge.cs` — read-only SDK subscriptions and exact raw event capture.
- `integrations/xpilot4_bridge/AssemblyInfo.cs` — probe-only internal access.
- `scripts/build_xpilot4_bridge.ps1` — pinned official SDK acquisition and companion/probe build.
- `tools/xpilot4_bridge_probe/XVatsim.XPilot4Bridge.Probe.csproj` — dependency-free offline companion probe.
- `tools/xpilot4_bridge_probe/Program.cs` — fake-broker, protocol, lifecycle, privacy, capacity, and handler-time checks.

### New Brain files

- `brain/include/XVatsim/brain/BrainXPilot4BridgeTypes.h` — immutable observations, commands, controller snapshots, and transport interface.
- `brain/include/XVatsim/brain/BrainXPilot4BridgeRuntime.h` — Brain-owned state and service contract.
- `brain/src/BrainXPilot4BridgeRuntime.cpp` — sole xPilot 4 source, message, snapshot, correlation, recovery, and admission decisions.

### New XVatsim worker files

- `modules/runtime_workers/include/XVatsim/modules/runtime_workers/XPilot4BridgeClient.h` — removable local transport worker interface and health metrics.
- `modules/runtime_workers/src/XPilot4BridgeClient.cpp` — asynchronous Windows named-pipe client, decoder, bounded mailboxes, and explicit loss facts.

### New regression files

- `tools/regression_harness/src/XPilot4BridgeContractProbe.h` — focused probe entry point.
- `tools/regression_harness/src/XPilot4BridgeContractProbe.cpp` — real named-pipe, Brain, dual-source, controller-receipt, lifecycle, and performance proof.

### Existing files allowed to change

- `.gitignore` — ignore only companion/probe `bin` and `obj` output.
- `brain/CMakeLists.txt` — compile the new Brain runtime.
- `brain/include/XVatsim/brain/BrainPdcRuntime.h` — distinguish xPilot 3 and xPilot 4 availability and record source identity.
- `brain/src/BrainPdcRuntime.cpp` — Brain-owned aggregate source availability and cross-source duplicate decision.
- `brain/src/BrainPdcLogRuntime.cpp` — report legacy log availability to the Brain without overriding xPilot 4 availability.
- `brain/include/XVatsim/brain/BrainOwnedRuntime.h` — contain the new Brain-owned runtime and cache identity.
- `brain/src/BrainOwnedRuntime.cpp` — preserve the current xPilot 4 transport
  session and Brain-owned pending startup observations across the initial
  confirmed-flight cache reset; hard session and process boundaries still
  clear them.
- `brain/include/XVatsim/brain/BrainOwnedWorkerTypes.h` — carry the Brain-owned correlated controller snapshot into candidate evaluation.
- `brain/src/BrainControllerRelevanceWorker.cpp` — include xPilot 4 evidence identity in the Brain cache key.
- `brain/src/BrainControllerEvidencePipeline.cpp` — append a Brain-created neutral correlation receipt for each candidate.
- `modules/runtime_workers/CMakeLists.txt` — compile the transport worker.
- `modules/runtime_workers/include/XVatsim/modules/runtime_workers/AsyncFactWorkerHost.h` — expose generic worker lifecycle and the xPilot 4 binding.
- `modules/runtime_workers/src/AsyncFactWorkerHost.cpp` — own and start/stop the removable transport worker.
- `plugin/src/XVatsimPlugin.cpp` — start/stop the generic worker host and pass xPilot 4 observations to the Brain through the existing PDC source service seam.
- `tools/regression_harness/CMakeLists.txt` — compile the focused probe.
- `tools/regression_harness/src/main.cpp` — expose the focused probe command.

No other file may change without an amendment to this plan.

## `XVatsimPlugin.cpp` justification

The plugin shell is touched only for host lifecycle and fact transport:

- start and stop the generic asynchronous worker host;
- provide the current immutable flight context to the Brain service; and
- log bounded state-change summaries returned by the Brain.

It will contain no API-version decision, message classifier, controller match,
vote, source selection, snapshot policy, PDC admission, unread logic, or UI
decision.

## Legacy preservation

The following are frozen:

- xPilot 3 `NetworkLogs` discovery, parsing, cadence, and worker;
- controller feed and radio-board acquisition;
- polygon and route geometry;
- controller UI construction and rendering;
- Orb/Drawer UI mechanics; and
- V2.0.2 release artifacts and installed plugin.

The only legacy PDC changes are Brain-owned source aggregation and Brain-owned
cross-source duplicate handling required for simultaneous xPilot 3/xPilot 4
evidence.

## Predeclared mechanical limits

- Companion observation queue: `512` complete observations.
- XVatsim incoming observation queue: `1024` complete observations.
- XVatsim outgoing command queue: `8` commands.
- Protocol frame payload: `256 KiB`; larger observations are chunked.
- Maximum reassembled observation: `4 MiB`; exceeding it produces an explicit loss/corruption fact.
- Brain harvest per existing refresh callback: at most `8` observations.
- Brain pending private-message evidence: at most `64` exact observations for
  at most `5 minutes`, scoped to one xPilot process and connection epoch. This
  queue exists only to bridge XVatsim flight-context startup; the Brain makes
  no admission or display decision until the flight identity is ready.
- Pipe reconnect cadence while absent: no faster than once per second.
- No new X-Plane flight-loop callback.
- No persistent private-message storage.
- Controller snapshot maximum retained entries: `4096`; excess is an explicit incomplete-snapshot fact.

## Predeclared performance pass limits

Measured on this development computer in Release configuration:

- xPilot SDK event-handler `p99 <= 500 microseconds` and `maximum <= 5 milliseconds` during the probe burst.
- Companion bounded-queue burst of `20,000` events completes in `<= 2 seconds` wall time.
- XVatsim `TryHarvest` `p99 <= 200 microseconds` and `maximum <= 2 milliseconds`.
- Brain service of a normal single observation completes in `<= 2 milliseconds`.
- Idle named-pipe client consumes `<= 25 milliseconds` process CPU during a `3 second` observation window.
- Queue, thread, handle, and retained-memory counts return to their declared steady state after disconnect/reconnect cycles.
- Existing performance probes and the complete scenario suite do not regress.

These limits may not be relaxed after a failure without Product Owner review.

## Required offline evidence

1. Build against the exact official xPilot Plugin SDK source shipped by xPilot
   `4.0.0-beta.7` at commit `3f3472f` without copying
   `xPilot.PluginSdk.dll` into the package. The companion must use beta 7's
   retained runtime surface and a native current-user pipe transport because
   beta 7 trims unused framework APIs from its self-contained executable.
2. Companion fake-broker probe proves API gating, read-only subscriptions,
   exact Unicode/multiline messages, controller events, complete snapshots,
   reconnect epochs, cleanup, capacity loss, chunking, and handler timing.
3. C++ named-pipe probe proves handshake, frame decoding, snapshot command,
   reconnect, corruption/loss reporting, fixed bounds, and idle CPU.
4. Brain probe proves flight/callsign binding, deferred startup-message
   admission after flight context arrives, survival across the production
   confirmed-flight cache reset, disconnect/process invalidation, bounded
   expiry/loss accounting, xPilot 3/4 correlation, unread behavior, snapshot
   recovery, and controller evidence receipts.
5. Static audit finds no xPilot send/control call in the companion.
6. Release C++ build and complete regression suite pass.
7. Existing Australia and PDC log-monitor focused probes pass unchanged.
8. Installed X-Plane and xPilot files remain untouched.
