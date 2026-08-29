# XVatsim Next Session Handoff

Updated: 2026-08-28

Status: Steps 1 through 3 are accepted and complete. Step 4 is offline-proven
through its latest correction but is not controlled-live-proven. The latest
controlled-live attempt stopped during Phase A and rollback passed.

## Roles And Authorization

- Darron is Product Owner and final approval authority.
- ChatGPT is Project Director and develops Contract Gates with Darron.
- Codex is the Engineering Agent and operates only within an approved Contract
  Gate.
- The repository is the durable shared memory between Director and Engineering
  tasks.
- No implementation, deployment, application startup, live request, or
  controlled-live proof may occur without Darron's explicit approval.

Do not infer authority from a previous session or from the presence of an
offline-proven binary.

## Authoritative Repository State

```text
Repository: C:\Users\DARRON\OneDrive\Documents\XVatsim-V2
Branch: v2-development
Accepted engineering/receipt HEAD before this handoff: 43d5776d2991167c89772afa0b80907759390c78
Implementation parent: 55dd147476f0da786eae4a27dde26912a6227d26
Saved scenarios: 700
Scenario fingerprint: C1367F24170283074D7D7371EFD4EDD1C687D7903F959CC3FA19FE3D807AAE8B
```

The documentation-only handoff commit is current `HEAD` after this closeout.
It cannot embed its own SHA because the SHA depends on the committed document
bytes. The next session must resolve it from Git and verify:

- parent: `43d5776d2991167c89772afa0b80907759390c78`;
- subject: `docs: update Step 4 next-session handoff`; and
- scope: exactly `docs/NEXT_SESSION_START_PROMPT.txt`,
  `docs/NEXT_SESSION_HANDOFF.md`, and `docs/MILESTONE_STATUS.md`.

Expected state after the documentation commit:

- tracked/staged changes: `0/0`;
- standard untracked files: `391`, all beneath `outputs/`;
- non-output untracked files: `0`;
- saved scenarios: `700`;
- X-Plane/xPilot processes: `0/0`;
- active/staged `.xpl`: `0/0`;
- active `win_x64`: absent; and
- protected `V2 Test`: `50` files and `29` directories, unchanged.

The canonical fingerprint uses ordinal case-insensitive scenario filename
ordering with an ordinal tie-break. Each scenario serializes its UTF-8
repository-relative path, NUL, raw bytes, and NUL.

Stop and report if any lock differs. Do not clean preserved `outputs/` evidence
to obtain a visually clean status.

## Accepted Milestones And Step 4 Lineage

### Step 1 - Windows V2 Proof Baseline

Accepted and complete. Implementation/receipt commit:
`2684b2f80c112673b8b9326edee1b0840c0ea9e4`.

### Step 2 - Explicit IFR/VFR Mode Foundation

Accepted and complete. Corrected receipt closeout:
`ac0f86261ebb12c7cdd0fc166f971ea738ad1f22`.

### Step 3 - ORB Rail And Information Drawer Foundation

Accepted and complete.

- Implementation/evidence:
  `219aa594adccb02143d02ea2f725761d85a6fe5f`
- Receipt-only closeout:
  `0c4ac545c1e484d5029e465e9eb99aa49fd19b08`

### Step 4 - VATSIM METAR

Implementation continues. The latest source is offline-proven but not
controlled-live-proven.

- Latest implementation/evidence:
  `55dd147476f0da786eae4a27dde26912a6227d26`
- Latest receipt-only:
  `43d5776d2991167c89772afa0b80907759390c78`
- Accessory input boundary corrective scenarios: `26/26`
- Step 3 focused scenarios: `31/31`
- Step 4 focused scenarios: `206/206`
- Complete regression: `700/700`
- Canonical fingerprint:
  `C1367F24170283074D7D7371EFD4EDD1C687D7903F959CC3FA19FE3D807AAE8B`
- 1,000-click stress: zero drops, all commands terminally accounted, maximum
  action time `100 µs`
