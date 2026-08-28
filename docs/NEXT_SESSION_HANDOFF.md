# XVatsim Next Session Handoff

Updated: 2026-08-27

Status: Steps 1 through 3 accepted and complete; Step 4 has not begun.

## Authoritative Repository State

```text
Repository: C:\Users\DARRON\OneDrive\Documents\XVatsim-V2
Branch: v2-development
Accepted Step 3 product/receipt closeout: 0c4ac545c1e484d5029e465e9eb99aa49fd19b08
```

The handoff documentation commit will be the current `HEAD` after this
documentation slice is committed. It cannot embed its own final SHA because
that SHA depends on the committed document bytes. Its required identity is:

- parent: `0c4ac545c1e484d5029e465e9eb99aa49fd19b08`;
- subject: `docs: prepare V2 Step 4 session handoff`; and
- scope: exactly `docs/NEXT_SESSION_HANDOFF.md`,
  `docs/NEXT_SESSION_START_PROMPT.txt`, and `docs/MILESTONE_STATUS.md`.

Expected next-session state after that documentation commit:

- tracked changes: 0;
- staged changes: 0;
- saved regression scenarios: 494;
- intentionally preserved untracked evidence files: 116, all under `outputs/`;
- X-Plane and xPilot: stopped; and
- active and staged `.xpl` files: 0.

The 116 local evidence files are local historical proof under `outputs/`, not
active implementation changes. Their inventory fingerprint is:

`B63A3726E4B785FD80D982AA564A63124D0F815CBFBC9A579AC4E00CF4956D51`

Do not clean, move, stage, commit, modify, or treat them as unexplained source
work without a separately approved cleanup gate.

## Working Arrangement

- Darron is the product owner, final decision-maker, and bridge between the
  Director and Engineer tasks.
- ChatGPT is the Director. It owns roadmap interpretation, bounded engineering
  briefs, Contract Gate review, acceptance criteria, and acceptance or rejection
  of completed work.
- Codex is the Engineer. It inspects the repository, proposes exact scope and
  proof, implements only after explicit approval, and produces reviewable
  evidence and narrow commits.
- The repository is the durable shared memory. Separate ChatGPT and Codex tasks
  must read the committed handoff, roadmap, contracts, receipts, and Git history;
  they must not assume that chat context transfers automatically.

The governing architecture rule remains:

`Brain decides. Modules produce facts. UI displays brain-approved facts.`

## Product And Platform Boundary

XVatsim V1.2.3 remains the closed public freeware baseline for Windows x64,
X-Plane 12, and xPilot. Its release commit is
`e4a626975513db93105b9c625f4fbb941e7f9c05`, and its annotated release tag is
`v1.2.3`. V2 work must not reopen, repackage, restore, or otherwise modify the
V1.2.3 release unless Darron separately authorizes a Version 1 defect response.

V2.0 remains Windows-only. Mac and Linux work is deferred until native build,
validation, and live-test resources are available. Neither platform is a V2.0
acceptance requirement.

## Accepted V2 Milestones And Commit Lineage

### Step 1 - Windows V2 Proof Baseline

Accepted and complete.

- Starting roadmap commit:
  `93dc18e41a3a3a2a891aa133a76b03d8871b9cc3`
- Implementation and receipt commit:
  `2684b2f80c112673b8b9326edee1b0840c0ea9e4`
- Receipt:
  `outputs/v2_step_01_windows_proof_baseline_receipt.md`
- Committed receipt SHA-256:
  `7A762200007C2AB0817C2DBE1997624F7CA17416A542012B641C153C18A39727`

Step 1 established the repeatable, clean Windows Release proof runner, locked
scenario discovery/counting/ordering, binary hashes, elapsed-time evidence,
atomic receipt publication, and intentional negative-count propagation.

### Step 2 - Explicit IFR/VFR Mode Foundation

Accepted and complete.

- Starting commit:
  `2684b2f80c112673b8b9326edee1b0840c0ea9e4`
- Proven implementation commit:
  `8b10841fd19a1ee1a908b74ffd3246e36b029647`
- Original receipt closeout:
  `eaf57da8359e14ab0b23cff3bc47f1f4e04f698e`
- Proof-integrity correction:
  `fc6ee9bd8962f8ddd983cfeb08875c54d4fc706d`
