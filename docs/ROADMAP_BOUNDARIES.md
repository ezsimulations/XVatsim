# Roadmap Boundaries

Updated: 2026-08-31

## V1 Product Position

XVatsim V1.2.3 is the current public freeware xPilot companion plugin for
Windows and X-Plane 12. XVatsim is not a replacement VATSIM client, not
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

## V2.0.0 Feature-Complete Beta Baseline

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
and a Product Owner normal-use live PASS. The exact `3/3` live-proven candidate
remains installed as the beta baseline.

V2.0.0 remains Windows/X-Plane 12/xPilot only. Mac and Linux support are
deferred until native test resources exist. V2.0.0 is feature-complete and
entering extended multi-flight beta; it is not yet publicly released or
release-certified. V1.2.3 remains the current public release and the retained
beta binary deliberately retains `1.2.3` metadata.

The accepted IFR/VFR selection foundation remains. A dedicated VFR evidence
engine and live VFR controller projection are not V2.0.0 requirements. They are
only an optional Version 3 product decision, and Version 3 is not promised.

Rule One remains absolute: modules report bounded mechanical facts, the Brain
makes every product and semantic decision, the plugin obeys, and the UI renders
Brain-approved facts. All products reuse the single accessory preparation,
presentation, rendering, and visible-publication return architecture.

The next state is extended beta observation. Any source/test correction, beta
versioning or packaging, build/deployment change, release certification,
publication, or Version 3 roadmap requires its own approved Contract Gate.

## Future Work Rule

Future features must enter through a milestone plan with:

- source-of-truth definition
- failure behavior
- regression-harness scenario coverage where possible
- explicit UI fit assessment
- clean release gate decision

No future feature should be added just because the code can technically read the data.
