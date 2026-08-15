# Roadmap Boundaries

Updated: 2026-08-15

## V1 Product Position

XVatsim V1.2.2 is the current public freeware xPilot companion plugin for
Windows and X-Plane 12. The repository is prepared for the narrow V1.2.3 route
traversal maintenance package. XVatsim is not a replacement VATSIM client, not
an xPilot fork, and not a full network/audio client.

The V1 reliability goal is controller-awareness trustworthiness:

- typed route parsing
- authoritative center and terminal coverage
- fail-closed source handling
- stable overlay presentation
- clean lifecycle/reset behavior
- regression coverage for known real-world failures

## Do Not Reopen For V1

- xPilot fork/replacement planning
- standalone desktop client planning
- installer/updater planning
- network/audio-client ownership
- private-message, PDC, or AUTO_ATC card presentation
- SimBrief or Navigraph AIRAC ingestion
- second-monitor/out-of-sim UI
- dedicated VFR workflow

These items should not change the closed Version 1 freeware release path except
for narrow patch releases that preserve the Version 1 runtime contract.

## V2.0.0 Entry Point

Future work begins as XVatsim V2.0.0 planning and implementation.

Initial V2 objectives:

- dedicated VFR implementation
- Mac support
- Linux support

Each V2 objective needs its own Contract Gate before source changes. The
brain-owned runtime contract still applies: Brain decides, modules produce facts,
and UI displays brain-approved facts.

## Future Work Rule

Future features must enter through a milestone plan with:

- source-of-truth definition
- failure behavior
- regression-harness scenario coverage where possible
- explicit UI fit assessment
- clean release gate decision

No future feature should be added just because the code can technically read the data.
