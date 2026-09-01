# XVatsim Next Session Handoff — V2.0.0 Feature-Complete Beta

Updated: 2026-08-31

## Current Classification

XVatsim V2 Steps 1 through 6 are accepted, complete, and frozen as the V2.0.0
feature-complete beta baseline.

The binding classification is:

`STEP 6 PDC ORB — OFFLINE-PROVEN, NORMAL-USE LIVE ACCEPTED; V2.0.0 FEATURE-COMPLETE BETA ENTRY`

The completed accessory product has exactly three ORBs:

1. METAR
2. VATSIM ATIS
3. One-shot PDC presentation backed by mechanically neutral first-waiting-
   message admission

This is feature-complete beta entry, not public-release certification. The next
project state is extended Product Owner-operated multi-flight beta observation
of the exact retained candidate.

## Roles And Authority

- Darron is Product Owner and final approval authority.
- ChatGPT is Project Director and prepares Contract Gates with Darron.
- Codex is the Engineering Agent and acts only under an explicitly approved
  Contract Gate.
- The repository is the durable shared memory between tasks.
- Opening a new session grants no correction, build, deployment, version,
  package, release, evidence-mutation, or Version 3 authority.

## Repository And Commit Chain

```text
Repository: C:\Users\DARRON\OneDrive\Documents\XVatsim-V2
Branch: v2-development
Pre-closeout parent: 7a7f51554513388dbcb8c282a8d45085eddf4446
Commit 1: 94825f07248dafc038d4c29e06ea61e40f49cfc3
Commit 2: 380039a4494f0b373542eced807345471f214036
```

Commit 1:

- subject: `feat: complete Step 6 one-shot PDC ORB`;
- parent: `7a7f51554513388dbcb8c282a8d45085eddf4446`;
- exact scope: `84` files.

Commit 2:

- subject: `docs: close out Step 6 PDC ORB proof`;
- parent: `94825f07248dafc038d4c29e06ea61e40f49cfc3`;
- exact scope: `3` files.

The current `HEAD` is the documentation-only beta-entry commit created after
Commit 2. Its SHA is deliberately not embedded because this document is part of
that commit. At the beginning of the next session, resolve `HEAD` locally and
require:

- `HEAD^` equals `380039a4494f0b373542eced807345471f214036`;
- subject is exactly `docs: enter V2.0.0 feature-complete beta`; and
- scope is exactly these seven files:
  - `README.md`
  - `docs/ARCHITECTURE.md`
  - `docs/MILESTONE_STATUS.md`
  - `docs/NEXT_SESSION_HANDOFF.md`
  - `docs/NEXT_SESSION_START_PROMPT.txt`
  - `docs/ROADMAP_BOUNDARIES.md`
  - `docs/V2_0_0_ROADMAP.md`

Do not substitute a remembered Commit 3 SHA for local verification.

Expected post-closeout repository state:

- modified tracked / staged / unmerged: `0/0/0`;
- canonical untracked inventory: `5,207` paths;
- canonical untracked inventory SHA-256:
  `4EC19C3E3E4971697B6E0BB325BD2D4DA2C49DFBFAA92B8EB014B6BA979C5455`;
- accepted scenarios: `861`;
- scenario fingerprint:
  `227121979F59CB3BB359B96DE57AC7DF601E0BE3736F82F38C19EE08437431D9`.

Canonical inventory is the ordinal-sorted output of
`git -c core.longpaths=true ls-files --others --exclude-standard`, joined with
LF and hashed as UTF-8 without BOM. Diagnose long-path, sort, separator,
encoding, and enumeration differences before declaring a mismatch.

## Accepted Step 6 Product Contract

Acquisition begins only after stable xPilot connection and complete
Brain-owned flight context. The qualified xPilot bridge may then mechanically
sample a positive-sequence, non-empty, bounded waiting private record.

The Brain admits the first such record exactly once for the current flight.
Admission is independent of sender or controller identity, message wording,
controller roster, IFR/VFR operating mode, and workflow or progression stage.
There is no combined private-message inbox, general chat mirror, semantic text
classifier, or message history.

On admission, the Brain atomically owns the retained snapshot, flight and
departure association, capture-complete latch, unread/viewed state, status,
tone, title, warning, acknowledgement, and lifecycle invalidation. Further
private-source identity, capability, sequence, sender, and body reads stop for
that flight.

The exact presentation contract is:

- waiting and qualified: `PDC / IDLE`;
- source unavailable: `PDC / SOURCE`;
- pre-capture source uncertainty: `PDC / CHECK`;
- captured and not visibly acknowledged: `PDC / NEW 1`;
- selected drawer: `PDC / OPEN`;
- captured and visibly acknowledged: `PDC / MSG 1`.

