# XVatsim Freeware User Guide

Version 2.0.0

Updated: September 2026

XVatsim is a Windows plugin for X-Plane 12 and xPilot. It gives VATSIM pilots a clean, route-aware frequency overlay that focuses on the controllers relevant to the current IFR flight.

XVatsim is intended for home flight simulation only. It is not approved for real-world aviation, navigation, flight planning, dispatch, or air traffic control use.

![Clean XVatsim overlay](assets/01_clean_ui.jpg)

## What's New In Version 2.0.0

Version 2.0.0 adds three information ORBs below the main frequency card:
METAR, VATSIM ATIS, and a one-shot PDC snapshot from xPilot. Each ORB opens its
own information drawer, keeps its own history, and marks newly received content
as read only after that content is visibly displayed.

Controller selection is also stronger. In the United States, XVatsim can use
vNAS sector information as another source of evidence when deciding which
Approach or Departure controller serves the flight. Terminal-controller
transmitter distance is evaluated from the departure or arrival airport rather
than from the moving aircraft. Center display continues to use the aircraft's
250-nautical-mile range gate. These facts support the same central decision
system; they do not create a second controller-selection path.

Version 2.0.0 also prevents identical ATIS content from repeatedly returning to
unread, improves exact route-polygon crossing detection, and keeps expensive
route and authority preparation away from the X-Plane flight loop so normal
settled operation remains calm.

## What XVatsim Does

XVatsim is not a replacement for xPilot. xPilot remains the VATSIM client. XVatsim watches the active VATSIM flight plan, aircraft state, radio state, reachable controller list, and route context, then displays the frequencies that are relevant to the current flight.

The goal is to reduce cockpit clutter. Instead of showing every controller that might be nearby, XVatsim focuses on the current phase of flight:

- Departure: departure airport local services, departure approach or departure control, current center, and CTAF or UNICOM fallback.
- Enroute: route-relevant center controllers.
- Arrival: destination-relevant center, approach, tower, ground, ATIS, CTAF, or UNICOM when the flight is close enough to arrival.

XVatsim can sleep when there is nothing useful to show and wake when controller or arrival context becomes relevant.

## Requirements

- Windows
- X-Plane 12
- xPilot
- VATSIM account and active xPilot connection
- IFR flight plan filed on VATSIM

XVatsim Version 2.0.0 does not include Mac support, Linux support, X-Plane 11 support, SimBrief import, Navigraph AIRAC import, a general private-message inbox, or a dedicated VFR controller-evidence workflow.

## Installation

1. Close X-Plane 12.
2. Download and extract the XVatsim zip from the X-Plane.org file page or the
   official GitHub Releases page at `github.com/ezsimulations/XVatsim/releases`.
3. Open the extracted folder.
4. Copy the included `Resources` folder into your X-Plane 12 root folder.
5. Confirm this file exists:

`X-Plane 12\Resources\plugins\XVatsim\win_x64\XVatsim.xpl`

6. Start X-Plane 12.
7. Start xPilot.
8. Connect to VATSIM.

If the plugin is installed correctly, the `XVatsim` menu appears under X-Plane's `Plugins` menu.

## First Flight Workflow

1. File an IFR flight plan on VATSIM.
2. Start X-Plane 12 and load the aircraft.
3. Start xPilot and connect to VATSIM using the same callsign as the flight plan.
4. Allow XVatsim a moment to load the flight plan and route context.
5. Fly normally. XVatsim will wake and sleep based on the current flight context.

During departure, XVatsim may show departure airport services or CTAF/UNICOM fallback. During cruise, it usually shows only route-relevant center controllers. Near destination, it wakes for arrival when the aircraft reaches the arrival preparation window.

## Understanding The Overlay

The overlay shows the flight-aware frequency board, COM radio state, transmit and receive state, Mode C state, and Standby Assist state.

Color and labels matter:

