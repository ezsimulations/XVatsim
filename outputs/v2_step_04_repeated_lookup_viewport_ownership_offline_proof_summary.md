# Step 4 Repeated-Lookup Viewport Ownership — Offline Proof Summary

Result: **PASS — offline only**.

The live C2 defect reproduced against unchanged production behavior: a second
identical KABQ lookup was accepted and published with zero parsing and zero
history mutation, but the drawer retained a nonzero viewport and hid the new
spotlight. The correction completes the existing Brain-owned
`scrollResetGeneration` contract through the immutable presentation snapshot,
exact presenter commit, first drawer raster, and publication diagnostics.

Measured proof:

- Corrective scenarios: 5/5.
- Single-snapshot: 12/12.
- Visibility/integration: 30/30.
- Hidden publication: 17/17.
- Accessory input: 26/26.
- Brain-exclusive accessory: 44/44.
- Step 3 focused: 31/31.
- Relevant Step 4: 264/264.
- Complete regression: 764/764.
- Fingerprint: `A0E94D47FF8E222ABA68C6D861653028D3BEDA294E9FC11D2207A4D8D3C50149`.
- 1,000 clicks: exact accounting, zero drops, maximum 100 us.
- 100,000 idle cycles: zero recurring accessory work.
- Worker cancellation: maximum 15 ms.
- Real WinHTTP loopback: 10 ms.
- Fixture isolation: passed.
- Visual proof: 43/43 twice; zero PNG, pixel, or dimension differences.
- Protected evidence: 53 manifests / 1,856 rows / 0 mismatches.

Repeated identical KABQ measured identities were request 3, lookup generation
2, and commands 5/6/7 for pending/spotlight/expiry. Reset generations were
5/6/7. The intended content was line 0 after each transition, with no duplicate
fact, stale rejection, queue loss, or liveness failure. Lost ownership preserved
ATIS, and an ordinary background mutation preserved offset 7.

No application was started, no payload was deployed or staged, and no live
request was made.

`STEP 4 REPEATED-LOOKUP VIEWPORT OWNERSHIP CORRECTION — OFFLINE-PROVEN, NOT CONTROLLED-LIVE-PROVEN`