- Warm idle: `100,000` cycles with zero recurring work
- Worker cancellation maximum: `17 ms`
- Real WinHTTP loopback: passed in `17 ms`
- Production-raster visual proof: `43` PNGs with zero repeat-render
  differences

Arrival remains:

`OUT OF SCOPE — UNCHANGED AND OFFLINE-PROVEN`

## Latest Offline-Proven Payload

The latest normal fixture-off plugin is:

`build\v2-step4-accessory-input-boundary-release-normal\dist\XVatsim\win_x64\XVatsim.xpl`

- Size: `2,293,248` bytes
- SHA-256:
  `694D1B70E40ACEB8F033D6985D3517CB20A76E3981D2165FDCABED4E90950CD3`

Companion payload:

- `authority_source_registry.json`:
  `3676CA43E5AFB8A5E443FDE6D04616918029E004E17EFAA022695D91E23DB60B`
- `ui_transition.mp3`:
  `C7BBE97DAD356C68FDFE40F9E8C1CF4EADEBCCD125463214F963F66356F8D9F1`

This is the latest offline-proven binary. It is not authorized for production
deployment or another controlled-live proof without a new approved gate.

## Locked Product Behavior

- METAR weather comes only from VATSIM sources.
- IFR Departure tracks and caches only the departure airport METAR.
- IFR Enroute tracks and caches only the arrival airport METAR.
- VFR retains the departure airport as its automatic primary METAR.
- In IFR or VFR, the pilot may explicitly request another airport's METAR.
- A pilot lookup temporarily owns the METAR drawer, not the ORB.
- After the bounded lookup spotlight, the drawer returns to the pinned primary
  unless a newer pilot drawer selection owns the display.
- The lookup may remain in truthful newest-first history.
- The METAR ORB always represents the current automatic primary airport.
- Before usable primary weather, the ORB contains only `METAR`.
- After successful acceptance, the ORB contains only the ICAO and one of
  `VFR`, `MVFR`, `IFR`, or `LIFR`, with the corresponding category color.
- The drawer contains the full accepted raw METAR plus truthful state and
  history information.

## Non-Negotiable Authority Architecture

**The Brain is the sole product and semantic decision-maker.**

Consequences:

- Modules and workers perform only bounded mechanical work commanded by the
  Brain.
- Workers return facts to the Brain and make no product decisions.
- Workers do not communicate with or wake one another.
- The Brain decides targets, acceptance, classification consequences, primary
  ownership, lookup spotlight, history, drawer ownership, publication intent,
  refresh eligibility, and lifecycle behavior.
- The Brain may wait logically and asynchronously for required facts, but it
  never blocks the simulator or draw thread.
- The overlay performs mechanical layout, preparation, rendering, and
  publication and cannot reinterpret Brain state.
- The mouse callback mechanically captures the exact clicked drawer, enqueues
  one immutable fact, requests bounded next-cycle service, and returns.
- The mouse callback cannot call the Brain, alter semantic state, render,
  prepare content, or change window geometry.
- FIFO input facts are consumed by the Brain on a safe simulator cycle.
- There is no render-dependent input lock, competing semantic completion path,
  secondary presentation authority, or worker-to-worker path.

## Step 4 Corrections Completed

1. Corrected the WinHTTP callback lifecycle by registering and waiting for
   `SENDREQUEST_COMPLETE`.
2. Restricted production weather to VATSIM.
3. Reduced the METAR ORB to neutral `METAR` or exact ICAO/category content.
4. Increased successful ORB typography to a readable centered and unclipped
   size.
5. Corrected repeated identical-lookup spotlight expiration and accessory
   liveness.
6. Corrected automatic primary METAR publication to the ORB.
7. Removed competing hidden preparation/commit authority and restored
   Brain-exclusive accessory ownership.
8. Corrected reentrant mouse handling with capture-only FIFO input facts and a
   bounded next-cycle wake of the existing flight loop.

