# Incremental Live Authority Refresh Rollback Plan

Prepared after Product Owner approval and before implementation on
2026-10-26.

Repository baseline:
`6b39f7b2351abc2b4c635b4d13650b62e9563d9f`

The checkpoint preserves the complete approved dirty state:

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `pre_implementation_tracked.patch` | 948,438 | `95B688508E451086EF6EFAB0470B973440B31495CE0D82FBBCBDFBB7BB39C09C` |
| `pre_implementation_approved_untracked.zip` | 134,783 | `D0A73E89E5A6DB3B1622B9393BD5E54B2E2EA7105AA7DBF97731A233C341F577` |
| `pre_implementation_status.txt` | 14,770 | `79800B9037982B668B472FBB4D04C1D4696EB3E7AE39F952BBD25A91D0964A28` |
| `pre_implementation_untracked_manifest.txt` | 4,126 | `FA030F028C25DCA6D86C0116D3EE3084964DFD1380885C34CA37BBD12F506B58` |

The approved untracked manifest contains 45 files. The archive was created
from that manifest before this rollback directory was added to the worktree.

## Recovery

1. Preserve the failed implementation and its diagnostics before recovery.
2. Create a separate checkout at the exact baseline commit.
3. Verify every checkpoint hash before use.
4. Apply `pre_implementation_tracked.patch` to that separate checkout.
5. Extract `pre_implementation_approved_untracked.zip` without overwriting the
   active worktree.
6. Compare the reconstructed tree with
   `pre_implementation_status.txt` and
   `pre_implementation_untracked_manifest.txt`.
7. Restore only reviewed affected files to the active tree; never use a
   destructive reset of the dirty worktree.
8. Rebuild and rerun the canonical-map, terminal-relationship, Center-click,
   Standby Assist, and authority-worker focused suites.
9. Do not replace the installed XPL while X-Plane is running.
10. Any later installed-binary rollback requires a separately preserved XPL
    backup, verified hashes, and Product Owner authorization.

No runtime switch may restore a parallel authority path. Recovery restores one
reviewed source state and one production decision owner.
