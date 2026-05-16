# XVatsim User Route Scenario Builder

This tool converts a SimBrief/X-Plane `.fms` route plus a simple controller
situation file into a regression harness `.scn` file.

Use it when you want to test a flight plan offline without waiting for live
VATSIM controller timing.

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
