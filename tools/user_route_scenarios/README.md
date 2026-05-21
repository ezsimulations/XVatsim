# XVatsim User Route Scenario Builder

This tool converts a SimBrief/X-Plane `.fms` route plus a simple controller
situation file into a regression harness `.scn` file.

Use it when you want to test a flight plan offline without waiting for live
VATSIM controller timing.

## Live Battle-Test Probe

Use this when you want to quickly battle-test a SimBrief/X-Plane `.fms` route
against the controllers that are online right now.

Double-click:

```text
tools\user_route_scenarios\Run_Live_FMS_Battle_Test.bat
```

Then paste the full `.fms` path and enter the callsign.

The live probe downloads current VATSIM network data, VATSIM transceivers,
VATSpy FIR/boundary data, and SimAware TRACON boundaries. It creates a
temporary scenario under:

```text
tools\user_route_scenarios\generated_live
```

It then runs the regression harness and prints the important board/resolver
summary lines. Normal live probes do not overwrite the permanent regression
scenario folder.

Command-line example:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\user_route_scenarios\Invoke-LiveFmsBattleTest.ps1 -FmsPath "C:\X-Plane 12\Output\FMS plans\LEPAEDDB01.fms" -Callsign DAL100
```

Only use `-SaveAsHarnessScenario` when a live failure should become a permanent
saved regression case. If the target permanent scenario already exists, the
script refuses to overwrite it unless `-ForceOverwrite` is also supplied.

## Owner Workflow

1. Generate or download the SimBrief `.fms` file into X-Plane, usually:

   `C:\X-Plane 12\Output\FMS plans`

2. Double-click:

   `tools\user_route_scenarios\Create_XVatsim_Scenario_From_FMS.bat`

3. Paste the full `.fms` path when prompted.

4. The first run creates a situation template under:

   `tools\user_route_scenarios\situations`

5. Fill in the route sectors and online controllers you want to simulate.

6. Run the batch file again with the same `.fms` path.

7. The tool creates a `.scn` under:

   `tools\regression_harness\scenarios`

8. It immediately runs the scenario and prints PASS or FAIL.

## Situation File Format

Use one row for the current route sector:

```text
current_sector=ZAU|ZAU|0
```

Use one row for each upcoming sector:

```text
next_sector=ZID|ZID|120
next_sector=ZME|MEM|310
```

Use one row for each online controller:

```text
online=ZID_CTR|134.325|6
online=MEM_CTR|132.400|6
```

Facility values:

- `6` means Center.
- `1` means FSS/Oceanic.
- `5` means Approach.

For ENROUTE tests, use `6` or `1`.

If a route sector has no matching online controller, the generated scenario
expects the UI to show an offline sector row using that sector ID. If an online
controller matches the sector prefix, the generated scenario expects the
controller callsign instead.

## Example

For a KORD to KSDF route where Chicago Center is current but offline and
Indianapolis Center is next and online:

```text
name=KORD KSDF example
callsign=TEST123
current_sector=ZAU|ZAU|0
next_sector=ZID|ZID|120
online=ZID_CTR|134.325|6
expect_callsigns=AUTO
```

The generated expectation is:

```text
expect.enroute_available=true
expect.enroute_callsigns=ZAU,ZID_CTR
```

## Learning From Failures

If a generated scenario fails, keep the `.fms`, the situation file, and the
generated `.scn`. That failure becomes a permanent saved case, so once we fix
the logic, the same bug cannot silently return later.
