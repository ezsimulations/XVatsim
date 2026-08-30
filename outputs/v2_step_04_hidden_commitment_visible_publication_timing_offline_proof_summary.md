# Step 4 Hidden Commitment and Visible Publication Timing — Offline Proof Summary

Date: 2026-08-29

## Result

`STEP 4 HIDDEN COMMITMENT AND VISIBLE PUBLICATION TIMING CORRECTION — OFFLINE-PROVEN, NOT CONTROLLED-LIVE-PROVEN`

## Starting state

- Branch `v2-development`; HEAD `03c29a83d5e693b2e51b5f2463393f3df7ad7cc3`.
- Tracked/staged `0/0`; 391 standard untracked files, all under `outputs/`.
- Baseline scenarios `700`; fingerprint `C1367F24170283074D7D7371EFD4EDD1C687D7903F959CC3FA19FE3D807AAE8B`.
- X-Plane/xPilot `0/0`; active/staged `.xpl` `0/0`; active `win_x64` absent.
- Protected `V2 Test` `50` files and `29` directories.

## Red reproduction and clarification stop

Against unchanged production source, the initial twelve corrective scenarios produced `3/12` pass and `9/12` fail. The failures reproduced the missing hidden disposition, blanket hidden-duration liveness classification, absent applicability markers, missing visible-eligibility timer, visible-command reopening risk, and incorrect 499,999/500,000-microsecond boundary behavior.

After the initial correction, the mandatory focused proof stopped at brain-exclusive `43/44`: `v2_step4_brain_exclusive_red_cancelled_liveness_unreported` still encoded cancellation through aggregate `commandElapsedMicroseconds`. The Director clarification migrated that protection to applicable `issueToCancellationMicroseconds=600001` without weakening or renaming it.

## Resumed proof

- Hidden/publication/cancellation corrective: `17/17`.
- Accessory-input boundary: `26/26`.
- Brain-exclusive accessory: `44/44`.
- Step 3 focused: `31/31`.
- Step 4 focused: `223/223`.
- Complete regression: `717/717`.
- Scenario lineage: `700 + 17`.
- Canonical fingerprint: `C2D35F643D5ABA133D70ECDE8CBA3232D5A405A37B95EF31CADC3DA586DE0D22`.

The 1,000-click production-path stress recorded zero drops, exact 1,000-command terminal accounting, empty final queues, and maximum click terminal time 100 microseconds. The 100,000-cycle warm-idle proofs recorded zero recurring accessory work. Duplicate and out-of-order facts remained rejected, queue capacity remained bounded, worker cancellation maximum was 15 ms, and real WinHTTP loopback passed in 15 ms.

The production raster tool wrote 43 PNGs twice with zero repeat-render hash differences. The normal fixture-off isolation regression passed.

## Normal fixture-off payload

- `build/v2-step4-accessory-input-boundary-release-normal/dist/XVatsim/win_x64/XVatsim.xpl`
- Size: `2,295,808` bytes
- SHA-256: `0AFBA04808B61D7E92543F082C8D8562E011B5A216A6DB8975C9CF11D38451BF`
- `authority_source_registry.json`: `3676CA43E5AFB8A5E443FDE6D04616918029E004E17EFAA022695D91E23DB60B`
- `ui_transition.mp3`: `C7BBE97DAD356C68FDFE40F9E8C1CF4EADEBCCD125463214F963F66356F8D9F1`

No application was started, no payload was deployed or staged, and no live request was made.