- Corrected receipt closeout:
  `ac0f86261ebb12c7cdd0fc166f971ea738ad1f22`
- Receipt:
  `outputs/v2_step_02_ifr_vfr_mode_foundation_receipt.md`
- Committed receipt SHA-256:
  `9C7770DA29B354C1DE2F35224D9AFF9F0D02BC4658B8885EEDC3EA3C8F1FABF4`

Step 2 established explicit, persistent, brain-owned IFR/VFR selection without
changing controller, workflow, route, frequency, or main-card output behavior.

### Step 3 - ORB Rail And Information Drawer Foundation

Accepted and complete.

- Starting commit:
  `ac0f86261ebb12c7cdd0fc166f971ea738ad1f22`
- Proven implementation and evidence commit:
  `219aa594adccb02143d02ea2f725761d85a6fe5f`
- Receipt-only closeout:
  `0c4ac545c1e484d5029e465e9eb99aa49fd19b08`
- Receipt:
  `outputs/v2_step_03_orb_rail_information_drawer_receipt.md`
- Committed receipt SHA-256:
  `4EEFEA63980736A74B1621A8005AF3383299946C14CC4658FA41F156CBEF84A9`

## Current Regression Baseline

- Saved scenarios: 494
- Canonical release-gate scenario fingerprint:
  `16AA355061096CC9EBB87A1E437422FEABB8B45CDF41D7057FF9853802BF8BCB`
- Fingerprint ordering: ordinal case-insensitive filename ordering with an
  ordinal tie-break.
- Fingerprint serialization per scenario: UTF-8 repository-relative path, NUL,
  raw scenario bytes, NUL.

The accepted Step 3 commit-bound Windows proof configured and built the Release
harness and fixture-OFF plugin, then passed 494/494 scenarios. Its intentional
495-versus-494 negative test failed before configuration and left the successful
receipt unchanged.

## Proven Step 3 Architecture

- Three attached ORBs—METAR, ATIS, and PDC—share the existing single XPLM
  overlay window.
- Only one drawer surface can be visible at a time.
- METAR, ATIS, and PDC/private-message data have three independent,
  brain-owned, process-memory histories.
- The brain owns selection, newest-first ordering, stable-key deduplication and
  replacement, count and byte limits, oldest-first eviction, accepted sequence,
  generations, preservation, and lifecycle clearing.
- The overlay owns bounded click facts, presentation-only scrolling, shared
  geometry, clamping, hit testing, rasterization, textures, and rendering.
- One event-driven Windows preparation worker runs below normal priority.
- The worker consumes immutable brain-approved snapshots and returns immutable
  plans tagged for exact-generation publication.
- Enqueue and ready-plan publication use bounded nonblocking handoffs.
- The worker cannot access XPLM, OpenGL, settings, files, network sources,
  plugin lifecycle callbacks, or mutable brain state.
- Accessory textures render on change. Once warm and unchanged, there is no
  recurring accessory history traversal, wrapping, GDI+ measurement, enqueue,
  rasterization, upload, or diagnostic work.
- Disable and stop cancel and join preparation safely; accepted live shutdown
  evidence shows zero surviving worker threads.

Normal Step 3 contains no live METAR, VATSIM ATIS, PDC, or private-message
source. Synthetic proof fixtures are compile-time isolated, linked only when
`XVATSIM_STEP3_LIVE_PROOF_FIXTURES=ON`, and absent from the normal plugin.

## Step 3 Live Performance Conclusion

The final fixture-ON reproof completed 1,079 actions with zero synchronous-wall,
render-wall, timing, cadence-contract, missed-draw, or threshold failures.
Observed maxima were 86 microseconds for dispatch, 3,631 microseconds for the
matching action draw, 3,643 microseconds for combined synchronous action work,
2,534 microseconds for drawer rasterization, 210 microseconds for drawer upload,
and 188 microseconds for completed accessory draw.

The final fixture-OFF smoke completed 13 actions with no hard failure. Its
combined synchronous action maximum was 1,357 microseconds, rasterization
maximum was 668 microseconds, OpenGL upload maximum was 74 microseconds, and
completed accessory draw maximum was 137 microseconds.

