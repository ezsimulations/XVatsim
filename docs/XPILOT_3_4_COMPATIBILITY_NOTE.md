# xPilot 3 and xPilot 4 Compatibility Note

Status: research and design only
Recorded: 2026-09-24
Current XVatsim release: 2.0.2

## Live finding

xPilot 4.0.0-beta.6 received and displayed a PDC/private message, but XVatsim
2.0.2 did not receive it. XVatsim 2.0.2 currently monitors the xPilot 3
`NetworkLogs` directory. The running xPilot 4 beta did not write the received
message to that legacy log.

xPilot 4's public plugin SDK exposes `IBroker.PrivateMessageReceived` with the
sender and message body. This is the intended research path for a future xPilot
4 bridge. Implementation remains gated on the technical event, lifecycle, and
distribution details in the maintainer questionnaire.

## Maintainer response

Justin confirmed on 2026-09-24 that:

- XVatsim may build and distribute an integration using interfaces exposed by
  the xPilot 4 Plugin SDK, subject to the SDK license.
- He does not expect separate VATSIM approval when XVatsim only consumes SDK
  data/events and does not independently connect to or interact with the VATSIM
  network. He correctly noted that he cannot speak for VATSIM policy.
- xPilot 4 and its Plugin SDK are still beta products, so API changes remain
  possible.
- He invited the detailed technical questionnaire.

The official Plugin SDK repository declares the MIT License. XVatsim must retain
the required copyright and license notice with any redistributed SDK source or
binary. This permission does not remove the need for capability/version gates
while the API remains in beta.

## Required compatibility model

XVatsim must remain compatible with both supported xPilot generations:

- xPilot 3 uses the existing low-frequency `NetworkLogs` monitor.
- xPilot 4 uses a small companion plugin built against the official xPilot
  plugin SDK and subscribes to `PrivateMessageReceived`.
- Both sources produce the same neutral message-evidence envelope for the
  XVatsim Brain: source identity/version, connection state, sender, raw message
  body, receive time, and a monotonic source sequence.
- A source worker or bridge may observe, copy, sequence, cache, and transport
  evidence. It must not classify, suppress, hide, rewrite, prioritize, or decide
  whether a message is private, PDC, ACARS, displayable, unread, or a duplicate.
- The Brain remains the only authority for admission, PDC/ACARS classification,
  deduplication, history order, unread state, ORB color/text, Drawer content,
  acknowledgement, and reset behavior.
- If evidence from both adapters is present, both observations reach the Brain.
  The Brain decides whether they describe the same message.
- The xPilot 4 path should be event-driven. Any file/IPC pickup on the XVatsim
  side must be low-frequency and bounded so it cannot add a heavy flight loop.
- Message bodies must not be written to routine diagnostics. Diagnostics should
  report bounded counters, source/version, sequence, latency, admission result,
  and Brain decision reason without exposing private content.

## Rollback and release boundary

The xPilot 4 bridge must be additive. The xPilot 3 log-monitor implementation
stays intact until both paths pass their compatibility gates and live tests.
XVatsim 2.0.2 remains the known-good rollback point. No xPilot 4 work may alter
controller selection, radio display, polygon/sector logic, standby logic, COM
writes, METAR, ATIS, or the existing xPilot 3 PDC behavior.

## Questions for the xPilot maintainer

Confirm before implementation:

1. Whether `PrivateMessageReceived` is intended as a stable third-party plugin
   contract for xPilot 4.
2. Whether every controller private message, including PDC/ACARS workflows,
   raises that event with complete sender and body data.
3. Event ordering, delivery thread, reconnect behavior, and whether replay can
   occur after a plugin reload.
4. The supported plugin packaging, installation, signing, update, and versioning
   model.
5. Whether an XVatsim companion plugin may be distributed with XVatsim and what
   license or attribution requirements apply.
6. How a plugin should detect SDK capability changes across xPilot 4 beta and
   stable releases.

## Acceptance gates for future development

- xPilot 3 private message and PDC live tests continue to pass unchanged.
- xPilot 4 private message and PDC live tests pass through the official SDK.
- Public radio, broadcast, server, outgoing, and other non-private traffic never
  enters the PDC Drawer unless the Brain explicitly admits it under its policy.
- Every accepted or rejected observation has a Brain receipt showing the source
  evidence and decision reason.
- New messages turn the PDC ORB amber and display `NEW`; opening and leaving the
  Drawer acknowledges them and returns the ORB to its idle color.
- Messages remain newest-first in the Drawer, survive bounded reconnect cases,
  and do not duplicate when source evidence is replayed.
- Idle CPU and flight-loop measurements remain within the existing performance
  contract.
- Removing the xPilot 4 companion restores the exact XVatsim 2.0.2 xPilot 3
  behavior without code or configuration migration.
