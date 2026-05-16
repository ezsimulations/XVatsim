[CmdletBinding()]
param(
    [string]$FmsPath = "",
    [string]$SituationPath = "",
    [string]$OutputPath = "",
    [switch]$OpenTemplate,
    [switch]$NoRun
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Resolve-WorkspaceRoot {
    return (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..\..")).Path
}

function Normalize-Token {
    param([string]$Value)
    return (($Value -replace "[^A-Za-z0-9_]", "").ToUpperInvariant())
}

function New-Slug {
    param([string]$Value)
    $slug = (($Value -replace "[^A-Za-z0-9]+", "_").Trim("_")).ToLowerInvariant()
    if ([string]::IsNullOrWhiteSpace($slug)) {
        return "user_route"
    }
    return $slug
}

function Get-FileBaseName {
    param([string]$Path)
    return [System.IO.Path]::GetFileNameWithoutExtension($Path)
}

function Convert-ToScenarioBool {
    param([bool]$Value)
    if ($Value) {
        return "true"
    }
    return "false"
}

function Split-List {
    param([string]$Value)
    if ([string]::IsNullOrWhiteSpace($Value)) {
        return @()
    }
    return @($Value -split "[,;> ]+" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | ForEach-Object { Normalize-Token $_ })
}

function Get-ControllerPrefix {
    param([string]$Callsign)
    $normalized = Normalize-Token $Callsign
    $separator = $normalized.LastIndexOf("_")
    if ($separator -le 0) {
        return $normalized
    }
    return $normalized.Substring(0, $separator)
}

function Get-SectorRegionKey {
    param([string]$Identifier)
    $normalized = Normalize-Token $Identifier
    $separator = $normalized.IndexOf("-")
    if ($separator -gt 0) {
        return $normalized.Substring(0, $separator)
    }
    return $normalized
}

function Parse-FmsPlan {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "FMS file not found: $Path"
    }

    $plan = [ordered]@{
        Path = (Resolve-Path -LiteralPath $Path).Path
        Cycle = ""
        Departure = ""
        Destination = ""
        DepartureRunway = ""
        DestinationRunway = ""
        Waypoints = @()
    }

    foreach ($rawLine in Get-Content -LiteralPath $Path) {
        $line = $rawLine.Trim()
        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }

        $parts = @($line -split "\s+")
        if ($parts.Count -ge 2) {
            switch ($parts[0].ToUpperInvariant()) {
                "CYCLE" { $plan.Cycle = $parts[1]; continue }
                "ADEP" { $plan.Departure = Normalize-Token $parts[1]; continue }
                "ADES" { $plan.Destination = Normalize-Token $parts[1]; continue }
                "DEPRWY" { $plan.DepartureRunway = $parts[1].ToUpperInvariant(); continue }
                "DESRWY" { $plan.DestinationRunway = $parts[1].ToUpperInvariant(); continue }
            }
        }

        if ($parts.Count -ge 6 -and $parts[0] -match "^\d+$") {
            $altitude = 0.0
            $latitude = 0.0
            $longitude = 0.0
            if (-not [double]::TryParse($parts[3], [ref]$altitude)) {
                continue
            }
            if (-not [double]::TryParse($parts[4], [ref]$latitude)) {
                continue
            }
            if (-not [double]::TryParse($parts[5], [ref]$longitude)) {
                continue
            }

            $plan.Waypoints += [pscustomobject]@{
                Type = [int]$parts[0]
                Ident = Normalize-Token $parts[1]
                Via = $parts[2].ToUpperInvariant()
                AltitudeFt = $altitude
                LatitudeDeg = $latitude
                LongitudeDeg = $longitude
            }
        }
    }

    $waypoints = @($plan.Waypoints)
    if ([string]::IsNullOrWhiteSpace($plan.Departure) -and $waypoints.Count -gt 0) {
        $plan.Departure = $waypoints[0].Ident
    }
    if ([string]::IsNullOrWhiteSpace($plan.Destination) -and $waypoints.Count -gt 0) {
        $plan.Destination = $waypoints[$waypoints.Count - 1].Ident
    }
    if ($waypoints.Count -lt 2) {
        throw "FMS file does not contain enough route waypoints."
    }

    return $plan
}

