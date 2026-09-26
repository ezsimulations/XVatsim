# XVatsim xPilot 4 Bridge

This optional companion is a read-only fact bridge for xPilot 4. It forwards
raw private-message and controller observations to XVatsim over a local,
current-user-only named pipe. It does not connect to VATSIM, send messages,
control radios or transponders, select controllers, classify messages, assign
votes, or alter XVatsim's display.

XVatsim remains an independent project. This companion is not part of xPilot,
is not endorsed by xPilot, and does not replace xPilot.

## Build

From the XVatsim repository root:

```powershell
.\scripts\build_xpilot4_bridge.ps1
```

The build script obtains the exact official xPilot Plugin SDK commit shipped by
xPilot `4.0.0-beta.7`, builds the companion and offline probe, verifies that the
SDK assembly is not present in the companion output, verifies the companion
avoids APIs removed from beta 7's trimmed host, and runs the probe.

## Experimental installation

Installation is not part of the current implementation authorization. After a
separate approved live-test gate, the companion folder will be:

```text
Plugins/XVatsim.XPilot4Bridge/XVatsim.XPilot4Bridge.dll
```

xPilot must be closed while installing or removing the folder. Removing that
single folder completely removes the companion and does not affect xPilot 3 or
the XVatsim X-Plane plugin.
