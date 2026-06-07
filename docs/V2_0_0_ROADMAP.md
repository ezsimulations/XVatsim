# XVatsim V2.0.0 Roadmap

Date: 2026-06-07

## Starting Point

XVatsim V1.0.2 is the current freeware Windows/X-Plane 12/xPilot release.
V2.0.0 starts after the closed Version 1 baseline.

## Primary V2 Objectives

- Add a dedicated VFR workflow.
- Add Mac support.
- Add Linux support.

## Contract Boundary

V2 work does not loosen the Engineer 3 runtime contract:

`Brain decides. Modules produce facts. UI displays brain-approved facts.`

No V2 implementation should add feature-specific authority, relevance, display,
or fallback scheduling into `plugin/src/XVatsimPlugin.cpp`.

## VFR Boundary

The VFR implementation needs its own Contract Gate before code changes. That gate
should define:

- VFR source-of-truth inputs
- how VFR differs from IFR flight-plan ownership
- how CTAF/UNICOM, airport locals, and reachable controllers are proven
- how the workflow fails closed when VFR evidence is incomplete
- which UI states are allowed in V2
- focused regression scenarios before any live test

Version 1 display rules remain active until a V2 display contract replaces them.

## Platform Boundary

Mac/Linux support is a platform-port workstream, not a reason to change live
runtime decisions.

The first platform gate should classify:

- build-system changes
- X-Plane SDK platform requirements
- binary output layout for `mac_x64`, Apple Silicon/universal needs, and
  `lin_x64`
- plugin signing/notarization needs for Mac
- packaging layout
- dependency and audio asset portability
- smoke tests for each supported platform

## Release Rule

Every V2 slice needs:

- a Contract Gate before edits
- focused regression proof
- full harness proof when runtime behavior changes
- clean generated-artifact handling
- a clear package validation step before public release