The Brain-owned drawer title is:

`PDC — <DEPARTURE ICAO>`

The exact warning is:

`CAPTURED SNAPSHOT — CHECK XPILOT FOR REVISIONS`

The retained snapshot is immutable. XVatsim intentionally ignores later
amendments; xPilot remains authoritative. A rare administrative or other
non-PDC private record may be the first mechanically eligible waiting record.
That accepted limitation must remain truthful in product and beta records.

Temporary xPilot disconnect/reconnect, overlay sleep, and Plugin Admin
suspend/resume preserve a captured snapshot. Brain-owned flight-identity or
plan-identity change, confirmed new-flight/cold-dark/session reset, explicit
full reset, and plugin unload clear and re-arm acquisition at their established
boundaries.

Rule One remains absolute:

- modules report bounded mechanical facts;
- the Brain makes every semantic and product decision;
- modules act or render only from Brain-owned decisions;
- the bridge does not classify private content;
- the overlay does not calculate status, unread state, counts, title, warning,
  or acknowledgement semantics; and
- the single accepted accessory preparation, rendering, and visible-
  publication return path remains authoritative.

Do not introduce a second inbox, semantic owner, scheduler, worker, dispatcher,
route, renderer, snapshot, publication path, runner, or proof system.

## Accepted Offline Proof Locks

- focused Step 6 probe: `61/61`;
- focused production-renderer visual proof: `14/14`;
- complete regression run 1: `861/861`;
- complete regression run 2: `861/861`;
- protected evidence: `80 manifests / 4,695 rows / 0 mismatches`;
- scenario count/fingerprint: `861` /
  `227121979F59CB3BB359B96DE57AC7DF601E0BE3736F82F38C19EE08437431D9`.

Final focused visual source:

- `53,227` bytes;
- SHA-256:
  `EBECD5FCAD4D3BB7C30E5678AEBC3BE2CF3724C06659F2118EA9C640C66BB53E`.

Resumed-green visual manifest:

- `14` rows / `0` mismatches;
- `1,226` bytes;
- SHA-256:
  `6572789BC541F9513107987FD7F777598C81E31E022B254D4838EFA54369A916`.

Offline summary:

- `4,684` bytes;
- SHA-256:
  `A2BC7ABCC2E946AEFBC2B3F34FB86DBBD53AEB1BAD860FDD21B1C203149DB148`.

Offline receipt:

- `2,210` bytes;
- SHA-256:
  `927C2AEC73F93CCE983F3A4A7AA826B7FC968AEC19FA6440ECCD12E875C2B424`.

Do not rebaseline, rewrite, or reinterpret the accepted 861-scenario lineage.

## Candidate And Retained Beta Deployment

Canonical candidate source:

`build/v2-step6-pdc-one-shot-waiting-admission-simplification-candidate/dist/XVatsim/win_x64`

The candidate and active
`C:\X-Plane 12\Resources\plugins\XVatsim\win_x64` each contain exactly:

| File | Bytes | SHA-256 |
|---|---:|---|
| `XVatsim.xpl` | `2,572,288` | `CA2840F6FE61124E37881124DF78E4B0A0049FD47C5BF4FA6EE179D9A6E77934` |
| `authority_source_registry.json` | `40,340` | `3676CA43E5AFB8A5E443FDE6D04616918029E004E17EFAA022695D91E23DB60B` |
| `ui_transition.mp3` | `27,116` | `C7BBE97DAD356C68FDFE40F9E8C1CF4EADEBCCD125463214F963F66356F8D9F1` |

Deployment parity is exact `3/3`, with zero subdirectories. This retained
installation is the beta baseline. Do not roll it back, overwrite it, rebuild
it, or redeploy it without a new gate. The preparation backup remains available
as a protected recovery point.

Installed xPilot binary:

- `4,990,976` bytes;
- SHA-256:
  `56AEC8CADC0CB36AAF36DC122C330FB782C76FC80190391B8D4A1F717602A8AF`.

X-Plane and xPilot were both stopped at closeout (`0/0`). Protected `V2 Test`
was `50` files / `29` directories.

## Accepted Normal-Use Live Result

The exact candidate passed a Product Owner-operated normal-use flight as
`UAL300` from `KDEN` to `KMSP`. Accepted observations were neutral `IDLE`, one
already-waiting record admitted once, visible `NEW 1`, selected `OPEN`, exact
drawer context and warning, visible acknowledgement, closed `MSG 1`, and the
same immutable snapshot on reopen. There was no duplicate capture, accounting
defect, perceived lag, or visual defect. No Plugin Admin manipulation, reset,
recovery, artificial ordering, or retry was used.

