# xPilot 4 Bridge Contract Gate

**Gate ID:** `XPILOT4-BRIDGE-01`
**Status:** Approved by Product Owner on 2026-09-26
**Baseline:** XVatsim `v2.0.2` at `b1f968f9854223b99f22e1c8f9f5fdf9bc170b51`
**Scope:** Design and future isolated implementation of an optional xPilot 4 companion bridge
**Release status:** Experimental until the release conditions in this gate are satisfied

## 1. Purpose

The xPilot 4 bridge will give XVatsim direct, read-only access to facts exposed by the xPilot 4 Plugin SDK. The initial facts are:

- incoming private-message events, including messages that may later be classified as PDC/ACARS;
- xPilot connection state and callsign;
- the current raw controller snapshot; and
- raw controller add, delete, frequency-change, and location-change events.

The bridge is an additional sensor. It is not a second decision engine. It may capture, sequence, cache for transport, and deliver facts. It may never determine what XVatsim displays or suppresses.

The existing xPilot 3 log-monitor path remains intact. XVatsim must continue to operate with xPilot 3 when the xPilot 4 companion is absent.

## 2. Governing Bible Law

The following rule is absolute and takes precedence over implementation convenience:

> The Brain is the only decision maker. A worker may obtain and report facts requested or needed by the Brain, but it may not suppress, filter, classify, score, prioritize, change, or act on those facts before the Brain receives them.

Every implementation choice must preserve this boundary:

```text
xPilot 4 SDK
    |
    v
Companion capture and transport worker
    |  raw, ordered, source-labelled facts
    v
XVatsim acquisition worker
    |  immutable observations and explicit health/loss facts
    v
Brain
    |  classification, correlation, votes, decisions, state
    v
UI and other Brain-directed effects
```

No path may bypass the Brain from the xPilot SDK, companion, transport, acquisition worker, cache, or diagnostic system to the UI.

## 3. Authorization Boundary

This gate authorizes only:

1. review and refinement of this design;
2. a future isolated implementation based on the approved design;
3. test fixtures and probes needed to prove the contract; and
4. an experimental xPilot 4 companion package that is kept separate from the stable XVatsim release until this gate passes.

This gate does not authorize:

- changing production code during creation of this document;
- changing the existing xPilot 3 log-monitor behavior;
- installing an experimental build into the Product Owner's live simulator without explicit authorization;
- committing, merging, pushing, packaging, or releasing an implementation without explicit authorization;
- connecting the companion directly to the VATSIM network;
- sending any message, radio command, transponder command, connection command, or other command through xPilot; or
- modifying or forking xPilot.

## 4. Confirmed xPilot 4 SDK Constraints

The design is based on the xPilot 4 SDK questionnaire and the maintainer's responses supplied on 2026-09-26. These are treated as interface facts, not as authority to change XVatsim's architecture.

- The companion targets `.NET 10.0`.
- The companion must inspect `IBroker.ApiVersion` at the beginning of initialization and must not subscribe to unavailable interfaces.
- A breaking beta may change the assembly version and `ApiVersion`.
- xPilot initializes a plugin once per plugin type and process. A normal plugin reload is not available without restarting xPilot.
- `SessionEnded` is the SDK shutdown signal and must be used to unsubscribe and release resources.
- SDK events run on the xPilot UI thread. Event handlers must return immediately.
- xPilot isolates ordinary plugin exceptions, but an in-process plugin can still block the UI or consume excessive memory.
- `PrivateMessageReceived` reports each incoming private message that reaches the xPilot UI, including PDC messages.
- The SDK supplies the raw sender and raw message. It does not classify PDC, ACARS, automated, or ordinary private messages.
- Event order is preserved within a connection. The SDK does not deduplicate or replay events.
- A process start, connection, reconnect, and plugin restart must be treated as new evidence epochs.
- `IBroker.GetControllers()` returns the current raw controller snapshot while connected.
- Controller events and snapshots include callsign, frequency in integer hertz, latitude, and longitude.
- The SDK does not supply controller aliases, role, range, sector relevance, or the UI grouping used by xPilot.
- xPilot does not apply range or relevance filtering to the controller list before exposing it to the SDK.
- A zero latitude or longitude value can be either unknown or valid. The bridge may not discard it or infer its meaning.
- The companion is loaded from its own `Plugins/<Name>/<Name>.dll` directory and must not ship the xPilot SDK assembly.
- Plugins have separate assembly-load contexts, but share xPilot's process, memory, CPU, and UI thread.

