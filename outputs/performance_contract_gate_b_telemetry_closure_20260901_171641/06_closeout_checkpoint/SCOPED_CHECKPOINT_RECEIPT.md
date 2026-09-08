# Gate B scoped checkpoint receipt

Date: 2026-09-01  
Parent: `9440d6245f63e52b47de5fba99c6041ecfdcc31e`  
Status: **authorized for exact allowlist staging**

## Closure decision

Gate B is formally closed as an engineering and evidence pass. The controlling report classifies all four callbacks above `3 ms`; none contains synchronous route preparation. No additional route optimization or live flight is required.

## Verified acceptance

- Release build: pass.
- Frozen regression: `861/861`.
- Route-oracle parity: `64/64`.
- Combined authority/route parity: `64/64`.
- Gate B probe: pass; independent rerun submit P99/max `44/54 us`, harvest `5/26 us`, transition `17/548 us`, queue depth `1`.
- Telemetry probe: pass; `99/99` records, zero loss, exact saturation, shutdown drainage `8/8`.
- Controlled live callback timing and route evidence: contiguous and lossless.
- Source manifest: `19/19` Gate B source/probe files exact.
- Telemetry candidate source manifest: `10/10` exact.
- Staged source/probe `git diff --check`: pass.
- Full staged evidence check reports only intentional Markdown hard-break whitespace and byte-preserved raw X-Plane log whitespace; no source/probe whitespace errors are present.

## Repository policy

The checkpoint payload is the exact path list in `STAGED_PATHS.txt`. It contains:

- `19` Gate B source/build-registration/probe files;
- `5` Gate B contracts and engineering reports;
- `11` selected final proof, receipt, manifest, and raw shutdown-evidence files; and
- `3` closeout policy files, including this receipt.

No broad staging command is permitted. Candidate binaries, harness executables, intermediate live snapshots, unrelated untracked documents, unrelated output trees, and build products are excluded. Existing out-of-scope working-tree content remains untouched.

The accepted candidate remains installed at SHA-256 `CC42CD97A2655C1D8FB445FD128C85833E82BB7D535490EA4B08F310B2C5F512`. Product-Calm remains preserved for rollback at SHA-256 `3D8E35CBD3B0FAA96CEB24D5422BD73ECB928F8A7F9B7417249FA994E4CD4E70`.