These corrections are offline-proven. The latest input-boundary correction has
not received a valid live accessory exercise because the latest controlled-live
attempt stopped in Phase A before xPilot started.

## Latest Controlled-Live Result

`STEP 4 ACCESSORY INPUT BOUNDARY CONTROLLED-LIVE REPROOF FAILED — ROLLBACK COMPLETED`

The exact approved binary loaded and displayed the neutral startup UI. Before
xPilot started or any pilot input occurred, diagnostics reported:

```text
Presentation command: 1
Click sequence: 0
Command elapsed: 62,906,600 µs
Contract limit: 500,000 µs
```

The mandatory stop rule was invoked immediately.

- xPilot was never started.
- No VATSIM request occurred.
- No KDFW, KABQ, or KSAN live weather phase occurred.
- No accessory click was exercised.
- All later phases were skipped.
- XVatsim closed immediately through Plugin Admin.
- X-Plane shut down normally.
- Manifest-driven rollback passed.

Latest evidence:

- Root:
  `outputs\v2_step_04_accessory_input_boundary_controlled_live_reproof_evidence`
- Physical files: `34`
- Manifest rows: `33`
- Manifest SHA-256:
  `01D7B8CFB45411237182A7192E6F1923ED396AB4FF59BAEF5BD8ECCBAF440BCA`

Latest backup:

- Root:
  `outputs\v2_step_04_accessory_input_boundary_controlled_live_reproof_backup`
- Physical files: `14`
- Manifest SHA-256:
  `6BFAB3DE6CDA4184B67B16BBCCF8B9DCD99ECF140F3F872CEB60F67FCD4B545E`

Both roots are protected and must remain byte-identical, untracked, unstaged,
uncommitted, unmoved, and undeleted.

## Diagnosed Engineering Cause

The reported `62.9066` seconds was not evidence of 62 seconds of CPU work, an
accessory input failure, network delay, or a simulator-thread stall.

Command 1 was the initial neutral presentation command:

- it had no pilot click;
- it was created during startup or while the overlay was not yet eligible to
  produce a visible frame; and
- its wall-clock timer included time intentionally spent hidden or not visibly
  eligible.

The current `OverlayWindow` implementation starts command timing when a command
is first observed. A command that requests raster work can remain marked as
awaiting first frame. A later synchronization of the same command can encounter
zero new raster work and report `Committed` without correctly resolving the
existing awaiting-first-frame state. `BrainOwnedRuntime` then applies a blanket
500 ms limit to the accumulated `commandElapsedMicroseconds`.

Relevant current-source audit locations:

- `modules\overlay\src\OverlayWindow.cpp`
  - command issue and timer initialization;
  - zero-raster `Committed` handling;
  - awaiting-first-frame identity;
  - first-frame terminal publication.
- `brain\src\BrainOwnedRuntime.cpp`
  - blanket presentation-command liveness threshold.

The next session must verify these locations against current source before
proposing a change.

## Required Engineering Direction

The next correction must distinguish hidden commitment from visible
publication:

- A command issued while no visible frame is required must commit the latest
  Brain state and terminally report a named result such as `CommittedHidden` or
  `CommittedNoVisibleFrameRequired`.
- It must not wait for a visible frame merely to become terminal.
- A visible command must measure visible-eligibility-to-first-frame separately.
- A click-driven command must retain the strict click-to-terminal limit below
  500 ms.
- Repeating the same command while it awaits first visible frame must not
  downgrade, duplicate, or contradict its terminal result.
- Later overlay visibility starts a separate visibility-request-to-first-frame
  measurement.
- A hidden command's old issue timestamp must not become the visible-frame
  timer.
- Diagnostics must separately serialize issue-to-commit,
  visible-eligibility-to-first-frame, click-to-terminal, and intentionally
  hidden duration.
- Do not loosen the 500 ms input requirement.
- Do not add recurring polling, blocking waits, a second semantic owner,
  another publication path, or worker-to-worker communication.

