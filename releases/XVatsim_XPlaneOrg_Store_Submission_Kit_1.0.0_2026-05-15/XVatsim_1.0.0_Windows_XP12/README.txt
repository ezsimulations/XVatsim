XVatsim
Version: 1.0.0
Build date: 2026-05-15
Platform: Windows / X-Plane 12 / xPilot

What XVatsim is:
XVatsim is an xPilot companion plugin for X-Plane 12 that gives the pilot a cleaner,
route-aware VATSIM frequency display. Instead of showing every nearby controller and
forcing the pilot to sort through clutter, XVatsim focuses on the controllers that are
relevant to the current flight and stage of operation.

Current supported scope:
- Windows only
- X-Plane 12 only
- xPilot required
- IFR flight-plan workflow supported

Install:
1. Close X-Plane.
2. Open your X-Plane 12 root folder.
3. Copy the included Resources folder into the X-Plane 12 root folder.
4. Confirm the final plugin path is:
   X-Plane 12\Resources\plugins\XVatsim\win_x64\XVatsim.xpl
5. Start X-Plane 12.
6. Start xPilot and connect to VATSIM.

Expected behavior:
- XVatsim stays hidden until xPilot connects.
- The overlay wakes with a roll-down animation.
- Departure shows departure-airport local controllers and CTAF or UNICOM fallback.
- Enroute shows route-relevant Center controllers only.
- Arrival wakes ahead of destination and shows destination-relevant approach, tower,
  ground, ATIS, CTAF, and route Center status.
- If no route controller is online, the UI can sleep while continuing to monitor.
- Optional Standby Assist can preload COM1 standby with the recommended live controller frequency.
- TX, RX, COM1, COM2, and MODE C status are displayed in the overlay.
- Private messages, PDC/AUTO_ATC cards, SimBrief import, Navigraph AIRAC import, VFR workflow,
  Mac, Linux, and X-Plane 11 support are not part of this V1 release.

Useful menu items:
- XVatsim > Open Display
- XVatsim > Close Display
- XVatsim > Auto Display
- XVatsim > Standby Assist On / Off
- XVatsim > Set Cruise Target To Current Altitude
- XVatsim > Reset Cruise Target To Filed Altitude
- XVatsim > Reset XVatsim Session

If something looks wrong:
- Include the departure and arrival airports
- Include the callsign
- Include what xPilot showed
- Include what XVatsim showed
- Include screenshots if possible
- Include X-Plane Log.txt if the issue can be repeated

Package contents:
- Resources\plugins\XVatsim\win_x64\XVatsim.xpl
- Resources\plugins\XVatsim\win_x64\ui_transition.mp3
- README.txt
- CHANGELOG.txt
- QUICK_START.txt