Any future SDK statement that conflicts with this section requires a gate amendment before the affected interface is used.

## 5. Bridge Role

The bridge consists of two mechanical sides:

1. **xPilot 4 companion** — subscribes to supported read-only SDK events, copies their exact values into a versioned transport envelope, and exposes requested snapshots.
2. **XVatsim acquisition worker** — receives envelopes away from the X-Plane flight loop and presents immutable observations to the Brain.

Both sides are workers. Neither side owns product policy.

### 5.1 Permitted worker actions

Workers may only:

- perform an SDK/API compatibility check;
- subscribe and unsubscribe to supported read-only SDK events;
- copy exact source values without semantic alteration;
- attach source identity, version, epoch, sequence, receipt time, and transport health metadata;
- preserve source event order;
- split and reassemble large payloads mechanically without changing their content;
- place observations in a bounded first-in/first-out transport queue;
- answer a Brain request for the entire current raw controller snapshot;
- report connection, disconnection, compatibility, transport, corruption, timeout, and capacity facts;
- perform framing, encoding, checksum, and protocol validation;
- retry a local transport connection using a fixed mechanical schedule; and
- aggregate numeric performance and health counters that contain no message text.

These actions do not carry a vote and do not imply display eligibility.

### 5.2 Forbidden worker actions

No worker, bridge, adapter, cache, or transport layer may:

- classify a message as PDC, ACARS, CPDLC, automated, private chat, general chat, or displayable;
- discard a message because of its sender, text, capitalization, punctuation, prefix, suffix, or presumed type;
- decide whether a message belongs in the PDC Orb or Drawer;
- calculate, set, clear, or acknowledge unread state;
- deduplicate, merge, prioritize, reorder, or replace product message history;
- infer a controller role from a callsign;
- reject or hide a controller because its callsign is unexpected;
- reject or hide a controller because its latitude, longitude, or frequency appears invalid;
- decide controller range, relevance, airport association, sector ownership, current/next status, or polygon coverage;
- assign a `+1`, `0`, or `-1` vote;
- decide which of the xPilot 3 or xPilot 4 sources is authoritative;
- hide one source merely because the other source is present;
- change controller rows, colors, distance, `Standby`, COM state, Orbs, Drawers, or any other UI state;
- send a message or command to xPilot or VATSIM;
- write raw private-message bodies to diagnostic logs; or
- silently discard, truncate, or overwrite evidence.

A code path that performs any forbidden action fails this gate even when the observed UI result appears correct.

## 6. Observation Contract Presented to the Brain

Every item delivered to the Brain must be an immutable observation. The schema may evolve, but it must preserve these concepts:

| Field | Meaning |
|---|---|
| `source_kind` | `xpilot3_network_log` or `xpilot4_plugin_sdk` |
| `source_family` | Identifies both paths as correlated xPilot/FSD evidence |
| `client_version` | Observed xPilot version when available |
| `companion_version` | xPilot 4 companion version |
| `sdk_api_version` | Observed xPilot SDK API version |
| `transport_protocol_version` | Local bridge protocol version |
| `process_epoch` | Changes when xPilot restarts |
| `connection_epoch` | Changes on each xPilot connect or reconnect |
| `source_sequence` | Monotonic sequence within an epoch |
| `received_at` | Local receipt time; never represented as network event time |
| `event_kind` | The raw SDK/log event represented by the observation |
| `raw_fields` | Exact sender, message, controller, frequency, and coordinate values supplied by the source |
| `completeness` | Mechanical statement about complete, chunked, corrupt, or lost transport data |
| `health` | Mechanical compatibility, connection, queue, and transport facts |