function New-SituationTemplate {
    param(
        [string]$Path,
        [object]$Plan
    )

    $directory = Split-Path -Parent $Path
    New-Item -ItemType Directory -Path $directory -Force | Out-Null

    $routeText = (($Plan.Waypoints |
        Where-Object { $_.Ident -ne $Plan.Departure -and $_.Ident -ne $Plan.Destination } |
        ForEach-Object { $_.Ident }) -join " ")
    if ([string]::IsNullOrWhiteSpace($routeText)) {
        $routeText = "<direct>"
    }

    $template = @"
# XVatsim offline test situation
# FMS route: $($Plan.Departure) -> $($Plan.Destination)
# FMS enroute points: $routeText
#
# Fill this in, save it, then run the scenario creator again.
#
# Sector rows mean "this sector is on the aircraft route."
# Format:
#   current_sector=SECTOR_ID|CONTROLLER_PREFIXES|ENTRY_NM
#   next_sector=SECTOR_ID|CONTROLLER_PREFIXES|ENTRY_NM
#
# Online controller rows mean "this controller is online in the simulated VATSIM feed."
# Format:
#   online=CALLSIGN|FREQUENCY|FACILITY
#
# Facility values:
#   6 = Center
#   1 = FSS/Oceanic
#   5 = Approach
# For ENROUTE tests, use 6 or 1.
#
# If a route sector has no matching online controller, the harness expects an
# offline row using the sector ID. If you want to override the expected UI list,
# set expect_callsigns manually.

name=$($Plan.Departure) $($Plan.Destination) SimBrief controller situation
callsign=TEST123

# Example only. Replace these with the sectors and prefixes you want to test.
current_sector=EDIT_CURRENT_SECTOR|EDIT_PREFIX|0
# next_sector=EDIT_NEXT_SECTOR|EDIT_PREFIX|150

# Example only. Add one row for each online controller you want to simulate.
# online=EDIT_CTR|123.450|6

# Optional. Leave AUTO unless you want to force the expected UI callsign order.
expect_callsigns=AUTO
"@

    Set-Content -LiteralPath $Path -Value $template -Encoding ASCII
}

function Parse-Situation {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Situation file not found: $Path"
    }

    $situation = [ordered]@{
        Name = ""
        Callsign = "TEST123"
        CurrentSectors = @()
        NextSectors = @()
        OnlineControllers = @()
        ExpectedCallsigns = "AUTO"
        Com1Active = "122.800"
    }

    $lineNumber = 0
    foreach ($rawLine in Get-Content -LiteralPath $Path) {
        $lineNumber++
        $line = ($rawLine -replace "#.*$", "").Trim()
        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }
        $equalsIndex = $line.IndexOf("=")
        if ($equalsIndex -lt 1) {
            throw "Invalid situation line $lineNumber. Expected key=value."
        }

        $key = $line.Substring(0, $equalsIndex).Trim().ToLowerInvariant()
        $value = $line.Substring($equalsIndex + 1).Trim()
        switch ($key) {
            "name" { $situation.Name = $value; continue }
            "callsign" { $situation.Callsign = Normalize-Token $value; continue }
            "com1_active" { $situation.Com1Active = $value; continue }
            "expect_callsigns" { $situation.ExpectedCallsigns = $value; continue }
            "current_sector" {
                $situation.CurrentSectors += Parse-SectorRow $value $true $lineNumber
                continue
            }
            "next_sector" {
                $situation.NextSectors += Parse-SectorRow $value $false $lineNumber
                continue
            }
            "online" {
                $situation.OnlineControllers += Parse-ControllerRow $value $lineNumber
                continue
            }
            default {
                throw "Unknown situation key '$key' at line $lineNumber."
            }
        }
    }

    foreach ($sector in @(@($situation.CurrentSectors) + @($situation.NextSectors))) {
        if ($sector.Identifier -like "EDIT_*" -or @($sector.Prefixes | Where-Object { $_ -like "EDIT_*" }).Count -gt 0) {
            throw "Situation file still contains EDIT_* placeholder sector data. Edit $Path first."
        }
    }
    foreach ($controller in $situation.OnlineControllers) {
        if ($controller.Callsign -like "EDIT_*") {
            throw "Situation file still contains EDIT_* placeholder controller data. Edit $Path first."
        }
    }

    if (@($situation.CurrentSectors).Count -eq 0 -and @($situation.NextSectors).Count -eq 0) {
        throw "Situation must include at least one current_sector or next_sector row."
    }

    return $situation
}

