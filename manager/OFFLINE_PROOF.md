# XVatsim Manager Offline Proof

**Date:** 2026-09-26

**Baseline:** `627fbb96d9234bb05ba1ce10c6acc2e6827e02b6`

**Development artifact:** `manager/artifacts/XVatsimManager.exe`

**Candidate version:** `2.1.0`

**Artifact SHA-256:** `F197731DEDFC8BDBDFAD1412FDCC7EF1C5CE73B5B8897EC674F26890EDBCA067`

**Embedded payload SHA-256:** `3AEC1AFE3D9674E36399A35BCEEA15A6DAE1950235FBB5001E5C878D32C356D3`

The isolated manager probe passed 24 checks:

- clean first-install planning and execution;
- preservation of an unmanaged settings file;
- verified-current detection;
- modified-file repair;
- version update;
- automatic rollback after an injected mid-install disk failure;
- rollback after a real Windows destination-file lock, including preservation
  of the prior receipt, removal of staging and temporary files, and retention
  of backup evidence;
- no-xPilot blocking, verified xPilot 3, supported xPilot 4, unsupported
  xPilot 4, and dual-version planning;
- transaction-level refusal when the required folder and pairing gate is not
  satisfied;
- refusal to install the bridge for a mismatched xPilot simulator plugin;
- ZIP path-traversal rejection;
- corrupt-payload rejection before destination writes; and
- running X-Plane/xPilot refusal.

The packaged executable also passed its embedded-payload startup self-test.
The manager now includes a boxed installation sequence that remains visible
for at least five seconds after a fast successful transaction. The transaction
engine remains authoritative and immediately interrupts the visual sequence
when an actual failure occurs. The Product Owner confirmed the completed
progress presentation during live manager scenario testing.
Read-only discovery on the Product Owner's machine found:

- `C:\X-Plane 12` as a valid X-Plane 12 installation;
- xPilot `4.0.0-beta.7` as a supported desktop client;
- the approved Beta 7 X-Plane plugin hash;
- all four managed XVatsim and bridge files matching the embedded payload; and
- a final recommendation of `Current`, with bridge compatibility true and no warnings.

Windows UI Automation and a rendered-window inspection also proved that the
startup screen displays only the verified X-Plane and xPilot folders, reports
`XVatsim is current`, disables the `Current` action, contains no drive scan or
rescan control, hides older xPilot beta records, and keeps diagnostic export
inside Advanced details. Hashes, timestamps, the receipt, backup count, and the
empty staging area remained unchanged after opening the manager.

No installed simulator or xPilot file was changed during this current-state
proof. The development executable is intentionally unsigned and is not
approved for public distribution.

## Product Owner Scenario Tests

| Scenario | Result | Evidence |
|---|---|---|
| Everything already current | **PASS — 2026-09-26** | Product Owner confirmed the manager reported that everything was current and no update was needed. Post-test comparison confirmed all four managed file hashes, lengths, and timestamps were unchanged. The receipt and backup count were unchanged, and staging remained empty. |
| Legacy xPilot 3 with no bridge | **PASS — 2026-09-26** | Product Owner confirmed the manager reported xPilot 3.0.2 as supported through the legacy integration and XVatsim as current. The absent xPilot 4 executable and bridge remained absent. A stale Beta 7 uninstall record was ignored. All three XVatsim managed files, the receipt, and backup count remained unchanged, and staging remained empty. |
| No xPilot installed | **PASS — 2026-09-26** | Product Owner confirmed the manager found X-Plane, reported that xPilot was missing, and kept Install disabled. Selecting a random unrelated folder was rejected and left the xPilot field empty. Both stale xPilot uninstall records were ignored. Managed files, receipt, backup count, and staging remained unchanged, and no bridge was created. |
| Supported xPilot 4 with missing bridge | **PASS — 2026-09-26** | Product Owner confirmed the manager offered Repair, displayed the staged progress panel, installed the missing bridge, and returned to Current. The bridge location and SHA-256 matched the payload. Existing XVatsim file hashes and timestamps were unchanged, the new receipt contained all four managed files, and staging was empty. The default window height was increased to match the size needed to display the progress panel without scrolling. |
| Manually selected xPilot 4 at an unusual path | **PASS — 2026-09-26** | Product Owner selected `C:\XVatsim Manager Test\xPilot Beta 7`, confirmed the full progress display, and completed Repair. The bridge was installed beneath that selected xPilot root with the exact payload hash. The normal-location bridge and all three XVatsim files retained their hashes and timestamps. The receipt records the unusual xPilot executable and bridge destination, the backup count was unchanged, and staging remained empty. |
| xPilot running blocks Repair | **PASS — 2026-09-26** | With xPilot Beta 7 running and the bridge intentionally held outside the plugin folder, Product Owner pressed Repair and received `Close xPilot, then choose Repair again.` No progress transaction began. XVatsim files, the held bridge, receipt, backup count, and staging remained unchanged, and no bridge was written into the active xPilot plugin folder. |
| Unmanaged user data survives Repair | **PASS — 2026-09-26** | A managed registry was safely altered with valid trailing whitespace while 69 unmanaged files were inventoried. Product Owner completed Repair. The registry returned to the approved payload hash, the altered version was preserved in a new transaction backup, and the XPL, audio, and bridge retained their hashes and timestamps. All 69 unmanaged files remained byte-for-byte unchanged, the receipt recorded the standard xPilot path and all four managed files, staging remained empty, and the UI returned to Current. |
| V2.0.2 to V2.1.0 upgrade | **PASS — 2026-09-26** | Product Owner completed Update using the frozen V2.1.0 candidate. The installed XPL, audio, registry, and bridge matched the embedded payload. The receipt changed to V2.1.0 with all four files, and the transaction backup retained the exact prior XPL and bridge hashes. The installed XPL contains the V2.1.0 UI label. |
| Clean V2.1.0 first installation | **PASS — 2026-09-26** | The active XVatsim folder, bridge, and receipt were moved into a verified recovery location to simulate a new user. Product Owner completed Install. The new installation contained exactly the three managed X-Plane files plus the bridge, all hashes matched, and the new receipt recorded V2.1.0. Sixty-eight unmanaged diagnostics and legacy backup files were then restored without overwriting any managed file; all 71 original plugin files are present and the recovery copy remains available. |

## Frozen V2.1.0 Payload

- `XVatsim.xpl`: `0D4ACC2A99309EE3ACC640798C286144044E57595E93AD8F7364485A65CBAEEF`
- `ui_transition.mp3`: `C7BBE97DAD356C68FDFE40F9E8C1CF4EADEBCCD125463214F963F66356F8D9F1`
- `authority_source_registry.json`: `3676CA43E5AFB8A5E443FDE6D04616918029E004E17EFAA022695D91E23DB60B`
- `XVatsim.XPilot4Bridge.dll`: `33DF8B96B62356C01E3DAD09E38198D3E7118F688421E83D6C797BAF6EA7CFA0`

The public update manifest intentionally remains at V2.0.2 during release
preparation. The candidate Manager is unsigned and remains blocked from public
distribution until Authenticode signing and a clean standard-account Windows
verification are complete.