- Green row: current-polygon or current-flight authority.
- Orange row: next route polygon or arrival-prep authority.
- `Active`: the frequency is tuned in an active COM radio.
- `Standby`: Standby Assist loaded the frequency into COM1 standby.
- `ASST ON`: Standby Assist is enabled.
- `ASST OFF`: Standby Assist is disabled.
- `MODE C *Active*`: transponder Mode C is active.
- `TX` and `RX`: transmit and receive state for COM1 and COM2.
- Top-right version text: green means the installed version is current, gray
  means update status is not known yet, and amber rotating version/UPDATE text
  means a newer XVatsim package is available.

Tuning a frequency does not make a row green by itself. XVatsim colors controller rows from route and authority context, not from radio tuning alone.

In Version 2.0.0, the brain owns the final radio-board order, controller relevance decisions, information ORBs and drawers, update notice state, and Standby Assist target. COM1 active frequency is the only radio state that advances the next Standby Assist target; COM2 can be displayed, but it does not mark a controller row active or move the assist pointer.

When an update is available, XVatsim shows a dismissible update notice panel
with the installed version, latest version, and an X-Plane.org or GitHub
download reminder.
Manual update checks use the same notice panel for available, current, and
failed results, so update status is not clipped into the bottom route/status
line.

![Center frequency display](assets/05_center_frequency_display.jpg)

## METAR, ATIS, And PDC Information

The three round ORBs below the main card are independent information sources.
Select an ORB to open its drawer. Select the same ORB again to close it, or
select another ORB to switch drawers. Use the mouse wheel over an open drawer
to read longer entries.

### METAR

The METAR ORB automatically follows the primary airport chosen for the current
flight context. The drawer shows the latest available VATSIM METAR and keeps a
bounded recent history. To look up another airport without changing the
automatic primary airport, choose `Plugins > XVatsim > Select METAR Airport...`,
enter one four-letter ICAO code, and press Enter.

### VATSIM ATIS

The ATIS ORB follows the primary departure or arrival airport and displays the
current VATSIM voice-ATIS text when available. `NEW A`, for example, means a new
Information A is unread; `INFO A` means that exact revision has been displayed.
If VATSIM republishes identical Information A content, XVatsim keeps it read
instead of treating it as a new message.

For another airport, choose `Plugins > XVatsim > Select ATIS Airport...`, enter
one four-letter ICAO code, and press Enter. A manual lookup does not replace the
automatic primary airport.

### One-Shot PDC Snapshot

After a stable xPilot connection and complete flight identity are available,
XVatsim can capture the first valid waiting xPilot private message for the
flight. The PDC ORB changes from `NEW 1` to `MSG 1` after the captured message
is visibly displayed. The drawer always warns `CAPTURED SNAPSHOT — CHECK XPILOT
FOR REVISIONS`.

This is a convenience snapshot, not a private-message inbox or a replacement
for xPilot. XVatsim does not decide from the sender or wording whether the first
waiting message is a clearance. Always use xPilot as the authoritative source,
especially for revisions, amendments, or later messages.

## CTAF And UNICOM

When no controlled local airport frequency is relevant, XVatsim can display CTAF or UNICOM fallback. For airports where no CTAF is published, XVatsim can show a no-CTAF / UNICOM fallback state.

For example, in some regions a destination or departure may show `NO CTAF / UNICOM 122.800`.

![European UNICOM fallback](assets/03_europe_unicom.jpg)

## Plugin Menu

Open X-Plane's menu bar and choose `Plugins > XVatsim`.

### IFR Mode / VFR Mode

Selects the saved operating-mode preference. Version 2.0.0 retains the VFR mode
foundation, but controller projection remains designed and proven for the IFR
flight-plan workflow.

### Manual CTAF Lookup

Opens a text prompt for looking up CTAF information manually. Enter the airport ICAO and press Enter. The prompt accepts CTAF lookup text such as `.ctaf KJAC` or simply the airport ICAO when the prompt is already open.

### Select METAR Airport

Opens a text prompt for a one-airport METAR lookup. Enter one four-letter ICAO
code and press Enter. The result opens in the METAR drawer without changing the
automatic primary airport.

