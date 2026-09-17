# Roadmap Boundaries

Updated: 2026-09-08

## V2 Product Position

XVatsim V2.0.0 is the current public freeware xPilot companion plugin for
Windows and X-Plane 12. XVatsim is not a replacement VATSIM client, not
an xPilot fork, and not a full network/audio client.

The V1 reliability goal is controller-awareness trustworthiness:

- typed route parsing
- authoritative center and terminal coverage
- fail-closed source handling
- stable overlay presentation
- clean lifecycle/reset behavior
- regression coverage for known real-world failures

## Do Not Reopen Without A New Milestone

- xPilot fork/replacement planning
- standalone desktop client planning
- installer/updater planning
- network/audio-client ownership
- general private-message inbox or AUTO_ATC card presentation
- SimBrief or Navigraph AIRAC ingestion
- second-monitor/out-of-sim UI
- dedicated VFR controller-evidence workflow

These items should not change the released V2.0.0 path without their own
source-of-truth, performance budget, offline proof, and live-test plan.

## V2.0.0 Released Baseline

Steps 1 through 6 are accepted, complete, and frozen. V2.0.0 now has three
completed Brain-owned accessory products on the existing rail and drawer:

- METAR from the official bounded METAR source;
- VATSIM ATIS from the existing shared VATSIM feed; and
- one-shot PDC presentation from the first mechanically valid waiting xPilot
  message for a complete flight identity.

The PDC feature is not a combined inbox, general chat mirror, sender/controller
classifier, or amendment monitor. The Brain retains exactly one bounded
flight-bound snapshot and closes further private-source sampling. xPilot remains
authoritative for later revisions, and the drawer warns the pilot to check
xPilot.

The accepted feature commit is
`94825f07248dafc038d4c29e06ea61e40f49cfc3`; Step 6 proof and closeout is
`380039a4494f0b373542eced807345471f214036`. The final proof baseline is visual
`14/14`, focused `61/61`, complete regression `861/861` twice, `861` scenarios
with fingerprint
`227121979F59CB3BB359B96DE57AC7DF601E0BE3736F82F38C19EE08437431D9`,
and a Product Owner normal-use live PASS. Later TRACON and ATIS corrections were
also live accepted before release.

V2.0.0 remains Windows/X-Plane 12/xPilot only. Mac and Linux support are
deferred until native test resources exist. The public package passed a fresh
Release build, `882 / 882` saved scenarios, exact nine-file smoke extraction,
version-string audit, and binary/package hash verification.

The accepted IFR/VFR selection foundation remains. A dedicated VFR evidence
engine and live VFR controller projection are not V2.0.0 requirements. They are
only an optional Version 3 product decision, and Version 3 is not promised.

Rule One remains absolute: modules report bounded mechanical facts, the Brain
makes every product and semantic decision, the plugin obeys, and the UI renders
Brain-approved facts. All products reuse the single accessory preparation,
presentation, rendering, and visible-publication return architecture.

The next state is post-release observation. Any source/test correction, new
version, packaging change, or Version 3 roadmap requires its own approved
Contract Gate.

## V2.0.1 Maintenance Release

V2.0.1 is a focused performance patch built from the live-accepted settled
operational refresh gate at commit `0c626a9`. It reduces repeated flight-loop
work after operational inputs settle while preserving immediate wake behavior
for meaningful changes and a one-second safety refresh. It also avoids repeated
inactive vNAS scans outside the supported United States scope and unnecessary
diagnostic job construction between scheduled diagnostic frames.

The V2.0.1 package passed a clean Release build, all `884 / 884` saved
regression scenarios, nine-file package inspection, binary identity checks,
and the Product Owner's controlled live performance test. The public manifest
reports V2.0.1 after both download destinations were prepared.

V2.0.1 does not change controller-selection policy, information ORBs, Standby
Assist, or UI rendering. It does not adopt X-Plane SDK 4.4 Panel Graphics.

## V2.0.2 Controller And PDC Maintenance Release

V2.0.2 restores the Rule One boundary for controller selection. Workers report
bounded yes, no, or neutral facts; the Brain makes the display decision. The
release includes callsign-variation and declared-extension evidence needed for
Melbourne terminal controllers and extended Australian Center coverage.

The PDC Drawer monitors xPilot network logs every five seconds for incoming
direct private and PDC/ACARS messages. The Brain excludes public radio,
broadcast, server, outgoing, other-session, and other-callsign traffic, keeps a
bounded newest-first history, and owns the `NEW`/`IDLE` lifecycle.

Routine flight-loop diagnostics are summarized once per minute with
rate-limited outlier detail. V2.0.2 preserves route-polygon colors, controller
distance, Standby Assist, METAR, ATIS, CTAF, and the V2.0.1 settled-performance
gate. The release passed the Product Owner's full-flight online test, all `886 /
886` saved scenarios, and the nine-file package gate.

## Recorded X-Plane SDK Direction

X-Plane SDK 4.4 introduces backend-native Panel Graphics and establishes the
preferred future replacement for XVatsim's legacy OpenGL overlay renderer. The
research, architectural boundaries, compatibility risks, and phased migration
plan are recorded in
[`XPLANE_SDK_4_4_PANEL_GRAPHICS_DIRECTION.md`](XPLANE_SDK_4_4_PANEL_GRAPHICS_DIRECTION.md).

This is a future, separately gated renderer milestone. It is not part of the
V2.0.2 maintenance scope, and it must not move
Brain decisions into the UI or introduce continuous flight-loop work.

## Future Work Rule

Future features must enter through a milestone plan with:

- source-of-truth definition
- failure behavior
- regression-harness scenario coverage where possible
- explicit UI fit assessment
- clean release gate decision

No future feature should be added just because the code can technically read the data.
