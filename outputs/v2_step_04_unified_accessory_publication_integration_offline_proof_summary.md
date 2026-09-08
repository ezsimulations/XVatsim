# Step 4 Unified Accessory Publication Integration — Offline Proof Summary

Date: 2026-08-29

## Result

`STEP 4 UNIFIED ACCESSORY PUBLICATION INTEGRATION — OFFLINE-PROVEN, NOT CONTROLLED-LIVE-PROVEN`

## Red and implementation proof

- Required red reproductions before correction: `0/5` passed, `5/5` failed.
- Corrected queue/coordinator/Brain/diagnostic integration: `10/10`.
- Legacy begin function, independent attempt counter, and legacy awaiting fields: removed.
- Same-command hidden-terminal then visible-attempt queue sequence: accepted.
- Combined command/attempt fact: both domains accepted atomically.
- Bounded capacity: one real failure, one retained immutable fact, one bounded retry, one delivery, zero duplicate delivery.
- Visibility loss reason: `visible-eligibility-lost-before-first-frame`.

## Measured startup seam ledger

`physical_produced=2 physical_consumed=2 brain_accepted=2 brain_rejected=0 command_terminals=1 attempt_terminals=1 diagnostics=2 queue_rejections=0 unchanged_attempts=0 liveness_failures=0 visibility_epoch=1 attempt_identity=1 visible_us=100`

The hidden fact was built by the production hidden-terminal builder, enqueued,
dequeued, accepted by the Brain, and serialized. The visible fact was generated
from the production coordinator's eligibility and draw completion, then used
the same queue, Brain consumer, and production serializer.

## Focused and final proof

- Visibility edge: `20/20`.
- Hardened hidden publication: `17/17`.
- Accessory-input boundary: `26/26`.
- Brain-exclusive accessory: `44/44`.
- Relevant Step 4 focused: `247/247`.
- Step 3 focused: `31/31`.
- Final complete regression, run once after focused proof: `747/747`.
- Scenario lineage: `737 + 10`.
- Canonical fingerprint: `173A7BE2B0D4D2DEC2BEB94F06ABC0F4905E68420FA4469ECE92F6907CF695F2`.

## Test-quality audit

- Visibility/integration scenario files: `30`.
- Unique probes: `30`.
- Duplicate probe mappings: `0`.
- Source-string-only behavioral proofs in this set: `0`.
- Literal claimed ledger counts: `0`.
- Direct expected-fact injection used as production-seam proof: `0`.

## Stress and isolation

- 1,000-action stress: issued/terminal `1000/1000`, drops `0`, pending `0`, maximum `100 µs`.
- Warm idle: `100,000` cycles with zero recurring accessory work.
- Worker cancellation maximum: `15 ms`.
- Real WinHTTP loopback: `15 ms`.
- Visual proof: 43 PNGs twice, zero differences.
- Fixture isolation: normal option OFF, fixture marker absent.

## Fixture-off payload

- `build/v2-step4-accessory-input-boundary-release-normal/dist/XVatsim/win_x64/XVatsim.xpl`: `2,304,000` bytes, SHA-256 `52CE56D41A3FD2CF7111216104F03B2E239BA52CAC76AD517A0ED9B11CB1A8BE`.
- `authority_source_registry.json`: `40,340` bytes, SHA-256 `3676CA43E5AFB8A5E443FDE6D04616918029E004E17EFAA022695D91E23DB60B`.
- `ui_transition.mp3`: `27,116` bytes, SHA-256 `C7BBE97DAD356C68FDFE40F9E8C1CF4EADEBCCD125463214F963F66356F8D9F1`.

No application was started, no payload was deployed or staged, and no live
request was made.