Worker preparation is asynchronous and is reported separately from
simulator-thread work and X-Plane frame cadence. The longest fixture-ON
preparation wait was 2,227,032 microseconds; the fixture-OFF cold preparation
wait was 256,838 microseconds. Neither was represented as synchronous
simulator-thread computation, and neither produced a visible UI failure.
Controlled idle periods showed zero recurring accessory work.

The accepted one-time flight-plan construction spike is established V1
behavior covering initial flight context, frequency ordering, route resolution,
and polygon-map construction. It is outside Step 3 accessory performance
classification. Future V2 work must continue guarding against recurring
flight-cycle CPU work, stutter, polling, repeated parsing, unnecessary
rendering, repeated network work, and diagnostic spam.

## Controlled External Rollback

Step 3 controlled rollback passed. The X-Plane plugin root, XVatsim
preferences, and relevant logs were restored to their verified pretest state.

- Active `.xpl` files: 0
- Staged `V2 Test` `.xpl` files: 0
- `V2 Test` and the Step 3 pretest backup remain preserved.
- V1 was not searched for, moved, restored, or modified.
- Rollback manifest SHA-256:
  `D3943D78D037203E1A70847B38001D75854CFA7FD734A8E3B98A4C683564E89E`
- Pretest backup manifest SHA-256:
  `A0B19A2ED9A37240CF11B78FF51F2FC964D652D64C94E4148868FCD8FCF4287D`

## Next Roadmap Boundary

The next roadmap item is Step 4 - METAR. The roadmap describes a removable
METAR fact worker, bounded airport cache, parser/classifier, brain-owned target
and freshness decisions, manual target/revert controls, and METAR ORB/drawer
publication.

Step 4 has not begun. This handoff is not implementation authorization. The
Director must issue a bounded Step 4 brief, Codex must inspect without editing
and present a Contract Gate, and Darron must explicitly approve that gate before
any Step 4 source, test, build, deployment, or live work.

### Director Next Action

- Verify the handoff state.
- Review Step 4 METAR product decisions.
- Prepare a bounded Step 4 engineering brief.
- Do not authorize implementation until the Contract Gate is reviewed and
  Darron approves it.

### Engineer Next Action

- Verify the handoff state.
- Perform only a read-only METAR/source/architecture audit after receiving the
  Director brief.
- Present an exact Step 4 Contract Gate.
- Do not edit, build, deploy, test live, or commit Step 4 work before Darron's
  explicit approval.

## Required Next-Session Startup

Run:

```powershell
git status --short --branch
git log -8 --oneline --decorate
git rev-parse HEAD
git rev-parse HEAD^
git show -s --format=%s HEAD
git diff-tree --no-commit-id --name-only -r HEAD
```

Resolve the documentation commit SHA with `git rev-parse HEAD`. Verify:

- branch: `v2-development`;
- current `HEAD` subject: `docs: prepare V2 Step 4 session handoff`;
- current `HEAD` parent:
  `0c4ac545c1e484d5029e465e9eb99aa49fd19b08`;
- current `HEAD` scope: exactly the three handoff documentation files;
- tracked and staged changes: 0;
- preserved untracked evidence files: 116;
- saved scenarios: 494;
- active/staged `.xpl` files: 0/0; and
- Step 4 implementation: not begun.

Then read these primary sources in order:

1. `docs/NEXT_SESSION_HANDOFF.md`
2. `docs/V2_0_0_ROADMAP.md`
3. `docs/V2_EXECUTION_PROTOCOL.md`
4. `docs/ARCHITECTURE.md`
5. `brain/README.md`
6. `modules/overlay/README.md`
7. `outputs/v2_step_01_windows_proof_baseline_receipt.md`
8. `outputs/v2_step_02_ifr_vfr_mode_foundation_receipt.md`
9. `outputs/v2_step_03_orb_rail_information_drawer_receipt.md`

Additional references may then follow:

10. `docs/BRAIN_OWNED_RUNTIME_CONTRACT.md`
11. `docs/MILESTONE_STATUS.md`

Stop if the branch, documentation-commit parent/subject/scope, tracked state,
scenario count, protected evidence, or external rollback state differs from
this handoff. Do not clean the preserved `outputs/` evidence to obtain a
visually clean status.
