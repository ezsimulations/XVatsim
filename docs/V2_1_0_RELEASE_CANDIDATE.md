# XVatsim V2.1.0 Release Candidate

Candidate freeze date: 26 September 2026

Baseline: V2.0.2 tag `b1f968f`

Implementation baseline: `627fbb96d9234bb05ba1ce10c6acc2e6827e02b6`

## Candidate purpose

V2.1.0 adds the read-only xPilot 4 companion bridge and the standalone XVatsim
Manager while preserving XVatsim's established xPilot 3 integration. The
companion reports raw SDK events and controller observations. It contains no
controller-selection or message-admission decision fields. Workers continue to
report facts and simple evidence; the XVatsim Brain remains the only product
decision owner.

The Manager detects validated X-Plane and xPilot installations, installs only
an allow-listed xPilot 4 companion, preserves the legacy xPilot 3 path, verifies
every embedded file by SHA-256, and performs transactional backup and rollback.
It is a separate Windows application and does not run in the simulator flight
loop.

The candidate also includes the already live-tested METAR classification and
age display, xPilot 4 PDC startup recovery, and wrapped PDC Drawer text from the
implementation baseline.

## Frozen artifacts

- XPL SHA-256: `0D4ACC2A99309EE3ACC640798C286144044E57595E93AD8F7364485A65CBAEEF`
- xPilot 4 bridge SHA-256: `33DF8B96B62356C01E3DAD09E38198D3E7118F688421E83D6C797BAF6EA7CFA0`
- Manager payload SHA-256: `3AEC1AFE3D9674E36399A35BCEEA15A6DAE1950235FBB5001E5C878D32C356D3`
- Unsigned Manager SHA-256: `F197731DEDFC8BDBDFAD1412FDCC7EF1C5CE73B5B8897EC674F26890EDBCA067`

The Manager embeds exactly the XPL above, the bridge above,
`ui_transition.mp3` at
`C7BBE97DAD356C68FDFE40F9E8C1CF4EADEBCCD125463214F963F66356F8D9F1`,
and `authority_source_registry.json` at
`3676CA43E5AFB8A5E443FDE6D04616918029E004E17EFAA022695D91E23DB60B`.

## Verification

- Fresh Visual Studio 2026 x64 Release configuration and build: PASS.
- Environment-independent saved scenarios: `887 / 887` PASS.
- Focused C++ contract probes: `11 / 11` PASS.
- xPilot 4 real named-pipe stress proof after timing-race correction: `15 / 15`
  consecutive PASS.
- xPilot 4 SDK companion probe: `21 / 21` PASS.
- Manager isolated probe: `24 / 24` PASS.
- Product Owner manager scenario matrix: PASS.
- V2.0.2 to V2.1.0 update using the frozen payload: PASS.
- Clean V2.1.0 first installation using the frozen payload: PASS.
- Restoration of all unmanaged diagnostics and legacy backups without changing
  managed files: PASS.
- Public V2.0.2 manifest against installed V2.1.0 remains silent/current: PASS.

The one saved scenario excluded from the environment-independent total checks
the exact legacy xPilot 3.0.2 simulator-plugin fingerprint at its real X-Plane
path. This machine currently contains the approved xPilot 4 Beta 7 simulator
plugin there. The legacy xPilot 3 manager path and no-bridge behavior passed the
Product Owner scenario matrix; the exact fingerprint scenario remains an
environment-specific release check when the legacy binary occupies that path.

## Publication boundary

This file records a release candidate. It does not authorize publication. The
public `docs/xvatsim_update.json` remains at V2.0.2 so users are not notified
before a signed Manager and the approved V2.1.0 download are available.

Before public distribution:

1. Authenticode-sign and timestamp the exact Manager candidate.
2. Verify the signed file hash and embedded payload identity.
3. Run a clean standard-account Windows installation test with the signed file.
4. Obtain Product Owner approval of the final downloadable package.
5. Publish the package and GitHub release before updating the public manifest.
