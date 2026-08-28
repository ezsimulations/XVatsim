# XVatsim V2 Step 4 Accessory Liveness Correction — Windows Release Proof

Date: 2026-08-28
Result: PASS

## Toolchain and fresh configurations

- Generator: Visual Studio 18 2026.
- MSVC: 19.51.36252.0 / toolset 14.51.36231.
- Windows SDK: 10.0.26100.0.
- Architecture/configuration: x64 Release.
- Compatibility definition:
  `_SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS`.

The first new cache omitted the already accepted compatibility definition and
MSVC 19.51 rejected the deprecated SDK coroutine header before the proof target
could build. The same fresh cache was reconfigured with the accepted definition;
no source or contract setting changed, and the complete builds below then
passed.

Proof configuration:

- Directory: `build/v2-step4-accessory-liveness-release-proof`.
- Plugin OFF; regression harness ON; Step 4 visual proof ON; fixtures OFF.
- Cache SHA-256:
  `1E932EC10068802640F0D7D8ED31BE404DA77884ADFBBD21FF6DA82A579E7CA6`.
- Harness SHA-256:
  `ABE8FE3134A0ED9C1AD1567E338A399EF8D734C77A826F47B4DBBA3AF40E2D66`.
- Visual proof executable SHA-256:
  `750C075183EDE737ECFB322C7A3302CC6AD22B6318E4F2FE7D8983659067DD76`.

Normal configuration:

- Directory: `build/v2-step4-accessory-liveness-release-normal`.
- Plugin ON; regression harness/visual proof OFF; fixtures OFF.
- Cache SHA-256:
  `846F3B90E8BABB7C59F41AD6D509EA971F6EED3A3C6FF95CFE9288737605CD80`.
- Plugin size: 2,407,936 bytes.
- Plugin SHA-256:
  `88BD236C879547F1C6BE386AB32BA8BDDEA50ACCF12A6960248B0A35DEB70080`.
- Registry SHA-256:
  `3676CA43E5AFB8A5E443FDE6D04616918029E004E17EFAA022695D91E23DB60B`.
- Audio SHA-256:
  `C7BBE97DAD356C68FDFE40F9E8C1CF4EADEBCCD125463214F963F66356F8D9F1`.

## Proof results

- Corrective 29/29; Step 3 31/31; Step 4 96/96; complete 590/590.
- Scenario fingerprint:
  `765008264996DBE603FFB6B30D9CD0000B3CA59FA37BBA8C1D62D16CFD47E498`.
- Stress maximum in-flight duration: 1 microsecond; limit 500,000
  microseconds; final queue 0; final in-flight false.
- Warm proof: 100,000 unchanged cycles with all recurring content,
  presentation, input, and diagnostic counters zero.
- Real WinHTTP loopback success: 16 ms final, 20 ms maximum observed.
- Cooperative cancellation maximum: 15 ms; limit 500 ms.
- Visuals: 22; repeat PNG differences 0.
- Visual manifest SHA-256:
  `6F11239FDC091BF1522B9BA7B2BB7C3E9C5ACB1B54D3D329CF760C04B4E9FF48`.
- Visual performance CSV SHA-256:
  `96389D5065B92804047937E6A7EDD054C50697E933CFF437DAD1D3DE0725F883`.

## Normal-binary isolation

ASCII and UTF-16 inspection passed.

- Required present: `metar.vatsim.net`, `VATSIM_METAR`.
- Required absent: NOAA, Aviation Weather, `aviationweather.gov`,
  `127.0.0.1`, `ProofEndpoint`, injected transport, corrective scenario names,
  synthetic KDFW fixture report, and a second weather source.

## Integrity and external boundary

- Original protected evidence: 116/116 paths match size and SHA-256.
- Failed-live evidence: 34/34 paths match size and SHA-256.
- Corrected-live-reproof evidence: 45/45 manifest entries match;
  `SHA256SUMS.txt` remains
  `E3355DED8FFFD76BB904D12ABF7BA8013BD1901E2349181D9C8C34861D1D58BA`.
- Existing staging: 38/38 files match; total staging remains 50 files and
  contains zero `.xpl` files.
- X-Plane/xPilot processes: 0/0; active/staged `.xpl`: 0/0; active
  `win_x64`: absent.
- Preferences and X-Plane log match their restored locked hashes.
- V1.2.3 was untouched.

No deployment, live request, or evidence commit occurred during proof.