function Parse-SectorRow {
    param(
        [string]$Value,
        [bool]$Current,
        [int]$LineNumber
    )

    $parts = @($Value -split "\|")
    if ($parts.Count -lt 2) {
        throw "Invalid sector row at line $LineNumber. Use SECTOR_ID|PREFIXES|ENTRY_NM."
    }

    $entryDistance = 0.0
    if ($parts.Count -ge 3 -and -not [string]::IsNullOrWhiteSpace($parts[2])) {
        if (-not [double]::TryParse($parts[2].Trim(), [ref]$entryDistance)) {
            throw "Invalid sector entry distance at line $LineNumber."
        }
    }

    if ($Current) {
        $entryDistance = 0.0
    }

    return [pscustomobject]@{
        Identifier = Normalize-Token $parts[0]
        Prefixes = @(Split-List $parts[1])
        EntryDistanceNm = $entryDistance
        Current = $Current
    }
}

function Parse-ControllerRow {
    param(
        [string]$Value,
        [int]$LineNumber
    )

    $parts = @($Value -split "\|")
    if ($parts.Count -lt 2) {
        throw "Invalid online controller row at line $LineNumber. Use CALLSIGN|FREQUENCY|FACILITY."
    }

    $facility = 6
    if ($parts.Count -ge 3 -and -not [string]::IsNullOrWhiteSpace($parts[2])) {
        if (-not [int]::TryParse($parts[2].Trim(), [ref]$facility)) {
            throw "Invalid controller facility at line $LineNumber."
        }
    }

    return [pscustomobject]@{
        Callsign = Normalize-Token $parts[0]
        Frequency = $parts[1].Trim()
        Facility = $facility
    }
}

function Find-MatchingSector {
    param(
        [object]$Controller,
        [array]$Sectors
    )

    $prefix = Get-ControllerPrefix $Controller.Callsign
    foreach ($sector in $Sectors) {
        foreach ($sectorPrefix in $sector.Prefixes) {
            if ($sectorPrefix -eq $prefix) {
                return $sector
            }
        }
    }
    return $null
}

function Get-ExpectedStations {
    param(
        [array]$CurrentSectors,
        [array]$NextSectors,
        [array]$OnlineControllers
    )

    $stations = @()
    $coveredRegionKeys = @{}
    $hasLive = $false

    foreach ($controller in $OnlineControllers) {
        if ($controller.Facility -ne 6 -and $controller.Facility -ne 1) {
            continue
        }

        $sector = Find-MatchingSector $controller $CurrentSectors
        if ($null -eq $sector) {
            $sector = Find-MatchingSector $controller $NextSectors
        }
        if ($null -eq $sector) {
            continue
        }

        $hasLive = $true
        $regionKey = Get-SectorRegionKey $sector.Identifier
        $coveredRegionKeys[$regionKey] = $true
        $stations += [pscustomobject]@{
            Callsign = $controller.Callsign
            Frequency = $controller.Frequency
            Distance = if ($sector.Current) { 0.0 } else { [double]$sector.EntryDistanceNm }
            SectorActive = [bool]$sector.Current
            Offline = $false
        }
    }

    foreach ($sector in @($CurrentSectors + $NextSectors)) {
        if (@($sector.Prefixes).Count -eq 0) {
            continue
        }
        $regionKey = Get-SectorRegionKey $sector.Identifier
        if ($coveredRegionKeys.ContainsKey($regionKey)) {
            continue
        }
        $stations += [pscustomobject]@{
            Callsign = $regionKey
            Frequency = ""
            Distance = if ($sector.Current) { 0.0 } else { [double]$sector.EntryDistanceNm }
            SectorActive = $false
            Offline = $true
        }
    }

    $ordered = @($stations | Sort-Object `
        @{ Expression = "Distance"; Ascending = $true }, `
        @{ Expression = "SectorActive"; Descending = $true }, `
        @{ Expression = "Frequency"; Ascending = $true }, `
        @{ Expression = "Callsign"; Ascending = $true })

    return [pscustomobject]@{
        HasLive = $hasLive
        Callsigns = @($ordered | ForEach-Object { $_.Callsign })
    }
}

