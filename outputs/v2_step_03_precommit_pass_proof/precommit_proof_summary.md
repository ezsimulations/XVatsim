# XVatsim V2 Step 3 precommit proof

Status: **PRECOMMIT PROOF PASSED**

This is a precommit proof record, not the final commit-bound Step 3 receipt. Repository HEAD remains `ac0f86261ebb12c7cdd0fc166f971ea738ad1f22` on `v2-development`; no commit was created.

## Authorized correction scope

- Modified: `tools/step3_visual_proof/src/main.cpp`
- Added documentation: `tools/step3_visual_proof/README.md`
- Replaced only the invalid visual artifacts in `outputs/v2_step_03_ui_evidence` with the proven final Run A output.
- Added this new precommit-pass evidence package and the new Windows precommit proof artifact.
- Production behavior, production source, scenarios, live evidence, controlled rollback state, and the failed precommit proof package were not changed.

The prior source-scope comparison checked 51 source/test files from the failed precommit inventory. The exporter source was the only changed existing file; zero unexpected production or test files changed.

## Exporter correction

The exporter now uses the production `AccessoryPreparationWorker` lifecycle: asynchronous startup, bounded nonblocking request submission, exact-key ready-plan retrieval, immutable brain preparation snapshots, exact prepared-plan publication to `UpdateAccessoryPresentation`, and explicit clean shutdown.

Each independently created visual state receives a unique monotonically increasing proof generation. A startup failure, preparation timeout, or non-matching generation is an infrastructure error. History-layout evidence is taken directly from the returned prepared plan; the exporter no longer substitutes the synchronous `BuildAccessoryHistoryLayout` path.

Offline worker startup and preparation waits are labelled as offline worker waits, not simulator-thread work. The worker completed 25 production preparation jobs, reported one successful below-normal-priority startup, and left zero surviving worker threads after `Stop`.

## Deterministic visual proof

- Fresh exporter SHA-256: `E3BC9593DC2BC3D520E7484973C2263A332829959E5A3D5D22C1CA20168C5260`
- Clean Run A: passed, zero contract failures.
- Clean Run B: passed, zero contract failures.
- Timing-free deterministic artifacts: 39.
- Byte mismatches: 0.
- Deterministic manifest for both runs: `22F4CE62B2E83C88B6AE532E44E7C56BFD06DA282E7088A722689DA6254AD519`.
- Run A complete manifest: `07B8DDF2F12DB57AB58DF3481088AAE8B3F79DB02BCD1CEE7470D14D5F716851`.
- Run B complete manifest: `9F484469D1DB8E6787F47C31DB35014DB1F7C30B10698EFB567937E51F793DBC`.
- Complete manifests are intentionally not claimed identical because each covers its run-specific timing CSV.

The proof explicitly passed:

- normal wrapped history visible and inside the drawer;
- long unbroken-token history visible and inside the drawer;
- newest retained history at the top;
- maximum scroll visibly reaching the correct final-history marker;
- top/bottom pixel differences confined to the drawer;
- visible `CONTENT LIMITED` treatment;
- 10,000 unchanged updates with zero history visits, copies, wrapping, GDI measurement, raster requests, or upload requests;
- pure translation with unchanged texture-local pixels, signatures, and zero-work counters;
- ORB label and `OPEN` fit at scales 0.85, 1.00, and 1.35;
- all screen clamping, compact-gap, movement, and closed-footprint checks;
- 1280x720 at scale 1.35 retaining 37 vertical pixels;
- six identical main-card crops with SHA-256 `310BFD37F2BD8B5D0689A1ED1BD98DB29E23F033AD20FE962C1E0907D2E34F6F` and zero differing pixels.

Maximum timing from final Run A for hard-limited offline work:

- brain projection: 1 microsecond;
- presentation publication: 8 microseconds;
- main-card GDI+ raster: 2,256 microseconds;
- ORB-rail GDI+ raster: 429 microseconds;
- drawer GDI+ raster: 838 microseconds;
- pure-translation update: 0 microseconds.

Worker preparation wait was recorded separately, with a maximum of 16,310 microseconds, and was not represented as simulator-thread work or an OpenGL upload.

## Regression and release proof

- Focused Step 3: discovered 31, executed 31, passed 31, failed 0, invalid 0.
- Legacy: discovered 463, executed 463, passed 463, failed 0, invalid 0.
- Complete: discovered 494, executed 494, passed 494, failed 0, invalid 0.
- Fixture verifier: 63/63.
- Windows Release proof: expected 494, discovered 494, executed 494, passed 494, failed 0.
- Intentional negative proof: expected 495, discovered 494, exit code 1 before configuration, build, or scenario execution.
- Successful Windows proof artifact remained unchanged after the negative test at SHA-256 `09454FD38C9F835EECF471307A7A784848088C04729523AE9878B3D8F3A9A1EF`.