### Select ATIS Airport

Opens a text prompt for a one-airport VATSIM ATIS lookup. Enter one four-letter
ICAO code and press Enter. The result opens in the ATIS drawer without changing
the automatic primary airport.

### Open Display

Forces the overlay open. Use this when you want to see XVatsim even if Auto Display would currently keep it asleep.

### Close Display

Forces the overlay closed. Use this when you want the overlay hidden.

### Auto Display

Returns XVatsim to automatic wake and sleep behavior. This is the normal mode for flying.

### More Opacity / Less Opacity

Adjusts overlay opacity.

### Larger UI / Smaller UI

Adjusts overlay scale. XVatsim also remembers overlay size changes made by resizing the overlay window.

### Faster Animation / Slower Animation

Adjusts the roll-down and roll-up animation speed.

### Reset Appearance

Resets overlay opacity, scale, and animation speed to defaults.

### Set Cruise Target To Current Altitude

Sets XVatsim's cruise target to the aircraft's current altitude. This can help when your flown cruise altitude differs from the filed altitude.

### Reset Cruise Target To Filed Altitude

Returns the cruise target to the filed VATSIM flight-plan altitude.

### Reset XVatsim Session

Clears flight-scoped XVatsim state. Use this when starting a new flight or when you intentionally want XVatsim to forget the current session.

### Recover Current Flight

Recovers XVatsim workflow state for the currently active VATSIM flight plan. Use this after an xPilot disconnect/reconnect or if XVatsim needs to resume monitoring an active flight.

This is not the same as Reset XVatsim Session. Recover Current Flight is intended to keep working with the current flight plan.

### Check for Updates

Runs a manual notify-only update check against the public XVatsim update
manifest. If the installed version is current, XVatsim shows a short
current-version status. If a newer version is available, XVatsim tells you to
download it from the X-Plane.org file page or the official GitHub Releases
page.

XVatsim does not automatically download, install, replace, or launch a browser for updates.

### Set Diversion Airport

Opens a text prompt for setting a diversion airport. Enter a four-letter airport ICAO. XVatsim retargets arrival logic to the diversion airport when the entry is accepted.

### Revert To VATSIM Flight Plan

Clears the manual diversion override and returns XVatsim to the destination in the active VATSIM flight plan.

### Standby Assist On / Off

Enables or disables Standby Assist. When enabled, XVatsim can preload COM1
standby with the selected live controller frequency. If no controller target
wins, a resolved CTAF or `NO CTAF / UNICOM` advisory can also be selected when
the separate CTAF/UNICOM advisory gate and all brain-owned safety checks allow
it. A controller standby target always takes priority. The overlay shows
`ASST ON` or `ASST OFF`.

## Keyboard Commands In X-Plane

XVatsim registers several X-Plane commands. You can bind these to keyboard keys, joystick buttons, or hardware controls.

To assign a keyboard command:

1. Open X-Plane.
2. Go to `Settings`.
3. Open the `Keyboard` tab.
4. Search for `xvatsim`.
5. Select the command you want.
6. Assign a key or button.
7. Click `Apply`.

Bindable XVatsim commands:

| X-Plane command | Purpose |
| --- | --- |
| `xvatsim/manual_ctaf_lookup` | Open the manual CTAF lookup prompt. |
| `xvatsim/display_open` | Force the XVatsim display open. |
| `xvatsim/display_close` | Force the XVatsim display closed. |
| `xvatsim/display_auto` | Return the display to automatic behavior. |
| `xvatsim/cruise_target_current` | Set the cruise target to current aircraft altitude. |
| `xvatsim/cruise_target_filed` | Reset the cruise target to filed VATSIM altitude. |
| `xvatsim/reset_session` | Reset XVatsim state for the next flight. |
| `xvatsim/recover_current_flight` | Recover XVatsim workflow state for the current flight. |

Menu-only functions, such as appearance controls, diversion airport entry, and Standby Assist On/Off, are controlled from `Plugins > XVatsim`.