Allowed event kinds initially include:

- `SourceAvailable`
- `SourceUnavailable`
- `ConnectionStateChanged`
- `CallsignObserved`
- `IncomingPrivateMessage`
- `ControllerSnapshotStarted`
- `ControllerSnapshotEntry`
- `ControllerSnapshotCompleted`
- `ControllerAdded`
- `ControllerDeleted`
- `ControllerFrequencyChanged`
- `ControllerLocationChanged`
- `TransportGap`
- `CapacityLoss`
- `CorruptEnvelope`
- `SessionEnded`

An observation must never include product-policy fields such as:

- `display`
- `hide`
- `is_pdc`
- `is_relevant`
- `is_current_controller`
- `controller_role`
- `confidence`
- `vote`
- `priority`
- `unread`
- `acknowledged`

Those fields represent decisions and therefore belong only to the Brain's state and decision receipts.

## 7. Decisions Reserved Exclusively for the Brain

Only the Brain may:

- decide whether an available source is eligible for a given purpose;
- bind evidence to the current aircraft, callsign, flight, simulator session, or connection epoch;
- classify incoming text as PDC/ACARS, ordinary private message, general chat, or excluded traffic;
- admit or reject a message from the PDC Orb and Drawer;
- deduplicate correlated observations from xPilot 3 and xPilot 4;
- order and bound product message history;
- set unread state and clear it after the pilot reads or closes the Drawer;
- determine the PDC Orb label and color;
- correlate controller data with VATSIM, VATSpy, vNAS, airport, frequency, route, geometry, and polygon evidence;
- determine whether two observations belong to the same evidence family;
- assign every `+1`, `0`, or `-1` vote;
- calculate the total vote count and retain an inspectable decision receipt;
- decide controller role, airport association, sector coverage, current/next state, color, distance, `Standby`, and display order;
- react to lost, incomplete, corrupt, stale, or incompatible evidence;
- request a full controller snapshot or another mechanical fact from a worker; and
- decide whether a source reset invalidates or preserves existing Brain-owned state.

When the Brain requests a snapshot, the worker must return the complete source snapshot without relevance or syntax filtering. The Brain may then determine how each entry participates in a decision.

## 8. Voting and Evidence Independence

The xPilot 4 bridge supplies facts, not votes.

- A worker must never convert an SDK event into `+1`, `0`, or `-1`.
- The Brain must own all scoring rules.
- The Brain's decision receipt must identify the originating observation for each score.
- The receipt must show positive, neutral, and negative inputs, including facts the Brain declined to use.
- xPilot 3 logs, xPilot 4 SDK events, and the public VATSIM feed can originate from the same FSD information. The Brain must label and correlate that shared evidence family so the same fact is not counted as multiple independent votes.
- xPilot 4 controller presence can strengthen timeliness or confirm a raw observation, but it is not automatically an additional independent `+1`.
- A close textual controller-call-sign match remains a fact presented to the Brain. The comparison worker may report component matches only if the Brain explicitly requests that mechanical comparison. The Brain alone converts that result into a score.

For every controller displayed during testing, diagnostics must make the Brain's complete score receipt available without exposing private-message bodies.

## 9. Message Path Contract

### 9.1 Capture

The companion must forward every `PrivateMessageReceived` event it receives. PDC/ACARS and ordinary private messages use the same raw event type because xPilot does not classify them.

The companion may not search for PDC phrases, sender patterns, control characters, route content, or callsign patterns. It may not make a display decision.

### 9.2 Brain admission

