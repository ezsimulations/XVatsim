# Canonical Authority Map Pre-Implementation Rollback Plan

**Created:** 2026-10-25  
**Baseline HEAD:** `6b39f7b2351abc2b4c635b4d13650b62e9563d9f`  
**Purpose:** Reconstruct the exact approved uncommitted working state that
existed immediately before implementation of
`CANONICAL-LIVE-AUTHORITY-POLYGON-MAP-01`.

## Preserved Files

### `pre_implementation_tracked.patch`

- Size: `161,989` bytes
- SHA-256:
  `33A8F90E46B9853546312B74B8C1A146A3AE95FA5A2F0161421E8AB1E5AA4885`
- Format: Git full-index binary patch
- Contents: every tracked modification and deletion relative to the baseline
  HEAD

### `pre_implementation_approved_untracked.zip`

- Size: `52,248` bytes
- SHA-256:
  `F0BEB5F35871EEE5A44D090253A7A3C5012ABBAA0685488B70607396A401222B`
- Format: ZIP archive with repository-relative paths
- Contents: all 17 untracked files present before implementation, including
  both authority contracts, prior proof artifacts, terminal relationship
  scenarios, and Center-frequency/Standby Assist scenarios

## Safe Recovery Procedure

Do not run a destructive reset against the active checkout.

1. Stop implementation and preserve the failed attempt as a separate patch and
   untracked archive.
2. Verify the two hashes above.
3. Create a separate recovery directory from baseline
   `6b39f7b2351abc2b4c635b4d13650b62e9563d9f`.
4. Apply `pre_implementation_tracked.patch` in that recovery directory.
5. Extract `pre_implementation_approved_untracked.zip` in that recovery
   directory.
6. Compare the recovered status, files, and focused test inputs with the
   pre-implementation manifest.
7. After review, restore only the intended affected files into the active
   checkout.
8. Rebuild and run the terminal relationship and Center-frequency/Standby
   Assist focused tests.
9. If an installed plugin must be rolled back, obtain separate Product Owner
   authorization and verify the exact plugin hash before replacement.

## Architectural Rollback Rule

Rollback is performed from preserved source state. No runtime feature flag may
reactivate the retired split polygon catalogs, terminal vote path,
transceiver/ATIS activation inference, or compatibility survivors.

