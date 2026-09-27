# XVatsim V2.1.0 Release Candidate

Candidate freeze date: 26 September 2026

Baseline: V2.0.2 tag `b1f968f`

Implementation baseline: `627fbb96d9234bb05ba1ce10c6acc2e6827e02b6`

Candidate source commit: `bd6bbe1150579922a53545a60b15be02bcbf0cf2`

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
- Manager payload SHA-256: `0CC61E580C11F1566B9D03544E108400C5B29F3ED08DA336698ACB906FA54367`
- Unsigned Manager SHA-256: `8AA67D8039742435B44F695E2C8B5DCB2751DE21C36612AE6C44A7629D3D52E0`
- Unsigned distribution ZIP SHA-256: `85603D6369938DCE471A0954D1C4A659CE14A26E547B29FE893A5ABB22D8E115`

The Manager embeds exactly the XPL above, the bridge above,
`ui_transition.mp3` at
`C7BBE97DAD356C68FDFE40F9E8C1CF4EADEBCCD125463214F963F66356F8D9F1`,
and `authority_source_registry.json` at
`3676CA43E5AFB8A5E443FDE6D04616918029E004E17EFAA022695D91E23DB60B`.

The final unsigned package is
`XVatsim_2.1.0_Freeware_Windows_XP12.zip`, contains 12 approved files, and is
64,417,929 bytes. Its `SHA256SUMS.txt` covers the 11 payload files other than
the checksum list itself. A separate clean extraction verified all 11 entries,
the exact Manager and XPL hashes above, intentional `NotSigned` status, and a
passing Manager embedded-payload self-test.

## Verification

- Fresh Visual Studio 2026 x64 Release configuration and build: PASS.
- Environment-independent saved scenarios: `887 / 887` PASS.
- Focused C++ contract probes: `11 / 11` PASS.
- xPilot 4 real named-pipe stress proof after timing-race correction: `15 / 15`
  consecutive PASS.
- xPilot 4 SDK companion probe: `21 / 21` PASS.
- Manager isolated probe: `24 / 24` PASS.
- Manager later-beta compatibility without a rebuild, missing-simulator-plugin
  refusal, and future-major refusal: PASS.
- Companion SDK API `0.1.99` acceptance and `0.2.0` refusal before event
  subscription: PASS.
- Product Owner amended-Manager wording, missing-bridge Repair, and
  return-to-Current confirmation: PASS.
- EZ Simulations executable icon embedded at Windows sizes from 16 through 256
  pixels and extracted from the final PE: PASS.
- Product Owner transaction scenario matrix before the compatibility amendment:
  PASS.
- V2.0.2 to V2.1.0 update using the frozen payload: PASS.
- Clean V2.1.0 first installation using the frozen payload: PASS.
- Restoration of all unmanaged diagnostics and legacy backups without changing
  managed files: PASS.
- Public V2.0.2 manifest against installed V2.1.0 remains silent/current: PASS.

The two Product Owner final installation scenarios used the same transaction
engine and the exact four managed file hashes listed above. The compatibility
amendment changed Manager eligibility and wording, then passed all 24 isolated
checks again. The required focused amended-Manager confirmation is complete.
The Product Owner subsequently approved distribution of the exact tested
Manager without an Authenticode signature. Package-level checksums and
SmartScreen instructions replace the former signed-build requirement.

The later xPilot beta amendment changed only Manager discovery, planning,
manifest metadata, compatibility wording, and focused probes. It did not change
the XPL, the live-tested bridge binary, the transaction engine, rollback, file
ownership, or runtime Brain behavior. The packaged amended Manager reports the
installed Beta 7 environment as `Current`, includes the bridge, and produces no
warning. The Product Owner opened the amended executable and confirmed the new
SDK-compatibility wording is present, completed the focused missing-bridge
Repair, and confirmed the Manager returned to `Current` at V2.1.0. Post-test
verification found all four managed hashes exact, a four-file V2.1.0 receipt,
and an empty staging area. The earlier full transaction scenario matrix remains
evidence for the unchanged transaction paths.

The one saved scenario excluded from the environment-independent total checks
the exact legacy xPilot 3.0.2 simulator-plugin fingerprint at its real X-Plane
path. This machine currently contains the approved xPilot 4 Beta 7 simulator
plugin there. The legacy xPilot 3 manager path and no-bridge behavior passed the
Product Owner scenario matrix; the exact fingerprint scenario remains an
environment-specific release check when the legacy binary occupies that path.

## Publication boundary

This file records a release candidate. It does not authorize publication. The
public `docs/xvatsim_update.json` remains at V2.0.2 so users are not notified
before the approved V2.1.0 download is available.

Before public distribution:

1. Package the exact Product Owner-tested unsigned Manager candidate.
2. Verify the Manager hash and embedded payload identity after extraction from
   the final ZIP.
3. Include plain SmartScreen instructions and SHA-256 checksums in the package.
4. Obtain Product Owner approval of the final downloadable package.
5. Publish the package and GitHub release before updating the public manifest.
