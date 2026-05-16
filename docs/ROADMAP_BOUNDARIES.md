# Roadmap Boundaries

Updated: 2026-05-15

## V1 Product Position

XVatsim V1 is an xPilot companion plugin for Windows and X-Plane 12. It is not a
replacement VATSIM client, not an xPilot fork, and not a full network/audio client.

The V1 reliability goal is controller-awareness trustworthiness:

- typed route parsing
- authoritative center and terminal coverage
- fail-closed source handling
- stable overlay presentation
- clean lifecycle/reset behavior
- regression coverage for known real-world failures

## Do Not Reintroduce For V1

- xPilot fork/replacement planning
- standalone desktop client planning
- installer/updater planning
- network/audio-client ownership
- private-message, PDC, or AUTO_ATC card presentation
- SimBrief or Navigraph AIRAC ingestion
- second-monitor/out-of-sim UI
- dedicated VFR workflow

These may be discussed after the V1 milestone map is complete, but they should not
change the current release path.

## Future Work Rule

Future features must enter through a milestone plan with:

- source-of-truth definition
- failure behavior
- regression-harness scenario coverage where possible
- explicit UI fit assessment
- clean release gate decision

No future feature should be added just because the code can technically read the data.