## Protected Evidence And Restored External State

The original protected evidence remains `116/116`, with fingerprint:

`B63A3726E4B785FD80D982AA564A63124D0F815CBFBC9A579AC4E00CF4956D51`

All prior Step 4 controlled-live evidence manifests were reverified with zero
mismatches before this handoff update. Do not modify, normalize, rename,
regenerate, stage, commit, or clean any prior evidence, screenshot, diagnostic,
manifest, or backup.

Restored external state:

- X-Plane/xPilot processes: `0/0`
- Active/staged `.xpl`: `0/0`
- Active `win_x64`: absent
- `V2 Test`: `50` files and `29` directories, unchanged
- `XVatsim.prf` SHA-256:
  `830ABE79983B244D6280EBBA62A99E54601A6927C928B7332B0F2FA0CCCDF006`
- X-Plane `Log.txt` SHA-256:
  `6E049A3B0BF02A141EB4D44F44A501EA2D6B5C1788CB9EC805740196AB16E65F`
- Active XVatsim root outside `V2 Test`: only the `logs` directory and four
  restored historical diagnostic logs
- V1.2.3: untouched

## Current Roadmap Status

- Steps 1-3: accepted and complete.
- Step 4: implementation continues.
- Step 4 current code: offline-proven through 700 scenarios.
- Step 4 controlled-live proof: incomplete.
- Latest live attempt stopped before live VATSIM contact.
- Latest accessory input correction: not yet live-proven or live-disproven.
- KABQ lookup: not re-exercised after the latest architectural corrections.
- KSAN Enroute: not re-exercised after the latest architectural corrections.
- Arrival: `OUT OF SCOPE — UNCHANGED AND OFFLINE-PROVEN`.
- Production deployment: not authorized.
- V1.2.3: untouched.

## Exact Next Action

The next session begins in analysis and Contract Gate preparation mode:

1. Verify the documentation commit's parent and repository locks.
2. Read this handoff and the latest correction/live evidence.
3. Perform a narrow read-only source audit of hidden commitment, visible
   eligibility, first-frame terminal publication, and Brain liveness timing.
4. Prepare a corrective Contract Gate for Darron and ChatGPT Director review.
5. Do not implement until Darron explicitly approves that gate.
6. The first approved gate may authorize implementation and offline proof only.
7. A later controlled-live reproof requires a separate gate and explicit
   approval.

## Required Next-Session Reading Order

1. `docs\NEXT_SESSION_START_PROMPT.txt`
2. `docs\NEXT_SESSION_HANDOFF.md`
3. `docs\MILESTONE_STATUS.md`
4. `docs\V2_STEP_04_ACCESSORY_INPUT_BOUNDARY_CORRECTION_ENGINEERING_BRIEF.md`
5. `outputs\v2_step_04_accessory_input_boundary_correction_receipt.md`
6. `outputs\v2_step_04_accessory_input_boundary_correction_offline_proof_summary.md`
7. `outputs\v2_step_04_accessory_input_boundary_controlled_live_reproof_evidence\37_final_summary.md`
8. `outputs\v2_step_04_accessory_input_boundary_controlled_live_reproof_evidence\33_terminal_publication_ledger.tsv`
9. `outputs\v2_step_04_accessory_input_boundary_controlled_live_reproof_evidence\34_phase_table.tsv`
10. `outputs\v2_step_04_accessory_input_boundary_controlled_live_reproof_evidence\36_rollback_receipt.md`
11. The current `OverlayWindow.cpp` and `BrainOwnedRuntime.cpp` timing and
    publication paths identified above.

If a referenced filename differs, locate the corresponding existing artifact
without renaming or rewriting evidence.

Stop if the branch, documentation-commit parent/subject/scope, tracked state,
scenario set, protected evidence, or external restored state differs. Do not
build, deploy, start applications, contact VATSIM, or begin implementation
without a new explicitly approved Contract Gate.
