# Step 3 deterministic visual proof

`XVatsimStep3VisualProof` is an offline proof tool. It reuses the production accessory layout, typography, preparation-worker, presentation, render-plan, and GDI+ raster paths.

For each independently created visual state, the tool assigns a unique monotonically increasing proof generation. Populated drawer content is projected into an immutable brain-approved preparation snapshot and submitted through the production `AccessoryPreparationWorker` lifecycle:

1. asynchronous `Start`;
2. bounded nonblocking `Request` retries;
3. exact-key `TryTakeReady` checks;
4. immutable prepared-plan publication to `UpdateAccessoryPresentation`;
5. explicit `Stop` with zero surviving worker threads.

The standalone tool may wait for worker startup and exact-generation preparation because it does not run on the simulator thread. These durations are reported as offline worker-preparation wait and are not described as X-Plane main-thread work.

The tool writes two manifests:

- `DETERMINISTIC_SHA256SUMS.txt` covers every timing-free image, geometry, text-layout, and summary artifact. Two clean runs must match this manifest and every listed file byte-for-byte.
- `SHA256SUMS.txt` is the complete per-run manifest. It additionally covers timing-bearing performance evidence, so separate runs are not expected to have identical complete manifests.

Worker startup failure, preparation timeout, a mismatched generation, PNG failure, layout violation, pixel mismatch, zero-work violation, or timing violation causes a nonzero exit.
