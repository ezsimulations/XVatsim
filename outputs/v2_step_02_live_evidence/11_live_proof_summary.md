# XVatsim V2 Step 2 Live Proof Summary

Witnessed by Darron on 2026-08-26 using the controlled active plugin layout at
`C:\X-Plane 12\Resources\plugins\XVatsim`.

## Result

- Initial startup with no operating-mode preference selected IFR and checked only `IFR Mode`.
- Explicit VFR selection checked only `VFR Mode`, persisted successfully, and did not visibly change the XVatsim card/controller output.
- `Reset XVatsim Session` preserved VFR.
- xPilot disconnect/reconnect preserved VFR.
- X-Plane/plugin restart loaded VFR from the settings store.
- Explicit IFR selection checked only `IFR Mode`, persisted successfully, and did not visibly change the XVatsim card/controller output.
- X-Plane/plugin restart loaded IFR from the settings store.
- Operating-mode diagnostics were bounded to initialization and explicit menu requests; reset, xPilot reconnect, and frame activity emitted no operating-mode mutation or persistence retry.

## Operating-Mode Diagnostic Sequence

```text
tick=1223711 event=operating-mode-initialized effective=ifr stateSource=default stateReason=missing-setting generation=0
tick=1224283 event=operating-mode-selection requested=ifr previous=ifr effective=ifr changed=false stateSource=default stateReason=missing-setting requestSource=pilot-menu requestReason=already-active generation=0 persistence=not-requested
tick=1224315 event=operating-mode-selection requested=vfr previous=ifr effective=vfr changed=true stateSource=pilot-menu stateReason=explicit-selection requestSource=pilot-menu requestReason=explicit-selection generation=1 persistence=success
tick=1224486 event=operating-mode-selection requested=vfr previous=vfr effective=vfr changed=false stateSource=pilot-menu stateReason=explicit-selection requestSource=pilot-menu requestReason=already-active generation=1 persistence=not-requested
tick=1224800 event=operating-mode-initialized effective=vfr stateSource=settings-store stateReason=stored-preference generation=0
tick=1225037 event=operating-mode-selection requested=ifr previous=vfr effective=ifr changed=true stateSource=pilot-menu stateReason=explicit-selection requestSource=pilot-menu requestReason=explicit-selection generation=1 persistence=success
tick=1225148 event=operating-mode-initialized effective=ifr stateSource=settings-store stateReason=stored-preference generation=0
```

The xPilot disconnect boundary was recorded at tick `1224534`. There is no operating-mode event at that boundary or during the preceding Reset Session proof. The next operating-mode event is the later VFR initialization at tick `1224800` after the plugin/X-Plane restart.

## Evidence Files

| File | SHA-256 | Evidence |
| --- | --- | --- |
| `01_ifr_default_no_preference.png` | `663C879DCB85D4B7A8DCEE4999B5F82B8CF271D61729CF5103E2C7F604ACB69B` | Initial IFR checkmark with no valid preference |
| `02_ifr_card_before_mode_change.png` | `663C879DCB85D4B7A8DCEE4999B5F82B8CF271D61729CF5103E2C7F604ACB69B` | IFR card/controller baseline |
| `03_vfr_selected_card_unchanged.png` | `D22970EC7C8DEB28B8BD2D3192379EAB6ABCEE4C645D549436C545AC0AAEC028` | VFR selected; card visibly unchanged |
| `04_vfr_after_reset_session.png` | `B7A2189E39E667A37BA32FB8F4758726727F06BF5AD9B4E5E887636E162E499D` | VFR preserved by Reset Session |
| `05_vfr_after_xpilot_reconnect.png` | `D0F14435B1A47849F284A1AD38F9586859E11E5A63DB0425CF4A28D23F483A39` | VFR preserved by xPilot reconnect |
| `06_vfr_after_restart.png` | `71B6BBA5AE3D0B1099F8EF44CB07AB8743B2305D566AFB335821EB75E97B6288` | VFR reloaded after restart |
| `07_ifr_selected_before_restart.png` | `8848E376C5ED5198FB8128636F5DD969195FB79729D0FBDCBA4A431AAC7815BC` | IFR selected and persisted |
| `08_ifr_after_restart.png` | `2B2FAE2EDDAFA44C975D23C180684A120BA9C246446032245E48ADF320C5FF38` | IFR reloaded after restart |
| `09_xvatsim_live_diagnostics_full.txt` | `65B79F796CEF5293965198753431AFB64335E5BBA3DD81E1101C51798814058C` | Full XVatsim live diagnostic capture |
| `10_xplane_live_log.txt` | `246A875FE651A5EEC9B8B228C3032720D9EB07B61B89D332CB774E8F9EA634CC` | XVatsim-only X-Plane log excerpt; records the full source-log hash |

## Controlled-Install Rollback

After X-Plane and xPilot were stopped, the active plugin slot and mutable external files were restored to their exact pre-Step-2 state:

- Active XVatsim plugin-root children: `logs`, `V2 Test` only.
- Active XVatsim `.xpl` count: `0`.
- Active `win_x64` directory: absent.
- `V2 Test` staging folder: preserved.
- `.xpl` files anywhere under `V2 Test`: `0`.
- Original `XVatsim.prf` restored: `E866EBAC005DAF39B7359A547A795D39D9AE6ED60C018B4637DA8753F67E8504`.
- Original X-Plane `Log.txt` restored: `D4E54021690B8CEE2CCA18838C16A449705B83F38F89CCE7AB51B78193FC7E71`.
- Original August 23 diagnostic restored: `0C42FBA5FB4DE0D8BE2E4120E8C4EBC5FBF6B20A017B586677BCDDC209EAAFB4`.
- Original August 24 diagnostic restored: `B636260B67BD48AD697DDC600B561BC1E626EE25D1EDC7C12E88A87295B13656`.
- Original August 25 diagnostic restored: `1D3F83427ECD5DA2AD608AD5B22E14F42F87B92429D7B40F906C0C370FBDF087`.
- Original August 26 diagnostic restored: `BD9C7B29EE6BBF058EBE600846C0B0DAA07EB86D95843E91F94BDEFFB57BB628`.

The staging/backup folder remains at
`C:\X-Plane 12\Resources\plugins\XVatsim\V2 Test\step_02_pretest_backup`.