The Brain applies the existing message-admission policy after the raw observation arrives. This includes:

- recognizing incoming direct messages;
- excluding radio, broadcast, server, outgoing, and general-chat traffic;
- classifying PDC/ACARS when supported by Brain-owned evidence;
- deduplicating repeated evidence;
- placing admitted messages newest-first in the Drawer;
- setting the Orb to amber `NEW` when unread content exists; and
- returning the Orb to idle after the Brain receives the Drawer-read/close action.

The xPilot 4 path must converge on the same Brain-owned state and UI behavior as the xPilot 3 path.

### 9.3 Transport cache

The companion may retain a bounded in-memory first-in/first-out queue only to survive short local transport interruptions. This queue is not the product message history.

- It must preserve exact content and order.
- It must not deduplicate or replace messages.
- It must not persist private-message bodies to disk.
- It must be cleared on normal `SessionEnded` after final health delivery is attempted.
- If capacity is exceeded, the fixed capacity policy must be content-blind and the exact loss count and sequence range must be reported to the Brain as soon as transport resumes.
- A transport loss is evidence. It may never be represented as a clean or empty result.

## 10. Controller Path Contract

The companion may forward the raw controller snapshot and controller events exposed by xPilot 4. These facts are additional evidence for the existing Brain; they are not a replacement controller engine.

The bridge must preserve:

- the exact controller callsign;
- integer frequency in hertz;
- latitude and longitude exactly as supplied;
- event type;
- event order within the connection epoch; and
- snapshot boundaries and completeness.

The bridge must not:

- group controllers by callsign suffix;
- normalize `ML_TWR` into another callsign;
- require an exact expected suffix;
- map a controller to an airport or facility;
- calculate distance from an airport or aircraft;
- apply a five-mile rule;
- determine polygon inclusion or crossing distance;
- select the current or next controller; or
- decide green, orange, hidden, `Standby`, or any other UI state.

If a coordinate is `0`, the bridge reports `0`. If a field is absent, the bridge reports absence. The Brain decides whether that observation is neutral, usable with other evidence, or evidence against a hypothesis.

## 11. Local Transport Design

The preferred transport is a versioned local named pipe between the xPilot 4 companion process and XVatsim. This avoids durable private-message files and permits the xPilot event handler to hand work to a background transport thread immediately.

The protocol must provide:

- an explicit protocol version and capability handshake;
- process and connection epochs;
- ordered source sequence numbers;
- length-delimited frames;
- checksums or equivalent corruption detection;
- exact UTF-8 round-trip behavior for .NET strings;
- mechanical chunking rather than truncation;
- bounded queue and frame limits;
- explicit gap, overflow, corruption, and disconnect observations;
- no network listener and no remote binding;
- access limited to the local user/session where supported; and
- no raw message text in protocol diagnostics.

The exact pipe name, security descriptor, frame sizes, queue capacity, and retry timings must be documented in the implementation proposal before code is written. Changing from an in-memory/named-pipe design to a persistent file or network transport requires a gate amendment.

## 12. Threading and Performance Contract

xPilot SDK event handlers execute on the xPilot UI thread. Each handler must:

1. copy the event's exact primitive/string fields;
2. assign the next epoch sequence number;
3. enqueue the immutable envelope without waiting for I/O; and
4. return.

An SDK event handler must not:

- open or write files;
- perform pipe I/O that can block;
- parse message text;
- run controller correlation or geometry;
- call XVatsim Brain logic;
- allocate unbounded storage;
- wait on X-Plane or another process; or
- hold a contended lock.

A background companion worker performs local transport. An XVatsim-side worker receives and validates envelopes outside the X-Plane flight loop. It posts bounded observations to the Brain through the established runtime boundary.

The bridge must be event-driven. It must not add a high-frequency X-Plane flight-loop callback. The existing low-rate xPilot 3 log monitor remains unchanged.

Performance evidence must include:

