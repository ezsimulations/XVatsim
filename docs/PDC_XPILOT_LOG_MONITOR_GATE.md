# PDC xPilot Log Monitor Contract Gate

Gate ID: `PDC-XPILOT-LOG-01`

Baseline: XVatsim V2.0.1, commit
`e34baf1912a112812e85c1940c2031bb0b922a7c`, plus the isolated Australia
controller-evidence repair and diagnostic-volume repair.

Authorization: on 2026-09-15 the Product Owner directed implementation of a
low-frequency xPilot log monitor for incoming private and PDC/ACARS messages,
with the Brain as the only admission and presentation authority. The initial
gate did not authorize commit, installation, merge, push, release, or xPilot
modification. The Product Owner later requested the live installation and, on
17 September 2026, confirmed the full-flight test and authorized committing the
validated result.

## Defect being repaired

The V2.0.1 PDC runtime reads an optional xPilot private-message dataref tuple,
captures one message, and then closes acquisition. The ordinary installed
xPilot build does not provide that dependable history interface. As a result,
the ORB can remain idle and later private or PDC/ACARS messages cannot reach the
Drawer.

xPilot already records its network protocol in append-only files under
`%LOCALAPPDATA%/org.vatsim.xpilot/NetworkLogs`. The protocol distinguishes
direct text from radio, broadcast, wallop, and server traffic. The verified
working cadence from the discarded candidate was a five-second metadata check;
the Product Owner's live ground test reported near-immediate delivery at that
cadence. This gate retains those observed facts but does not copy the discarded
runtime implementation.

## Decision boundary

The Brain is the sole decision maker.

The file worker may only:

- discover mechanically valid `NetworkLog-YYYYMMDD-HHMMSS.txt` candidates;
- report file identity, size, modification, read offsets, and source errors;
- read only the bounded byte range requested by the Brain;
- parse protocol direction, packet kind, sender, recipient, channel marker,
  time, body bytes, completeness, and validation facts;
- report every requested parsed text record, including public radio records,
  without deciding whether it belongs in the product.

The worker must not classify a message as displayable, discard a valid record
because its text does not resemble a clearance, rank senders, acknowledge a
message, reorder history, set an ORB color, or suppress a record without an
explicit mechanical error/capacity fact.

The Brain alone decides:

- whether the active xPilot session and flight/callsign binding are valid;
- exclusion of incoming network-wide pilot login/logout packets so another
  pilot's `#AP`, `#AA`, or `#DP` record cannot replace the local session;
- which discovered log is current and which byte range to request;
- whether a parsed protocol record is an incoming direct message to the active
  pilot;
- exclusion of radio (`@frequency`), broadcast (`*`), wallop (`*s`), server,
  outgoing, other-session, and other-callsign records;
- deduplication, cache retention, newest-first Drawer order, unread state,
  acknowledgement, ORB text, and ORB color;
- source loss/recovery, log rotation/truncation, retry cadence, and flight reset.

There is no worker-side text heuristic for PDC or ACARS. On VATSIM these arrive
through the same direct-message protocol record as other private messages, so
all incoming direct records admitted by the Brain belong in the one PDC/private
Drawer. General radio chat never enters the Drawer.

## Runtime design

1. The Brain performs a metadata observation every five seconds while xPilot is
   connected and the active callsign/departure context is known.
2. File discovery and reading run on the existing low-priority diagnostics
   worker lane. The flight loop only exchanges one bounded request/result
   mailbox and never performs filesystem I/O.
3. An unchanged file causes no payload read, no cache mutation, no Drawer
   rebuild, and no ORB mutation.
4. A growing file is read from the Brain-owned cursor in bounded chunks. A
   short continuation cadence may drain a fixed snapshot without delaying the
   next five-second metadata check; this is background work, not a faster poll.
5. Initial attachment, reconnect, rotation, or truncation performs a bounded
   recovery scan sufficient to find the current xPilot login/session. Any
   unreadable or skipped range is reported as a loss/availability fact.
6. The Brain retains at most 32 messages and 256 KiB of message product data.
   Exact file identity plus source offset provides replay identity. Only an
   exact source-record replay is deduplicated; repeated text at a new offset is
   retained as a distinct message.
7. Newest messages appear first in the Drawer. Each accepted message begins
   unread. At least one unread message makes the PDC ORB amber and shows `NEW`.
   Visible entries may be acknowledged while the Drawer is open. When the pilot
   leaves the PDC Drawer, the Brain acknowledges every retained message from
   that opened batch, including multi-record PDC content below the viewport.
   The ORB then returns to the cyan awaiting state and shows `IDLE`. A later
   message returns it to amber.