function Build-ScenarioText {
    param(
        [object]$Plan,
        [object]$Situation
    )

    $departure = $Plan.Departure
    $destination = $Plan.Destination
    $waypoints = @($Plan.Waypoints)
    $departurePoint = $waypoints[0]
    $destinationPoint = $waypoints[$waypoints.Count - 1]
    $aircraftPoint = $waypoints | Where-Object { $_.Ident -ne $departure -and $_.Ident -ne $destination } | Select-Object -First 1
    if ($null -eq $aircraftPoint) {
        $aircraftPoint = $departurePoint
    }

    $routeTokens = @($waypoints |
        Where-Object { $_.Ident -ne $departure -and $_.Ident -ne $destination } |
        ForEach-Object { $_.Ident })
    $routeText = ($routeTokens -join " ")
    if ([string]::IsNullOrWhiteSpace($routeText)) {
        $routeText = "DCT"
    }

    $expectation = Get-ExpectedStations `
        -CurrentSectors $Situation.CurrentSectors `
        -NextSectors $Situation.NextSectors `
        -OnlineControllers $Situation.OnlineControllers

    $expectedCallsigns = $Situation.ExpectedCallsigns
    if ([string]::IsNullOrWhiteSpace($expectedCallsigns) -or $expectedCallsigns.ToUpperInvariant() -eq "AUTO") {
        if (@($expectation.Callsigns).Count -eq 0) {
            $expectedCallsigns = "<none>"
        } else {
            $expectedCallsigns = ($expectation.Callsigns -join ",")
        }
    }

    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add("# Generated from SimBrief/X-Plane FMS by tools\user_route_scenarios\New-FmsHarnessScenario.ps1")
    $lines.Add("# FMS path: $($Plan.Path)")
    if (-not [string]::IsNullOrWhiteSpace($Plan.Cycle)) {
        $lines.Add("# FMS AIRAC cycle: $($Plan.Cycle)")
    }
    $lines.Add("# This scenario replays the supplied controller situation offline.")
    $lines.Add("")
    $lines.Add("name=$($Situation.Name)")
    $lines.Add("now_seconds=1000")
    $lines.Add("")
    $lines.Add("state.flight_active=true")
    $lines.Add("state.departure_released=true")
    $lines.Add("state.arrival_awake=false")
    $lines.Add("state.airborne_since_seconds=900")
    $lines.Add("")
    $lines.Add("flight.callsign=$($Situation.Callsign)")
    $lines.Add("flight.departure_icao=$departure")
    $lines.Add(("flight.departure_lat={0:F6}" -f $departurePoint.LatitudeDeg))
    $lines.Add(("flight.departure_lon={0:F6}" -f $departurePoint.LongitudeDeg))
    $lines.Add("flight.has_departure_coordinates=true")
    $lines.Add("flight.destination_icao=$destination")
    $lines.Add(("flight.destination_lat={0:F6}" -f $destinationPoint.LatitudeDeg))
    $lines.Add(("flight.destination_lon={0:F6}" -f $destinationPoint.LongitudeDeg))
    $lines.Add("flight.has_destination_coordinates=true")
    $lines.Add("flight.route_text=$routeText")
    $lines.Add("")
    $lines.Add("aircraft.valid=true")
    $lines.Add("aircraft.on_ground=false")
    $lines.Add("aircraft.battery_on=true")
    $lines.Add(("aircraft.latitude_deg={0:F6}" -f $aircraftPoint.LatitudeDeg))
    $lines.Add(("aircraft.longitude_deg={0:F6}" -f $aircraftPoint.LongitudeDeg))
    $lines.Add("")
    $lines.Add("radio.com1_active=$($Situation.Com1Active)")
    $lines.Add("xpilot.connected=true")
    $lines.Add("controller.feed_available=true")
    $lines.Add("controller.feed_stale=false")
    $lines.Add("")
    $lines.Add("plan.matched=true")
    $lines.Add("plan.departure_icao=$departure")
    $lines.Add(("plan.departure_lat={0:F6}" -f $departurePoint.LatitudeDeg))
    $lines.Add(("plan.departure_lon={0:F6}" -f $departurePoint.LongitudeDeg))
    $lines.Add("plan.has_departure_coordinates=true")
    $lines.Add("plan.destination_icao=$destination")
    $lines.Add(("plan.destination_lat={0:F6}" -f $destinationPoint.LatitudeDeg))
    $lines.Add(("plan.destination_lon={0:F6}" -f $destinationPoint.LongitudeDeg))
    $lines.Add("plan.has_destination_coordinates=true")
    $lines.Add("plan.route_text=$routeText")
    $lines.Add("")
    foreach ($point in $waypoints) {
        $lines.Add(("# fms.waypoint=ident={0};via={1};altitudeFt={2:F0};lat={3:F6};lon={4:F6}" -f `
            $point.Ident, $point.Via, $point.AltitudeFt, $point.LatitudeDeg, $point.LongitudeDeg))
    }
    $lines.Add("")

    foreach ($sector in $Situation.CurrentSectors) {
        $lines.Add(("route.current_sector=identifier={0};entryDistanceNm=0;matchTokens={0};controllerPrefixes={1}" -f `
            $sector.Identifier, ($sector.Prefixes -join ",")))
    }
    foreach ($sector in $Situation.NextSectors) {
        $lines.Add(("route.next_sector=identifier={0};entryDistanceNm={1};matchTokens={0};controllerPrefixes={2}" -f `
            $sector.Identifier, $sector.EntryDistanceNm, ($sector.Prefixes -join ",")))
    }
    $lines.Add("")
    foreach ($controller in $Situation.OnlineControllers) {
        $lines.Add(("controller.entry=callsign={0};frequency={1};facility={2};actionable=true;atis=false" -f `
            $controller.Callsign, $controller.Frequency, $controller.Facility))
    }
    $lines.Add("")
    $lines.Add("expect.enroute_available=$(Convert-ToScenarioBool $expectation.HasLive)")
    $lines.Add("expect.enroute_callsigns=$expectedCallsigns")
    $lines.Add("")

    return ($lines -join [Environment]::NewLine)
}

$root = Resolve-WorkspaceRoot

if ([string]::IsNullOrWhiteSpace($FmsPath)) {
    $FmsPath = Read-Host "Paste the full path to the SimBrief/X-Plane .fms file"
}

$plan = Parse-FmsPlan $FmsPath

if ([string]::IsNullOrWhiteSpace($SituationPath)) {
    $defaultSituationName = (New-Slug ((Get-FileBaseName $plan.Path) + "_situation")) + ".txt"
    $SituationPath = Join-Path $root (Join-Path "tools\user_route_scenarios\situations" $defaultSituationName)
}

if (-not (Test-Path -LiteralPath $SituationPath -PathType Leaf)) {
    New-SituationTemplate -Path $SituationPath -Plan $plan
    Write-Host ""
    Write-Host "Created situation template:" -ForegroundColor Yellow
    Write-Host $SituationPath
    Write-Host ""
    Write-Host "Edit that file with the sectors/controllers you want to test, then run this tool again."
    if ($OpenTemplate) {
        Start-Process -FilePath "notepad.exe" -ArgumentList "`"$SituationPath`""
    }
    exit 2
}

$situation = Parse-Situation $SituationPath
if ([string]::IsNullOrWhiteSpace($situation.Name)) {
    $situation.Name = "$($plan.Departure) $($plan.Destination) SimBrief controller situation"
}

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $scenarioName = "user_" + (New-Slug "$($plan.Departure)_$($plan.Destination)_$(Get-FileBaseName $plan.Path)") + ".scn"
    $OutputPath = Join-Path $root (Join-Path "tools\regression_harness\scenarios" $scenarioName)
}

$scenarioText = Build-ScenarioText -Plan $plan -Situation $situation
$outputDirectory = Split-Path -Parent $OutputPath
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
Set-Content -LiteralPath $OutputPath -Value $scenarioText -Encoding ASCII

Write-Host ""
Write-Host "Created regression scenario:" -ForegroundColor Green
Write-Host $OutputPath

if (-not $NoRun) {
    $harness = Join-Path $root "build\tools\XVatsimRegressionHarness.exe"
    if (-not (Test-Path -LiteralPath $harness -PathType Leaf)) {
        throw "Regression harness not found. Build XVatsimRegressionHarness first: $harness"
    }

    Write-Host ""
    Write-Host "Running generated scenario..."
    $output = & $harness $OutputPath 2>&1
    $exitCode = $LASTEXITCODE
    if ($exitCode -eq 0) {
        Write-Host "XVatsim Offline Scenario Result: PASS" -ForegroundColor Green
    } else {
        Write-Host "XVatsim Offline Scenario Result: FAIL" -ForegroundColor Red
        $output | ForEach-Object { Write-Host $_ }
        exit $exitCode
    }
}