- xPilot UI-thread handler time distribution and maximum;
- transport queue depth and high-water mark;
- transport reconnect count;
- overflow and sequence-gap counts;
- XVatsim worker time;
- Brain ingestion time;
- X-Plane flight-loop timing before and after enabling the bridge; and
- memory growth over a full flight and reconnect cycle.

The numerical pass limits must be set before live testing and may not be relaxed after seeing a failure without Product Owner review.

## 13. Version and Capability Gate

The companion must use an explicit supported `IBroker.ApiVersion` set or range.

On an unsupported API version:

1. the companion records one bounded compatibility diagnostic;
2. it does not subscribe to unavailable SDK interfaces;
3. it exposes `SourceUnavailable` with the observed API version through the safe base transport when possible;
4. it performs no product fallback decision; and
5. XVatsim's Brain decides whether the xPilot 3 source or other evidence remains usable.

Missing methods, type-load errors, and initialization failures must result in an unavailable-source fact rather than guessed compatibility. The companion must fail closed with respect to SDK calls while leaving xPilot and XVatsim operational.

The companion package must:

- live only in its own xPilot plugin directory;
- include its own version in `IPlugin.Name` or equivalent load identity;
- exclude the xPilot Plugin SDK assembly;
- avoid changing any other xPilot plugin or file;
- be installed only while xPilot is closed; and
- support removal by deleting only its own directory.

## 14. xPilot 3 and xPilot 4 Coexistence

XVatsim remains compatible with both generations:

| Pilot client | Evidence path | Required companion |
|---|---|---|
| xPilot 3.0.2 | Existing `NetworkLogs` low-rate monitor | No |
| xPilot 4 | xPilot 4 Plugin SDK through local bridge | Yes |

No xPilot 3 companion will be created under this gate.

The xPilot 4 implementation must be additive:

- no existing xPilot 3 parser, cadence, cache, Brain decision, Orb behavior, or Drawer behavior may be changed merely to add the xPilot 4 source;
- the Brain receives explicit availability facts for both sources;
- when both sources exist, both observations reach the Brain with their source identity intact;
- only the Brain may select, correlate, deduplicate, or reject either source;
- loss of the xPilot 4 companion must not disable the xPilot 3 path; and
- absence of xPilot 3 log files must not cause a worker to assume xPilot 4 evidence is valid or authoritative.

## 15. Diagnostics and Privacy

Diagnostics must be sufficient to diagnose missed events, CPU spikes, loops, reconnects, and Brain decisions without creating large files or exposing message content.

Allowed diagnostic data includes:

- versions and capabilities;
- epochs and sequence numbers;
- event kind;
- message character/byte length;
- redacted or one-way correlation identifier when needed for duplicate analysis;
- queue depth and high-water mark;
- processing duration;
- transport state;
- loss/corruption counts;
- controller fields needed for a Brain decision receipt; and
- Brain scores and final decisions.

Diagnostics must not include:

- private-message or PDC bodies;
- credentials, tokens, or connection secrets;
- unbounded per-frame success logs;
- repeated compatibility errors every loop; or
- raw payload dumps.

Routine health data must be aggregated and rate-limited. A short diagnostic burst may be enabled deliberately for a live test, but must have a fixed duration and automatic return to normal logging.

## 16. Implementation Change Boundary

Before the first implementation edit, the implementation proposal must list every file to be added or changed and map each file to one of these roles:

- xPilot 4 companion SDK boundary;
- local transport protocol;
- XVatsim acquisition worker;
- Brain observation type;
- Brain message decision path;
- Brain controller evidence path;
- Brain decision receipt/diagnostics;
- build/package configuration; or
- contract verification.

No file outside that manifest may be changed without amending the proposal. In particular, unrelated controller selection, polygon geometry, frequency matching, UI layout, COM control, CTAF, ATIS, METAR, and xPilot 3 log code remain out of scope.

