# XVatsim Manager Contract Gate

**Gate ID:** `XVATSIM-MANAGER-01`

**Status:** Approved by Product Owner on 2026-09-26; xPilot 4 beta
compatibility amendment approved on 2026-09-26

**Baseline:** `627fbb96d9234bb05ba1ce10c6acc2e6827e02b6`
**Scope:** A separate Windows installer, updater, repair, and rollback manager

## Purpose

The manager provides one guided path for a first installation, update, repair,
or compatibility check. It locates valid X-Plane 12 and xPilot installations,
deploys only the files in its signed manifest, verifies every deployed byte,
and restores the previous files if any step fails.

The manager is a deployment tool. It is never loaded by X-Plane or xPilot and
has no runtime path into controller, message, radio, route, accessory, or UI
decisions.

## Governing Bible Law

> The Brain is the only decision maker. A worker may obtain and report facts,
> but it may not suppress, filter, classify, score, prioritize, change, or act
> on those facts before the Brain receives them.

This manager does not add a worker to XVatsim. It may copy approved binaries and
report installation facts. It may not alter XVatsim's runtime architecture,
data, settings, source feeds, evidence votes, or Brain decisions.

## Authorized Files

Implementation may create or change only:

- `docs/XVATSIM_MANAGER_CONTRACT_GATE.md`
- `.gitignore` entries for manager output
- `manager/**`
- `scripts/build_xvatsim_manager.ps1`

No file under `brain/`, `core/`, `modules/`, `plugin/`, `integrations/`,
`assets/`, or the existing release/update manifests may be changed by this
gate.

## Owned Deployment Files

The first manager payload owns exactly these XVatsim runtime files:

- `Resources/plugins/XVatsim/win_x64/XVatsim.xpl`
- `Resources/plugins/XVatsim/win_x64/ui_transition.mp3`
- `Resources/plugins/XVatsim/win_x64/authority_source_registry.json`

For a validated xPilot 4 installation it may also own exactly:

- `%LOCALAPPDATA%/org.vatsim.xpilot/Plugins/XVatsim.XPilot4Bridge/XVatsim.XPilot4Bridge.dll`

Logs, preferences, backups, documentation, unrelated plugins, and every other
file are outside manager ownership and must be preserved.

## Discovery And Compatibility

1. Fast discovery reads X-Plane installer records, known locations, Steam
   libraries, and Windows uninstall records.
2. Every candidate is validated by executable identity and expected directory
   structure. A matching filename alone is insufficient.
3. If targeted discovery finds nothing, the pilot selects the X-Plane root.
   The normal workflow must not crawl scenery, aircraft, system, network, or
   unrelated folders looking for an executable.
4. Multiple valid X-Plane installs are displayed and require an explicit
   selection.
5. xPilot 3 remains supported through XVatsim's existing legacy integration and
   receives no companion DLL.
6. The manager recognizes xPilot generation 4 without binding installation to
   an individual beta product version or simulator-plugin file hash. A new
   xPilot 4 beta does not require a manager rebuild merely because its product
   version or binary fingerprint changed.
7. The companion is the SDK compatibility boundary. At xPilot startup it reads
   the broker's machine-readable SDK API version. The approved companion
   accepts API versions greater than or equal to `0.1.0` and lower than
   `0.2.0`. Outside that range it subscribes to no events and reports an
   explicit unavailable fact to the Brain-owned runtime.
8. A future xPilot major generation is not assumed compatible with xPilot 4.
   It remains blocked until separately designed and approved.
9. Presence of the xPilot simulator plugin remains required, but its exact
   file hash is not a bridge-compatibility decision. xPilot owns validation of
   its desktop/client simulator pairing.
10. XVatsim installation remains disabled until valid X-Plane and xPilot
   installations are both selected and their required pairing is verified.

## Transaction Contract

For every install, update, or repair the manager must:

1. refuse to proceed while X-Plane or xPilot is running;
2. extract the embedded payload to a private transaction staging directory;
3. reject absolute paths, parent traversal, unexpected files, size mismatches,
   and SHA-256 mismatches;
4. back up each existing managed file outside the simulator tree;
5. replace files through same-directory temporary files;
6. verify the installed SHA-256 values;
7. write a per-X-Plane installation receipt only after verification; and
8. restore all replaced files and remove all newly created files if any step
   fails.

The manager never force-closes an application, deletes an XVatsim directory,
or performs a recursive cleanup of a simulator tree.

## User Experience Contract

The normal screen presents validated installations, plain compatibility text,
and one recommended action: `Install`, `Update`, `Repair`, or `Current`.
Advanced details remain available without being required. Ambiguous paths,
running programs, unrecognized xPilot generations, missing simulator plugins,
and failed verification are shown before any change is made. For xPilot 4 the
screen states that SDK compatibility is checked by the companion when xPilot
starts; it does not claim that a product-version string proves SDK
compatibility.

An installation, update, or repair presents a visible progress panel with
plain-language stages. A successful fast transaction remains visible for at
least five seconds so the pilot can see that work occurred. The timed display
does not delay, bypass, or replace payload verification, backup, rollback, or
post-install hash checks. A real failure interrupts the success sequence and
is shown immediately.

## Release Boundary

The development executable may be unsigned for Product Owner testing. Public
distribution requires Authenticode signing, SHA-256 verification, a timestamp,
and a separately approved online update manifest. This gate does not authorize
publishing, pushing, changing the public update JSON, or redistributing xPilot.

## Required Offline Proof

The manager cannot be called complete until isolated tests prove:

- clean first install;
- update and repair;
- preservation of unrelated files;
- rollback after an injected mid-install failure;
- rejection of a corrupt payload and path traversal;
- running-process refusal;
- xPilot 3, current and later xPilot 4 beta product versions, both, none,
  missing simulator plugin, and an unrecognized future xPilot generation;
- companion acceptance throughout API `0.1.x` and refusal at API `0.2.0`
  before any event subscription;
- multiple X-Plane selection behavior; and
- detection of an unreceipted or modified XVatsim binary.

The existing simulator installation is not a test fixture. Product Owner live
testing begins only after the isolated proof passes.
