# XVatsim V2 Step 4 Accessory Liveness Correction Receipt

## Disposition

`STEP 4 ACCESSORY LIVENESS CORRECTED AND OFFLINE-PROVEN — CONTROLLED LIVE REPROOF NOT AUTHORIZED`

## Reproduced cause

The deterministic red proof reproduced the live freeze against the unmodified
production implementation. An accepted accessory action was bound to an exact
selection/render generation pair. When compatible METAR content advanced the
render generation from 1 to 2 before the matching draw, the newer draw did not
complete or cancel the action. The dispatcher remained in flight and blocked
later METAR, ATIS, and PDC clicks.

## Correction

- Accessory actions now bind the selected drawer as well as selection and render
  generations.
- A draw for the same selected drawer and selection generation may complete an
  action when its render generation exactly matches or compatibly supersedes the
  expected generation.
- Wrong-selection and older-generation draws cannot falsely complete an action.
- Failed deferred preparation bindings and lifecycle boundaries explicitly
  cancel and release their actions and clear deferred state.
- Compatible render supersession completes the associated performance action.
- Bounded aggregate diagnostics distinguish exact completion, compatible
  supersession, explicit cancellation, selection supersession, mismatch, queue
  depth, and maximum in-flight duration.
- Successful METAR ORB text now uses the established bold face at 10.0 design
  pixels while retaining the locked two-line ICAO/category presentation.

WinHTTP sequencing, source selection, endpoint construction, payload validation,
METAR parsing, category thresholds, targeting, cache authority, and refresh
cadence were not changed.

## Proof results

- Corrective scenarios: 29/29.
- Step 3 focused scenarios: 31/31.
- Step 4 focused scenarios: 96/96.
- Complete saved suite: 590/590; final pass 11.792 seconds.
- Canonical scenario fingerprint:
  `765008264996DBE603FFB6B30D9CD0000B3CA59FA37BBA8C1D62D16CFD47E498`.
- Stress: 1,000 compatible supersessions; 1,000 terminal completions; final
  queue depth zero; final in-flight false; zero unexpected drops, stale
  publications, ownership errors, delayed reopen, duplicate history, or
  identical-content parse.
- Maximum deterministic accessory action in-flight duration: 1 microsecond
  against the 500,000-microsecond limit.
- Warm unchanged proof: 100,000 cycles with zero parse, fingerprint, history,
  wrapping, raster, upload, publication, input-dispatch, or terminal-diagnostic
  work.
- Prompt cancellation: five-phase maximum 15 milliseconds; stalled loopback
  cancellation maximum 0 milliseconds; both below 500 milliseconds.
- Successful production WinHTTP loopback regression: complete send, response,
  JSON harvest, and one brain parse in 16 milliseconds on the final run;
  maximum observed in the corrective proof set was 20 milliseconds.
- Visual proof: 22 production-raster PNGs; repeated render differences 0;
  main-card signature outside the authorized ORB region unchanged at
  `8949928878432326300`.
- Visual evidence manifest SHA-256:
  `6F11239FDC091BF1522B9BA7B2BB7C3E9C5ACB1B54D3D329CF760C04B4E9FF48`.

## Windows Release and isolation

- Fresh proof Release configuration: plugin off, regression harness and visual
  proof on, fixtures off.
- Fresh normal Release configuration: plugin on, regression harness and visual
  proof off, fixtures off.
- Normal fixture-off plugin size: 2,407,936 bytes.
- Normal fixture-off plugin SHA-256:
  `88BD236C879547F1C6BE386AB32BA8BDDEA50ACCF12A6960248B0A35DEB70080`.
- Required production markers `metar.vatsim.net` and `VATSIM_METAR` are present.
- NOAA, Aviation Weather, loopback, synthetic METAR, injected transport,
  proof-endpoint, corrective-scenario, and proof-fixture markers are absent.
- Binary-isolation failures: 0.

The fresh Visual Studio 18 proof configuration initially encountered the known
experimental-coroutine SDK deprecation rejection before any target build. The
same fresh cache was configured with the repository's established warning-silence
definition; all required Release builds and proofs then passed. This was a
toolchain setup correction, not a production-source or contract deviation.

## Evidence protection and scope

- Original protected evidence: 116/116 hash-verified.
- First failed-live evidence: 34/34 hash-verified.
- Second corrected-live-reproof evidence: 46 physical files, 45 manifest
  entries, all verified; manifest SHA-256 remains
  `E3355DED8FFFD76BB904D12ABF7BA8013BD1901E2349181D9C8C34861D1D58BA`.
- Existing 38 staging files: all verified against the protected inventory.
- X-Plane/xPilot processes: 0/0.
- Active/staged `.xpl`: 0/0.
- Active `win_x64` deployment: absent.
- V1.2.3: untouched.
- Live VATSIM requests, deployment, and controlled live reproof: not performed.

## Commits

- Implementation and evidence: `793e83d` (`fix: restore Step 4 accessory rail liveness`).
- This receipt is recorded in the immediately following receipt-only commit.

## Contract deviations

None observed.

The newly built binary is not authorized for deployment. A separate controlled
live-reproof Contract Gate and Darron's explicit approval are required.