The implementation should begin in an isolated Git worktree or branch created from the exact `v2.0.2` baseline. The stable baseline must remain recoverable throughout the experiment.

## 17. Required Verification

All evidence below is required before the bridge can be considered for a release.

### 17.1 Architecture and static checks

- No dependency from companion or acquisition worker to UI code.
- No decision field in the worker observation contract.
- No message-text or controller-callsign classifier in a worker.
- No score assignment outside the Brain.
- No calls to xPilot send, tune, transponder, connect, disconnect, or network-control APIs.
- No raw private-message diagnostic output.
- No shipped xPilot SDK assembly.
- Exact touched-file manifest matches the approved implementation proposal.

### 17.2 Raw message tests

- Ordinary private message is delivered exactly once with exact sender and text.
- PDC-formatted message is delivered through the same raw path with no worker classification.
- Automated sender is delivered unchanged.
- Empty, Unicode, multiline, and maximum tested payloads round-trip exactly.
- Source ordering is preserved.
- Reconnect creates a new epoch.
- Duplicate source events remain separate observations until the Brain evaluates them.
- Transport interruption preserves queued order or reports every lost sequence explicitly.

### 17.3 Brain message tests

- Existing xPilot 3 admission and exclusion behavior remains unchanged.
- The same eligible message produces the same Brain state from either source.
- General/radio/broadcast/server/outgoing traffic does not enter the Drawer because of a Brain decision.
- New admitted content makes the Orb amber and displays `NEW`.
- Opening/reading and closing the Drawer clears unread state through the Brain and returns the Orb to idle.
- Newest admitted messages appear first.
- Duplicate xPilot 3/xPilot 4 evidence is correlated in the Brain rather than counted twice.
- A private-message event received before flight identity is ready survives the
  Brain's initial confirmed-flight cache reset and is evaluated immediately
  after that identity becomes ready.
- Pending private-message evidence is still cleared on xPilot disconnect,
  process replacement, or connection-epoch change.
- Decision diagnostics name the source observations and Brain rule without logging the body.

### 17.4 Raw controller tests

- Complete controller snapshot reaches the Brain with explicit start/end boundaries.
- Add, delete, frequency-change, and location-change events preserve values and order.
- Unexpected callsigns such as `ML_TWR` are delivered unchanged.
- Zero and absent coordinates are delivered rather than removed.
- No range, airport, suffix, role, or relevance filtering occurs before the Brain.
- Snapshot loss or incomplete transfer is explicit.

### 17.5 Brain controller tests

- A bridge observation alone cannot directly create, remove, recolor, or reorder a controller row.
- Every displayed controller has a complete Brain decision receipt.
- Receipts expose all available VATSIM, VATSpy, vNAS where applicable, distance, polygon/extension, and xPilot evidence considered by the Brain.
- Every contributing rule produces only `+1`, `0`, or `-1` in the Brain.
- Correlated xPilot/FSD observations are not counted as independent votes.
- Melbourne/Sydney extension scenarios remain correct.
- Current/next controller color and polygon-crossing distance remain correct.
- Flying away from a sector does not cause a worker observation to select it as next.

### 17.6 Lifecycle and failure tests

- xPilot starts before X-Plane.
- X-Plane starts before xPilot.
- XVatsim starts after the companion.
- Companion is absent.
- xPilot 4 is disconnected, connected, reconnected, and exited normally.
- X-Plane plugin is reloaded while xPilot remains active.
- Local pipe disconnects and reconnects.
- Unsupported `ApiVersion` is handled without subscription or crash.
- Required SDK member is absent.
- Companion throws during controlled test and xPilot remains responsive.
- Queue reaches its capacity and Brain receives an exact loss fact.
- `SessionEnded` unsubscribes handlers and stops background work.
- No callback, handle, thread, or memory growth remains after cycles.

### 17.7 Performance tests