## Long-Haul And Reconnect Use

If xPilot disconnects during a long flight, reconnect to VATSIM with the same callsign and active flight plan. Then use:

`Plugins > XVatsim > Recover Current Flight`

XVatsim will attempt to reload the active VATSIM flight plan and resume monitoring the current flight. If the plan is missing, stale, or does not match the current callsign, recovery may fail closed rather than guessing.

## When To Use Reset Session

Use `Reset XVatsim Session` when:

- You are starting a new flight.
- You changed aircraft or callsign and want a clean XVatsim state.
- You intentionally want XVatsim to forget the current flight.

Do not use Reset Session just to recover after a reconnect. Use `Recover Current Flight` for that.

## Troubleshooting

### XVatsim menu does not appear

- Confirm the plugin file exists at `X-Plane 12\Resources\plugins\XVatsim\win_x64\XVatsim.xpl`.
- Confirm you are running X-Plane 12 on Windows.
- Check `X-Plane 12\Log.txt` for plugin load errors.

### Overlay does not appear

- Start xPilot and connect to VATSIM.
- Confirm the aircraft has valid electrical/radio state.
- Choose `Plugins > XVatsim > Open Display`.
- Choose `Plugins > XVatsim > Auto Display` to return to normal behavior.

### Flight plan does not load

- Confirm the VATSIM callsign in xPilot matches the filed flight plan.
- Give XVatsim time to refresh after connecting.
- Use `Plugins > XVatsim > Recover Current Flight`.

### Wrong or unexpected frequency display

- Compare what xPilot shows with what XVatsim shows.
- Remember that XVatsim intentionally hides irrelevant or unproven controllers.
- Current controllers display green; next or arrival-prep controllers display orange.
- If a relevant controller is missing or an irrelevant controller appears, collect logs and report it.

### Standby Assist did not tune a frequency

- Confirm Standby Assist is on.
- For controller targets, confirm a relevant live controller exists.
- For CTAF/UNICOM advisory targets, confirm the CTAF/UNICOM advisory gate is
  enabled and the lookup has completed successfully.
- Confirm the recommended frequency is not already active.
- A controller target takes priority over a CTAF/UNICOM advisory target.
- The PDC information drawer is separate from Standby Assist and never tunes a frequency.

### METAR, ATIS, or PDC information is missing

- Confirm xPilot is connected and the flight callsign matches the filed plan.
- METAR and ATIS availability depends on the current VATSIM sources.
- For a different airport, use `Select METAR Airport...` or `Select ATIS Airport...` from the XVatsim plugin menu.
- The PDC drawer captures only the first valid waiting xPilot message after the flight is armed. Check xPilot for the complete conversation and all revisions.
- If an ATIS republishes the same information code and unchanged content, XVatsim intentionally keeps the existing entry read.

### Update check is unavailable

- Confirm you have internet access.
- Confirm GitHub Pages can serve `https://ezsimulations.github.io/XVatsim/xvatsim_update.json`.
- If the public update file is temporarily unreachable, XVatsim continues to work normally; only the update check is unavailable.

## Bug Reports

For useful bug reports, include:

- Departure airport.
- Arrival airport.
- Callsign.
- Filed route if available.
- What xPilot showed.
- What XVatsim showed.
- Screenshots if possible.
- Whether Standby Assist was on or off.
- Whether you had disconnected/reconnected xPilot.
- The files listed below.

Fresh diagnostic logs are generated locally when the plugin runs. These logs are not shipped inside the package, but they are useful for troubleshooting.

Log files:

`X-Plane 12\Resources\plugins\XVatsim\logs\xvatsim_diagnostics.log`

`X-Plane 12\Log.txt`

Support contact:

`ezsimulations@gmail.com`

## Freeware Notes

XVatsim is being provided as freeware. Please keep the package intact when sharing it so pilots receive the plugin, transition audio, authority registry, README, quick start, and this user guide together. XVatsim 2.0.0 is focused on Windows, X-Plane 12, xPilot, and IFR flight-plan operations.