Do not reproduce or transcribe private sender or message-body content in future
documents.

Accepted runtime facts include:

- exactly one capture event;
- `captureComplete=1`, unread before acknowledgement `1`, retained bytes `418`;
- private sampler duration `3,859` microseconds;
- click accounting `3/3/0/0/0`;
- publications accepted/rejected/stale-rejected `13/0/0`;
- maximum synchronous accessory work `23` microseconds;
- maximum click-to-terminal `32,467` microseconds; and
- exact accounting `true`.

The one `1,713 ms` route/authority refresh warning is a nonblocking beta
observation. It occurred after successful capture, was unrelated to PDC
sampling/rendering, caused no perceived lag, and tripped no accessory threshold.

## Evidence And Cleanup Prohibition

Manifest locks:

- preparation backup: `6/6`, `998` bytes,
  `6AB7CBC5CB1374AF39FED20CB41E2B14382928D06BB37D1E4B5C2900F89336C7`;
- readiness evidence: `5/5`, `834` bytes,
  `B528C16E9CCFA6E6655ECD6AA23D3A710F9D5CA385DF5FD150ABDB886C2A729A`;
- closeout evidence: `6/6`, `566` bytes,
  `3687213853EE39B5231A49D1DC3AD5AD486805C2DB0F36EF6A48B346C9FD4498`.

All untracked proof outputs, manifests, screenshots, logs, receipts, readiness
records, live-test records, candidate artifacts, and the preparation backup are
protected. Historical failed campaigns and mandatory-stop evidence remain
immutable history; they are not approved future proof and must not be restored
or reused.

Do not run `git clean`; delete, move, normalize, regenerate, or rewrite
untracked evidence; restore quarantine material; overwrite accepted proof; or
replace the retained candidate. The preserved diagnostics `.log` remains
required by the backup manifest even though the repository's `*.log` ignore
rule excludes it from Git's path inventory.

Stopped post-test files remain:

- `XVatsim.prf`: `232` bytes /
  `118FA9430AC7B63A774770481705C9B681EE78AE13394320ABFB84E1F4837B10`;
- X-Plane `Log.txt`: `315,094` bytes /
  `B630A0BCD4621CC38B4C41C32B75D223535B77C5D0AD37FB59C7ED1C425575C1`;
- current diagnostics: `225,940` bytes /
  `5A21476EF76BE35416B55508FFE8C957D2679CBB3D4E2898305BDCB9299B8AF8`.

## Version And Roadmap Boundary

V1.2.3 remains the current public release. The live-proven beta candidate
retains embedded `1.2.3` metadata deliberately; changing it would produce a
different binary from the one proved offline and live. A future beta-packaging
gate may assign a V2 beta version and smoke-test those exact new bytes.

V2.0.0 is feature-complete and entering extended beta. It is not publicly
packaged, release-certified, or published. The existing IFR/VFR foundation
remains; a dedicated VFR evidence model and live projection are outside V2.0.0
and are only an optional Version 3 consideration. No Version 3 commitment
exists.

## Next-Session Boundary

Begin read-only. Resolve and verify the documentation-only current `HEAD`, then
verify repository, scenario, evidence, deployment, process, preference, and log
locks before discussing new authority.

The next ordinary project state is extended Product Owner-operated multi-flight
beta observation of the exact installed candidate. A new explicit Contract Gate
is required before any source or test correction, proof change, build or
deployment, beta version assignment, packaging, release certification or
publication, accepted-evidence mutation, or VFR/Version 3 work.

Required reading order:

1. `docs/NEXT_SESSION_START_PROMPT.txt`
2. `docs/NEXT_SESSION_HANDOFF.md`
3. `docs/MILESTONE_STATUS.md`
4. `docs/ARCHITECTURE.md`
5. `docs/ROADMAP_BOUNDARIES.md`
6. `docs/V2_0_0_ROADMAP.md`
7. `docs/V2_STEP_06_PDC_ORB_ONE_SHOT_WAITING_MESSAGE_FINAL_ENGINEERING_BRIEF.md`
8. `outputs/v2_step_06_pdc_orb_product_owner_live_test_director_acceptance.md`
9. `outputs/v2_step_06_pdc_orb_v2_0_0_beta_entry_closeout_receipt.md`

Stop and diagnose any mismatch. Do not repair a lock or alter preserved state
merely to make it match.