8. Cached messages remain readable during temporary source loss. A confirmed
   callsign/flight identity change clears the prior flight's message history.
9. Diagnostics contain source state, request/result counts, bytes, exclusions,
   malformed/loss counters, admission counts, unread count, cache size, worker
   time, and Brain reasons. They never contain private message bodies.

## Files in scope

New files:

- `brain/include/XVatsim/brain/BrainPdcLogTypes.h`
- `brain/src/BrainPdcLogRuntime.cpp`
- `modules/runtime_workers/include/XVatsim/modules/runtime_workers/PdcLogReader.h`
- `modules/runtime_workers/src/PdcLogReader.cpp`
- `tools/regression_harness/src/PdcLogMonitorContractProbe.h`
- `tools/regression_harness/src/PdcLogMonitorContractProbe.cpp`

Existing files allowed to change:

- `brain/include/XVatsim/brain/BrainPdcRuntime.h`
- `brain/src/BrainPdcRuntime.cpp`
- `brain/src/BrainOwnedRuntime.cpp`
- `brain/CMakeLists.txt`
- `modules/runtime_workers/include/XVatsim/modules/runtime_workers/AsyncDiagnosticsWriter.h`
- `modules/runtime_workers/src/AsyncDiagnosticsWriter.cpp`
- `modules/runtime_workers/CMakeLists.txt`
- `plugin/src/XVatsimPlugin.cpp`
- `tools/regression_harness/CMakeLists.txt`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/src/Step6PdcContractProbe.cpp`

Any need to change controller selection, controller scoring, route/polygon work,
METAR, ATIS, radio/standby behavior, xPilot itself, or files outside this list
is outside this gate.

## Preserved behavior

- The V2.0.1 METAR and ATIS services, ORBs, Drawers, colors, acknowledgement,
  ordering, caches, and cadences remain unchanged.
- PDC uses the established accessory publication and visible-entry
  acknowledgement path; the renderer remains a renderer of Brain state.
- The existing one-message dataref evaluator remains available to its legacy
  offline scenarios, but the production plugin no longer depends on it for PDC
  acquisition.
- Controller receiver-board admission, Australia evidence voting, polygon
  current/next state, distance display, standby, route work, and controller
  diagnostics are untouched by this gate.
- No message sending capability is added. xPilot files are opened read-only and
  xPilot is not modified.

## Acceptance evidence

The gate passes only when the compiled normal path proves all of the following:

1. The parser reports incoming/outgoing direct, radio, broadcast, wallop,
   server, login, logout, and client identity as protocol facts without product
   admission.
2. The Brain admits incoming direct messages addressed to the active callsign
   and rejects every other channel/session/callsign with an explicit reason or
   counter. Interleaved incoming `#AP`, `#AA`, and `#DP` traffic for other VATSIM
   clients cannot open, close, or replace the local xPilot session.
3. PDC/ACARS text and ordinary incoming private text are retained without a
   clearance-text heuristic; public radio chatter is absent from the Drawer.
4. A five-second unchanged interval performs metadata-only monitoring with no
   payload read, presentation mutation, or controller-work wake.
5. Append, partial-line completion, reconnect, rotation, truncation, stale
   result, unavailable directory, oversized line, malformed UTF-8, and bounded
   capacity paths are deterministic and reported.
6. History is newest first, bounded to 32 messages/256 KiB, stable across
   unchanged callbacks, deduplicated only by exact source identity, and reset
   on confirmed flight/callsign change.
7. ORB lifecycle is gray `IDLE`, amber `NEW` on unread content, cyan `IDLE`
   after the pilot leaves the opened Drawer, then amber `NEW` again for a later
   message. Closing or switching away from the PDC Drawer acknowledges the full
   retained batch even when a multi-record PDC extends below the viewport. The
   Drawer shows the newest message first and preserves older messages.
8. The flight-loop side performs no file I/O. Request, harvest, unchanged, and
   message-admission callbacks remain within the existing callback performance
   gate; worker wall/CPU time is reported separately.
9. Existing V2.0.1 PDC, accessory, METAR, ATIS, controller, Australia, and full
   regression suites do not gain a new failure.
10. A release plugin builds successfully. Installation and commit occur only
    after explicit Product Owner authorization.

## Rollback

The original checkout remains at the V2.0.1 release commit. This branch was
created directly from that commit, and pre-change patches plus SHA-256
manifests preserve the earlier Australia, diagnostic, and installed-XPL states.
Rolling back this gate means returning this branch to its V2.0.1 parent or
restoring the preserved installed XPL; it never requires moving the original
main branch.