- xPilot remains responsive during message and controller-event bursts.
- SDK event handlers remain within the predeclared time limit.
- XVatsim adds no high-frequency flight loop.
- Full-flight CPU, flight-loop time, memory, diagnostic growth, and queue depth remain within predeclared limits.
- There is no unbounded cache, retry loop, reconnect loop, or log loop.

### 17.8 Regression and live tests

- The complete XVatsim automated regression suite passes from a clean build.
- Release configuration builds successfully.
- Existing xPilot 3 PDC/private-message live test passes.
- xPilot 4 private-message and PDC live test passes.
- Controller behavior is verified on at least one US and one extension-dependent non-US scenario.
- A full-flight soak test passes with diagnostics reviewed.
- The Product Owner reviews the Brain vote receipts for live controller decisions.
- The Product Owner confirms Orb/Drawer behavior and plugin health.

## 18. Release Gate

The xPilot 4 bridge may not enter the stable XVatsim package until all of the following are true:

- this contract is approved by the Product Owner;
- the exact implementation file manifest and transport limits are approved;
- all verification in Section 17 passes;
- xPilot 3 compatibility has been live-tested again;
- the supported xPilot 4 API version is explicitly recorded;
- xPilot 4 has reached a sufficiently stable release or the Product Owner explicitly accepts a beta-only experimental package;
- installation and removal instructions are verified;
- the companion is clearly described as an independent XVatsim enhancement, not an xPilot replacement or official xPilot component;
- rollback has been rehearsed; and
- the Product Owner explicitly authorizes commit, merge, package, and release steps.

An xPilot beta update does not automatically expand the supported API set. Each new breaking API version must pass compatibility review and the affected verification tests.

## 19. Stop Conditions

Implementation or testing must stop and return to design review if:

- a worker needs to classify, filter, score, or suppress evidence to make the feature work;
- a path can change UI state without a Brain decision;
- the companion requires an xPilot send/control API;
- private-message content must be persisted to disk;
- the SDK cannot provide required facts without blocking its UI thread;
- transport loss cannot be made visible to the Brain;
- xPilot 3 behavior changes unexpectedly;
- controller voting cannot expose a complete Brain receipt;
- CPU, flight-loop, memory, or log growth exceeds the predeclared limit;
- the xPilot SDK API changes outside the supported version contract; or
- an implementation edit falls outside the approved file manifest.

The solution must not work around a stop condition by moving the decision to another worker.

## 20. Rollback Contract

The known-good rollback point is XVatsim `v2.0.2` at `b1f968f9854223b99f22e1c8f9f5fdf9bc170b51`.

Rollback must be possible by:

1. disabling the xPilot 4 source in Brain-owned configuration;
2. removing only the XVatsim xPilot 4 companion directory from xPilot;
3. rebuilding or reinstalling the unchanged `v2.0.2` XVatsim baseline; and
4. continuing to use the existing xPilot 3 log-monitor path without migration or repair.

The experiment must not rewrite existing xPilot 3 logs, XVatsim settings, controller datasets, message history formats, or release artifacts. No rollback may depend on recovering data transformed by a worker.

## 21. Gate Completion Evidence

When implementation is ready for review, the engineering report must provide:

- exact baseline and implementation commit hashes;
- complete touched-file manifest;
- a worker-versus-Brain responsibility audit;
- supported SDK/API/protocol versions;
- static forbidden-call and forbidden-decision checks;
- automated test totals;
- live xPilot 3 and xPilot 4 results;
- representative Brain controller vote receipts;
- Orb/Drawer state-transition evidence;
- lifecycle and failure results;
- performance and log-volume measurements;
- packaging contents, including proof that the SDK assembly is absent;
- installation and removal verification; and
- tested rollback steps.

No implementation passes because the display merely looks correct. It passes only when the evidence proves that raw facts reached the Brain, the Brain alone made the decisions, and the known-good V2.0.2 behavior remains recoverable.
