# XVatsim Manager Online Payload Contract Gate

**Status:** Approved by the Product Owner on 2026-10-22
**Scope:** Let the standalone Manager obtain future verified XVatsim payloads
without requiring a newly downloaded Manager for every plugin release.

## Problem

The V2.1.0 Manager can install, update, and repair only the payload embedded in
its own executable. Opening an older Manager can therefore report that its own
payload is current even after a newer XVatsim release has been published. That
behavior makes the word `Update` misleading and forces users to download a new
Manager for every plugin release.

## Product Contract

1. The Manager checks the existing public `xvatsim_update.json` file when it
   opens.
2. The plugin remains notify-only. The Manager's online payload support does
   not change the plugin update checker or install files from inside X-Plane.
3. A future public manifest may contain an optional `manager_payload` object
   with:
   - the direct HTTPS payload URL;
   - the exact payload byte length;
   - the exact payload SHA-256;
   - the minimum compatible Manager version.
4. If the published version is newer than the embedded payload and the
   `manager_payload` facts are present, the Manager downloads that payload,
   verifies it, and presents the normal user-selected `Update` action.
5. The Manager never starts an installation merely because it found or
   downloaded a payload. The user must select Install, Update, or Repair.
6. X-Plane and xPilot must remain closed before any managed file is changed.
7. The existing staging, backup, final-file verification, receipt, and rollback
   transaction remains the only installation path.
8. A network, JSON, size, hash, ZIP inventory, payload schema, or product
   version failure cannot itself modify the installed plugin. If a valid
   public manifest identifies a newer release but that release payload fails
   verification, install actions are blocked and the pilot is directed to the
   current release package.
9. The embedded payload remains available for a first installation or repair
   when the public manifest cannot be reached or validated, provided it would
   not downgrade a newer receipted installation.
10. A Manager whose embedded payload is older than the installed receipt must
    refuse to replace those files and direct the user to the current Manager.
11. If a future payload requires a newer Manager, the current Manager explains
    that one manual Manager download is required. It does not attempt to
    replace its own running executable.
12. The first release containing this feature is a one-time transition:
    existing V2.1.0 Manager users must download the new release Manager once.
    Compatible later plugin releases can then arrive through the Manager.

## Trust And Verification Gate

The Manager may select an online payload only after all of these checks pass:

- the public manifest is schema 1, product `XVatsim`, and stable channel;
- the published and minimum Manager versions are valid release versions;
- the payload URL is absolute HTTPS;
- the declared size is positive and within the Manager's fixed limit;
- the received byte count exactly matches the declared size;
- the downloaded archive SHA-256 exactly matches the published SHA-256;
- the archive passes the existing safe-path and exact-inventory validation;
- the archive uses the supported installation-payload schema; and
- the payload product version exactly matches `latest_version`.

The downloaded bytes are facts. They gain no install authority until every
gate above passes and the user selects the displayed action.

## Brain Authority

The Manager is a deployment application and never runs inside X-Plane or
xPilot. It cannot publish controller, route, Assist, radio, aircraft, or UI
runtime decisions. This addition does not create a worker with decision
authority and does not change the Brain-only runtime contract.

## Publication Order

For every future release:

1. Build and test the plugin and Manager payload.
2. Record the exact payload filename, byte length, and SHA-256.
3. Upload the payload and full user package to their final public locations.
4. Verify anonymous HTTPS downloads and repeat the recorded hashes.
5. Update `xvatsim_update.json` last.

Publishing the JSON first is prohibited because it could advertise an asset
that users cannot yet retrieve.

## Required Proof

Offline automated proof must cover:

- published version already represented by the embedded payload;
- valid newer remote payload selection;
- absent online payload metadata;
- incompatible minimum Manager version;
- offline/timeout and invalid JSON behavior;
- non-HTTPS URL rejection;
- excessive, short, and long downloads;
- SHA-256 mismatch;
- invalid ZIP paths or inventory;
- payload/public-version mismatch;
- installed receipt newer than the available payload;
- retained transaction, rollback, process guard, and unmanaged-file tests.

Before release, a human must also open the release-candidate Manager and prove:

1. a current installation reports current after a successful online check;
2. an older isolated installation is offered the published update;
3. X-Plane or xPilot running still blocks the update;
4. a deliberately unavailable update endpoint changes no installed file; and
5. the completed update returns to Current with all installed hashes matching
   the downloaded payload.

No release commit, package publication, public-manifest change, or user rollout
is authorized by this gate alone.

## Implementation And Product-Owner Proof

On 2026-10-22 the implementation passed all 40 isolated Manager checks. These
included valid online-payload selection, offline fallback, invalid JSON,
non-HTTPS and oversized payload rejection, short and long download rejection,
SHA-256 rejection, unsafe ZIP rejection, public/payload version mismatch,
minimum-Manager enforcement, downgrade refusal, transaction rollback, locked
file rollback, process guarding, preservation of unmanaged files, and the
existing xPilot compatibility matrix.

The self-contained Manager release-build path and embedded-payload self-test
also passed. The wider XVatsim suite passed all 911 applicable scenarios; the
only excluded scenario remains the documented machine-specific absolute
xPilot path fixture.

The Product Owner then used the release-candidate Manager to replace the
installed XPL and authority registry from a newly built embedded payload. The
Manager returned to `Current`; all four managed file lengths and SHA-256 values
matched the new receipt; the former XPL and registry were present in the
transaction backup; and transaction staging was empty. A separate recovery
copy was also retained outside the simulator tree.

The installed candidate was run live as DAL41 at EGLL on the EGLL-to-KEWR
route. Diagnostics confirmed a resolved route, healthy xPilot 4 transport,
zero bridge gaps or loss, zero worker or writer failures, zero performance
outliers, Brain-owned controller and Assist decisions, route-scoped detailed
controller work, and bounded diagnostic storage. The Product Owner approved
that online test after shutdown.

This proof validates the Manager transaction and the installed plugin. The
first hosted `manager_payload` download still requires release-publication
proof after the final asset exists at its anonymous HTTPS URL and before the
public updater JSON is changed.