Final binary hashes:

- Windows runner: `F7DD070C0A3671029EBE0E0D19D4FB2BF6C2DAC39D557F0D2DDF108908258BFC`
- Release harness: `5E19E84EE1C09D2FE82EE40D21B7345E0CC06ED39ACC79AE07353D95836E6574`
- Fixture-OFF plugin: `737706614048537D435A030F0E25BCAC192C61F6771EA041710C5A5A73ED2030`
- Fixture-ON plugin: `C14F41B68D06E5DCE6056458EF6BE40FFFDE6C9995CB6E6439FEDA0C2816C119`
- Fixture verifier: `752CBBF2079513134B16A9D4D8D1A6EA18D3ED342AA5D39000199F2F2565BA75`

The fixture-OFF cache records the option OFF and its binary contains neither fixture identity nor synthetic marker. The fixture-ON cache records the option ON and its binary contains both required identities.

## Scenario fingerprints

- Step 3 lineage fingerprint: `6E7A2EB5ED86FE23CFB2AAF161F4D329F6F6AE1086D1CCE5D4E2DE07B55200B5`.
- Canonical release-gate fingerprint: `16AA355061096CC9EBB87A1E437422FEABB8B45CDF41D7057FF9853802BF8BCB`.

The lineage value uses PowerShell culture ordering. The canonical release-gate value uses ordinal case-insensitive filename ordering with an ordinal tie-break. Both use the same UTF-8 relative-path, NUL, raw bytes, NUL serialization and cover the same 494 files. The canonical release-gate algorithm is used going forward; the lineage value remains preserved evidence.

## Historical and external-state protection

- The failed precommit package remains byte-identical at manifest SHA-256 `17B7EDD1A23EE311C0093FFDC2B6D99E83B90B7EC6EA486D92F8BB8EF5E602CC`.
- Controlled rollback manifest remains `D3943D78D037203E1A70847B38001D75854CFA7FD734A8E3B98A4C683564E89E`.
- Step 3 backup manifest remains `A0B19A2ED9A37240CF11B78FF51F2FC964D652D64C94E4148868FCD8FCF4287D`.
- X-Plane and xPilot remain stopped.
- Active `.xpl` count: 0.
- Staged `.xpl` count: 0.
- Step 1 receipt remains `7A762200007C2AB0817C2DBE1997624F7CA17416A542012B641C153C18A39727`.
- Step 2 receipt remains `9C7770DA29B354C1DE2F35224D9AFF9F0D02BC4658B8885EEDC3EA3C8F1FABF4`.
- V1 branches, tags, packages, manifests, hashes, and protected evidence were untouched.

## Scope and stop boundary

`git diff --check` and the staged whitespace check pass. No files are staged. Step 3 changed/new non-output files have zero final-newline or trailing-whitespace violations. The 15 repository-wide final-newline findings are unchanged baseline files outside Step 3 scope.

No deployment, X-Plane launch, commit, final Step 3 receipt, or Step 4 work was performed.

## Documentation-only correction and curated staging boundary

After Director acceptance of this precommit proof, only the following three
architecture documents were corrected to describe the proven Step 3 design:

- `docs/ARCHITECTURE.md`
- `brain/README.md`
- `modules/overlay/README.md`

This documentation-only correction changes no production or test behavior.
The accepted source, scenario, binary, visual, live-proof, rollback, and
historical-evidence hashes above remain the proof baseline.

The implementation/evidence candidate is curated explicitly. It includes the
approved Step 3 source and build integration, these three documents, both proof
tools, exactly 31 Step 3 scenarios, regression-harness implementation and
documentation, timing-free deterministic visual evidence, final fixture-ON and
fixture-OFF live evidence, rollback reports, and compact successful precommit
audits. A separate committed-evidence manifest covers only the evidence files
actually staged and is therefore checkout-verifiable.

The candidate excludes the final Step 3 receipt, the rejected precommit-proof
package, preliminary or superseded attempts, repeated raw regression logs,
binaries and build products, copied payloads, rollback payload copies, external
backup contents, the precommit Windows proof artifact, and all Step 1, Step 2,
V1, or unrelated files. Original evidence manifests that reference deliberately
unstaged payloads or ignored logs are retained as historical working evidence
but are not presented as checkout-verifiable committed manifests.
