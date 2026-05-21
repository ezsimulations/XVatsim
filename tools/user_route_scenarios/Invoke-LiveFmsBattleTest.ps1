[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$FmsPath,

    [string]$Callsign = "TEST123",

    [string]$OutputPath = "",

    [switch]$NoRun,

    [switch]$SaveAsHarnessScenario,

    [switch]$ForceOverwrite,

    [switch]$OpenScenario
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$VatSimDataUrl = "https://data.vatsim.net/v3/vatsim-data.json"
$VatSimTransceiversUrl = "https://data.vatsim.net/v3/transceivers-data.json"
$VatSpyDatUrl = "https://raw.githubusercontent.com/vatsimnetwork/vatspy-data-project/master/VATSpy.dat"
$VatSpyBoundariesUrl = "https://raw.githubusercontent.com/vatsimnetwork/vatspy-data-project/master/Boundaries.geojson"
$SimAwareTraconUrl = "https://github.com/vatsimnetwork/simaware-tracon-project/releases/latest/download/TRACONBoundaries.geojson"

function Resolve-WorkspaceRoot {
    return (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..\..")).Path
}

function Normalize-Token {
    param([string]$Value)
    if ([string]::IsNullOrWhiteSpace($Value)) {
        return ""
    }
    return (($Value -replace "[^A-Za-z0-9_-]", "").ToUpperInvariant())
}

function New-Slug {
    param([string]$Value)
    $slug = (($Value -replace "[^A-Za-z0-9]+", "_").Trim("_")).ToLowerInvariant()
    if ([string]::IsNullOrWhiteSpace($slug)) {
        return "live_fms_battle_test"
    }
    return $slug
}

function Read-UrlText {
    param(
        [string]$Url,
        [int]$TimeoutSeconds = 45
    )

    $raw = & curl.exe -L --max-time $TimeoutSeconds -s $Url
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to download $Url"
    }
    $text = ($raw -join "`n")
    if ([string]::IsNullOrWhiteSpace($text)) {
        throw "Downloaded empty payload from $Url"
    }
    return $text
}

function Read-UrlJson {
    param([string]$Url)
    return (ConvertFrom-Json -InputObject (Read-UrlText -Url $Url -TimeoutSeconds 60))
}

function Escape-ScenarioField {
    param([string]$Value)
    if ($null -eq $Value) {
        return ""
    }
    return (($Value -replace "[`r`n]+", ",") -replace ";", "," -replace "#", "")
}

function Format-Number {
    param([double]$Value, [string]$Format = "F6")
    return $Value.ToString($Format, [System.Globalization.CultureInfo]::InvariantCulture)
}

function Convert-FrequencyHzToMhz {
    param($Frequency)
    if ($null -eq $Frequency) {
        return ""
    }
    $numeric = [double]$Frequency
    if ($numeric -gt 1000000.0) {
        return (($numeric / 1000000.0).ToString("F3", [System.Globalization.CultureInfo]::InvariantCulture))
    }
    return $numeric.ToString("F3", [System.Globalization.CultureInfo]::InvariantCulture)
}

function Convert-ToCoordinateToken {
    param(
        [double]$LatitudeDeg,
        [double]$LongitudeDeg
    )

    $latHemisphere = if ($LatitudeDeg -lt 0) { "S" } else { "N" }
    $lonHemisphere = if ($LongitudeDeg -lt 0) { "W" } else { "E" }

    $latAbs = [Math]::Abs($LatitudeDeg)
    $lonAbs = [Math]::Abs($LongitudeDeg)

    $latDegrees = [Math]::Floor($latAbs)
    $lonDegrees = [Math]::Floor($lonAbs)
    $latMinutes = [Math]::Round(($latAbs - $latDegrees) * 60.0)
    $lonMinutes = [Math]::Round(($lonAbs - $lonDegrees) * 60.0)

    if ($latMinutes -ge 60) {
        $latDegrees += 1
        $latMinutes = 0
    }
    if ($lonMinutes -ge 60) {
        $lonDegrees += 1
        $lonMinutes = 0
    }

    return ("{0:00}{1:00}{2}{3:000}{4:00}{5}" -f `
        [int]$latDegrees,
        [int]$latMinutes,
        $latHemisphere,
        [int]$lonDegrees,
        [int]$lonMinutes,
        $lonHemisphere)
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
    if ($waypoints.Count -lt 2) {
        throw "FMS file does not contain enough route waypoints."
    }
    if ([string]::IsNullOrWhiteSpace($plan.Departure)) {
        $plan.Departure = $waypoints[0].Ident
    }
    if ([string]::IsNullOrWhiteSpace($plan.Destination)) {
        $plan.Destination = $waypoints[$waypoints.Count - 1].Ident
    }

    return $plan
}

function Get-GeoJsonExteriorRings {
    param($Geometry)

    $rings = @()
    if ($null -eq $Geometry -or [string]::IsNullOrWhiteSpace($Geometry.type)) {
        return $rings
    }

    if ($Geometry.type -eq "Polygon") {
        if (@($Geometry.coordinates).Count -gt 0) {
            $rings += ,@($Geometry.coordinates[0])
        }
    } elseif ($Geometry.type -eq "MultiPolygon") {
        foreach ($polygon in @($Geometry.coordinates)) {
            if (@($polygon).Count -gt 0) {
                $rings += ,@($polygon[0])
            }
        }
    }

    return $rings
}

function Format-Ring {
    param($Ring)

    $points = @()
    foreach ($coord in @($Ring)) {
        if (@($coord).Count -lt 2) {
            continue
        }
        $lon = [double]$coord[0]
        $lat = [double]$coord[1]
        $points += ("{0},{1}" -f (Format-Number $lat), (Format-Number $lon))
    }
    return ($points -join "|")
}

function Test-PointInRing {
    param(
        [double]$LatitudeDeg,
        [double]$LongitudeDeg,
        $Ring
    )

    $inside = $false
    $points = @($Ring)
    $count = $points.Count
    if ($count -lt 3) {
        return $false
    }

    $j = $count - 1
    for ($i = 0; $i -lt $count; $i++) {
        $xi = [double]$points[$i][0]
        $yi = [double]$points[$i][1]
        $xj = [double]$points[$j][0]
        $yj = [double]$points[$j][1]

        $crosses = (($yi -gt $LatitudeDeg) -ne ($yj -gt $LatitudeDeg))
        if ($crosses) {
            $xIntersect = (($xj - $xi) * ($LatitudeDeg - $yi) / (($yj - $yi) + 0.000000000001)) + $xi
            if ($LongitudeDeg -lt $xIntersect) {
                $inside = -not $inside
            }
        }
        $j = $i
    }

    return $inside
}

function Get-StringArray {
    param($Value)
    if ($null -eq $Value) {
        return @()
    }
    if ($Value -is [System.Array]) {
        return @($Value | ForEach-Object { "$_" })
    }
    return @("$Value")
}

function Get-PropertyValue {
    param(
        $Object,
        [string]$Name,
        $DefaultValue = $null
    )

    if ($null -eq $Object) {
        return $DefaultValue
    }

    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) {
        return $DefaultValue
    }

    return $property.Value
}

function Get-TerminalTokens {
    param($Feature)

    $properties = Get-PropertyValue $Feature "properties"
    $prefixes = @(Get-StringArray (Get-PropertyValue $properties "prefix") | ForEach-Object { Normalize-Token $_ } | Where-Object { $_ })
    $suffix = Normalize-Token (Get-PropertyValue $properties "suffix")
    $suffixes = @()
    if ($suffix -eq "APP" -or $suffix -eq "DEP") {
        $suffixes = @($suffix)
    } elseif ([string]::IsNullOrWhiteSpace($suffix)) {
        $suffixes = @("APP", "DEP")
    }

    $tokens = @()
    foreach ($prefix in $prefixes) {
        foreach ($tokenSuffix in $suffixes) {
            $tokens += "$($prefix)_$tokenSuffix"
        }
    }

    return @($tokens | Sort-Object -Unique)
}

function Get-TerminalCallsignPatterns {
    param([string]$Token)

    $normalized = Normalize-Token $Token
    if ([string]::IsNullOrWhiteSpace($normalized)) {
        return @()
    }

    $separatorIndex = $normalized.LastIndexOf("_")
    if ($separatorIndex -le 0 -or $separatorIndex -ge ($normalized.Length - 1)) {
        return @($normalized)
    }

    $prefix = $normalized.Substring(0, $separatorIndex)
    $suffix = $normalized.Substring($separatorIndex + 1)
    if ($suffix -ne "APP" -and $suffix -ne "DEP") {
        return @($normalized)
    }

    return @($normalized, "$($prefix)_*_$suffix") | Sort-Object -Unique
}

function Read-VatSpyAuthorityLines {
    param([string]$Payload)

    $lines = @()
    $inFirs = $false
    foreach ($rawLine in ($Payload -split "`n")) {
        $line = $rawLine.Trim()
        if ([string]::IsNullOrWhiteSpace($line) -or $line.StartsWith(";")) {
            continue
        }
        if ($line -eq "[FIRs]") {
            $inFirs = $true
            continue
        }
        if ($line.StartsWith("[") -and $line.EndsWith("]")) {
            if ($line -ne "[FIRs]") {
                $inFirs = $false
            }
            continue
        }
        if ($inFirs) {
            $lines += $line
        }
    }
    return $lines
}

function Add-TerminalCoverageForAirport {
    param(
        [System.Collections.Generic.List[string]]$Lines,
        [string]$Prefix,
        [array]$CoverageFeatures
    )

    $isDeparture = $Prefix -eq "departure"
    foreach ($coverage in $CoverageFeatures) {
        $tokens = @($coverage.Tokens)
        $prefixes = @($coverage.Prefixes)
        if ($tokens.Count -eq 0 -or $prefixes.Count -eq 0) {
            continue
        }
        $identifier = $tokens[0]
        $joinedTokens = ($tokens -join ",")
        $patterns = @($tokens | ForEach-Object { Get-TerminalCallsignPatterns $_ } | Sort-Object -Unique)
        $joinedPatterns = ($patterns -join ",")
        $joinedPrefixes = ($prefixes -join ",")
        $Lines.Add("$Prefix.coverage.available=true")
        $Lines.Add("$Prefix.coverage.has_terminal=true")
        $Lines.Add("$Prefix.coverage.sector=identifier=$identifier;entryDistanceNm=0;matchTokens=$joinedTokens;controllerPrefixes=$joinedPrefixes;controllerPatterns=$joinedPatterns;terminalCoverage=true")
    }
}

function Build-LiveScenario {
    param(
        [object]$Plan,
        [string]$ScenarioCallsign,
        [object]$VatsimData,
        [object]$TransceiverData,
        [object]$VatSpyBoundaries,
        [string[]]$VatSpyFirLines,
        [object]$SimAwareTracon
    )

    $waypoints = @($Plan.Waypoints)
    $departurePoint = $waypoints[0]
    $destinationPoint = $waypoints[$waypoints.Count - 1]
    $routeTokens = @($waypoints |
        Where-Object { $_.Ident -ne $Plan.Departure -and $_.Ident -ne $Plan.Destination } |
        ForEach-Object { Convert-ToCoordinateToken $_.LatitudeDeg $_.LongitudeDeg })
    if ($routeTokens.Count -eq 0) {
        $routeTokens = @("DCT")
    }
    $routeText = ($routeTokens -join " ")

    $controllerCallsigns = @{}
    foreach ($controller in @($VatsimData.controllers)) {
        $controllerCallsign = Normalize-Token (Get-PropertyValue $controller "callsign")
        if (-not [string]::IsNullOrWhiteSpace($controllerCallsign)) {
            $controllerCallsigns[$controllerCallsign] = $true
        }
    }
    foreach ($atis in @($VatsimData.atis)) {
        $atisCallsign = Normalize-Token (Get-PropertyValue $atis "callsign")
        if (-not [string]::IsNullOrWhiteSpace($atisCallsign)) {
            $controllerCallsigns[$atisCallsign] = $true
        }
    }

    $departureTerminalCoverage = @()
    $arrivalTerminalCoverage = @()

    foreach ($feature in @($SimAwareTracon.features)) {
        $tokens = @(Get-TerminalTokens $feature)
        if ($tokens.Count -eq 0) {
            continue
        }

        $properties = Get-PropertyValue $feature "properties"
        $prefixes = @($tokens | ForEach-Object { ($_ -split "_")[0] } | Sort-Object -Unique)
        foreach ($ring in @(Get-GeoJsonExteriorRings $feature.geometry)) {
            if (Test-PointInRing $departurePoint.LatitudeDeg $departurePoint.LongitudeDeg $ring) {
                $departureTerminalCoverage += [pscustomobject]@{
                    Id = Normalize-Token (Get-PropertyValue $properties "id")
                    Name = Escape-ScenarioField (Get-PropertyValue $properties "name")
                    Prefixes = $prefixes
                    Tokens = $tokens
                    Ring = $ring
                }
            }
            if (Test-PointInRing $destinationPoint.LatitudeDeg $destinationPoint.LongitudeDeg $ring) {
                $arrivalTerminalCoverage += [pscustomobject]@{
                    Id = Normalize-Token (Get-PropertyValue $properties "id")
                    Name = Escape-ScenarioField (Get-PropertyValue $properties "name")
                    Prefixes = $prefixes
                    Tokens = $tokens
                    Ring = $ring
                }
            }
        }
    }

    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add("# Live battle test generated by Invoke-LiveFmsBattleTest.ps1")
    $lines.Add("# FMS path: $($Plan.Path)")
    if (-not [string]::IsNullOrWhiteSpace($Plan.Cycle)) {
        $lines.Add("# FMS AIRAC cycle: $($Plan.Cycle)")
    }
    $lines.Add("# Route text converted to coordinate tokens: $routeText")
    $lines.Add("name=Live $($Plan.Departure) $($Plan.Destination) $ScenarioCallsign battle test")
    $lines.Add("now_seconds=1000")
    $lines.Add("")
    $lines.Add("xpilot.connected=true")
    $lines.Add("controller.feed_available=true")
    $lines.Add("controller.feed_stale=false")
    $lines.Add("authority.board_handoff=true")
    $lines.Add("")
    $lines.Add("aircraft.valid=true")
    $lines.Add("aircraft.on_ground=false")
    $lines.Add("aircraft.battery_on=true")
    $lines.Add("aircraft.latitude_deg=$(Format-Number $departurePoint.LatitudeDeg)")
    $lines.Add("aircraft.longitude_deg=$(Format-Number $departurePoint.LongitudeDeg)")
    $lines.Add("")
    $lines.Add("flight.callsign=$(Normalize-Token $ScenarioCallsign)")
    $lines.Add("flight.departure_icao=$($Plan.Departure)")
    $lines.Add("flight.departure_lat=$(Format-Number $departurePoint.LatitudeDeg)")
    $lines.Add("flight.departure_lon=$(Format-Number $departurePoint.LongitudeDeg)")
    $lines.Add("flight.destination_icao=$($Plan.Destination)")
    $lines.Add("")
    $lines.Add("radio.com1_active=122.800")
    $lines.Add("radio.com2_active=122.800")
    $lines.Add("")
    $lines.Add("plan.matched=true")
    $lines.Add("plan.stale=false")
    $lines.Add("plan.departure_icao=$($Plan.Departure)")
    $lines.Add("plan.departure_lat=$(Format-Number $departurePoint.LatitudeDeg)")
    $lines.Add("plan.departure_lon=$(Format-Number $departurePoint.LongitudeDeg)")
    $lines.Add("plan.has_departure_coordinates=true")
    $lines.Add("plan.destination_icao=$($Plan.Destination)")
    $lines.Add("plan.destination_lat=$(Format-Number $destinationPoint.LatitudeDeg)")
    $lines.Add("plan.destination_lon=$(Format-Number $destinationPoint.LongitudeDeg)")
    $lines.Add("plan.has_destination_coordinates=true")
    $lines.Add("plan.route_text=$routeText")
    $lines.Add("")
    $lines.Add("resolver.route_resolve=true")
    $lines.Add("")

    Add-TerminalCoverageForAirport -Lines $lines -Prefix "departure" -CoverageFeatures $departureTerminalCoverage
    Add-TerminalCoverageForAirport -Lines $lines -Prefix "arrival" -CoverageFeatures $arrivalTerminalCoverage
    if ($departureTerminalCoverage.Count -gt 0 -or $arrivalTerminalCoverage.Count -gt 0) {
        $lines.Add("")
    }

    foreach ($coverage in @($departureTerminalCoverage + $arrivalTerminalCoverage)) {
        $ringText = Format-Ring $coverage.Ring
        foreach ($token in @($coverage.Tokens)) {
            $parts = @($token -split "_")
            if ($parts.Count -lt 2) {
                continue
            }
            $terminalPrefix = $parts[0]
            $terminalSuffix = $parts[$parts.Count - 1]
            $patterns = ((Get-TerminalCallsignPatterns $token) -join ",")
            $name = Escape-ScenarioField $coverage.Name
            $lines.Add("authority_position.tracon=id=$token;name=$name;polygon=$token;patterns=$patterns;kind=terminal")
            $lines.Add("authority_polygon.tracon=id=$terminalPrefix;name=$name;suffix=$terminalSuffix;prefixes=$terminalPrefix;polygon=$ringText")
        }
    }
    if ($departureTerminalCoverage.Count -gt 0 -or $arrivalTerminalCoverage.Count -gt 0) {
        $lines.Add("")
    }

    $lines.Add("transceiver.available=true")
    $lines.Add("transceiver.stale=false")
    foreach ($client in @($TransceiverData)) {
        $clientCallsign = Normalize-Token (Get-PropertyValue $client "callsign")
        if (-not $controllerCallsigns.ContainsKey($clientCallsign)) {
            continue
        }
        foreach ($transceiver in @(Get-PropertyValue $client "transceivers" @())) {
            $frequency = Convert-FrequencyHzToMhz (Get-PropertyValue $transceiver "frequency")
            $lat = [double](Get-PropertyValue $transceiver "latDeg" 0.0)
            $lon = [double](Get-PropertyValue $transceiver "lonDeg" 0.0)
            $lines.Add("transceiver.candidate=callsign=$clientCallsign;frequency=$frequency;lat=$(Format-Number $lat);lon=$(Format-Number $lon)")
        }
    }
    $lines.Add("")

    foreach ($controller in @($VatsimData.controllers)) {
        $controllerCallsign = Normalize-Token (Get-PropertyValue $controller "callsign")
        if ([string]::IsNullOrWhiteSpace($controllerCallsign)) {
            continue
        }
        $frequency = Escape-ScenarioField (Get-PropertyValue $controller "frequency")
        $facility = [int](Get-PropertyValue $controller "facility" 0)
        $textAtis = Escape-ScenarioField ((Get-PropertyValue $controller "text_atis" @()) -join ",")
        $textPart = if ([string]::IsNullOrWhiteSpace($textAtis)) { "" } else { ";text_atis=$textAtis" }
        $lines.Add("controller.entry=callsign=$controllerCallsign;frequency=$frequency;facility=$facility;actionable=true;atis=false$textPart")
    }
    foreach ($atis in @($VatsimData.atis)) {
        $atisCallsign = Normalize-Token (Get-PropertyValue $atis "callsign")
        if ([string]::IsNullOrWhiteSpace($atisCallsign)) {
            continue
        }
        $frequency = Escape-ScenarioField (Get-PropertyValue $atis "frequency")
        $textAtis = Escape-ScenarioField ((Get-PropertyValue $atis "text_atis" @()) -join ",")
        $textPart = if ([string]::IsNullOrWhiteSpace($textAtis)) { "" } else { ";text_atis=$textAtis" }
        $lines.Add("controller.entry=callsign=$atisCallsign;frequency=$frequency;facility=4;actionable=true;atis=true$textPart")
    }
    $lines.Add("")

    foreach ($feature in @($VatSpyBoundaries.features)) {
        $properties = Get-PropertyValue $feature "properties"
        $label = Normalize-Token (Get-PropertyValue $properties "id")
        if ([string]::IsNullOrWhiteSpace($label)) {
            continue
        }
        foreach ($ring in @(Get-GeoJsonExteriorRings $feature.geometry)) {
            $ringText = Format-Ring $ring
            if ([string]::IsNullOrWhiteSpace($ringText)) {
                continue
            }
            $lines.Add("resolver.center_feature=label=$label;tokens=$label;polygon=$ringText")
        }
    }
    $lines.Add("")

    foreach ($feature in @($departureTerminalCoverage + $arrivalTerminalCoverage)) {
        $id = if ([string]::IsNullOrWhiteSpace($feature.Id)) { $feature.Tokens[0] } else { $feature.Id }
        $prefixes = ($feature.Prefixes -join ",")
        $name = Escape-ScenarioField $feature.Name
        $ringText = Format-Ring $feature.Ring
        $lines.Add("resolver.terminal_feature=id=$id;name=$name;prefixes=$prefixes;polygon=$ringText")
    }
    if ($departureTerminalCoverage.Count -gt 0 -or $arrivalTerminalCoverage.Count -gt 0) {
        $lines.Add("")
    }

    foreach ($line in $VatSpyFirLines) {
        $lines.Add("resolver.authority_catalog_fir=$line")
    }
    $lines.Add("")

    return ($lines -join [Environment]::NewLine)
}

function Write-BattleSummary {
    param([string[]]$Output)

    $summaryKeys = @(
        "DepartureCollectedCallsigns:",
        "ArrivalAirspaceCallsigns:",
        "ArrivalLocalCallsigns:",
        "ResolverRouteStatus:",
        "ResolverRouteCurrentSectors:",
        "ResolverRouteNextSectors:",
        "ResolverAuthorityStatus:",
        "ResolverAuthorityRelevantMatches:",
        "ResolverAuthorityProofSources:",
        "ResolverEnrouteCallsigns:"
    )

    Write-Host ""
    Write-Host "Battle-test summary:" -ForegroundColor Cyan
    foreach ($key in $summaryKeys) {
        $line = $Output | Where-Object { $_.StartsWith($key) } | Select-Object -First 1
        if (-not [string]::IsNullOrWhiteSpace($line)) {
            Write-Host $line
        }
    }
}

$root = Resolve-WorkspaceRoot
$plan = Parse-FmsPlan $FmsPath

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $suffix = if ($SaveAsHarnessScenario) { "_battle_test.scn" } else { "_probe.scn" }
    $scenarioName = "live_" + (New-Slug "$($plan.Departure)_$($plan.Destination)_$Callsign") + $suffix
    $outputFolder = if ($SaveAsHarnessScenario) { "tools\regression_harness\scenarios" } else { "tools\user_route_scenarios\generated_live" }
    $OutputPath = Join-Path $root (Join-Path $outputFolder $scenarioName)
}

if ((Test-Path -LiteralPath $OutputPath -PathType Leaf) -and $SaveAsHarnessScenario -and -not $ForceOverwrite) {
    throw "Refusing to overwrite permanent harness scenario without -ForceOverwrite: $OutputPath"
}

Write-Host "Downloading live VATSIM/controller source data..." -ForegroundColor Cyan
$vatsimData = Read-UrlJson $VatSimDataUrl
$transceiverData = Read-UrlJson $VatSimTransceiversUrl
$vatspyDat = Read-UrlText $VatSpyDatUrl
$vatspyBoundaries = ConvertFrom-Json -InputObject (Read-UrlText $VatSpyBoundariesUrl -TimeoutSeconds 60)
$simAwareTracon = ConvertFrom-Json -InputObject (Read-UrlText $SimAwareTraconUrl -TimeoutSeconds 60)

$scenarioText = Build-LiveScenario `
    -Plan $plan `
    -ScenarioCallsign $Callsign `
    -VatsimData $vatsimData `
    -TransceiverData $transceiverData `
    -VatSpyBoundaries $vatspyBoundaries `
    -VatSpyFirLines @(Read-VatSpyAuthorityLines $vatspyDat) `
    -SimAwareTracon $simAwareTracon

$outputDirectory = Split-Path -Parent $OutputPath
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
Set-Content -LiteralPath $OutputPath -Value $scenarioText -Encoding ASCII

Write-Host ""
Write-Host "Created live battle-test scenario:" -ForegroundColor Green
Write-Host $OutputPath

if ($OpenScenario) {
    Start-Process -FilePath "notepad.exe" -ArgumentList "`"$OutputPath`""
}

if ($NoRun) {
    return
}

$harness = Join-Path $root "build\tools\XVatsimRegressionHarness.exe"
if (-not (Test-Path -LiteralPath $harness -PathType Leaf)) {
    throw "Regression harness not found: $harness"
}

$logDirectory = Join-Path $root "build\logs"
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
$logPath = Join-Path $logDirectory (([System.IO.Path]::GetFileNameWithoutExtension($OutputPath)) + "_output.txt")

Write-Host ""
Write-Host "Running harness..."
$harnessOutput = @(& $harness $OutputPath 2>&1)
$exitCode = $LASTEXITCODE
Set-Content -LiteralPath $logPath -Value $harnessOutput -Encoding ASCII

if ($exitCode -eq 0) {
    Write-Host "XVatsim Live Battle Test Probe: COMPLETE" -ForegroundColor Green
} else {
    Write-Host "XVatsim Live Battle Test Probe: HARNESS ERROR" -ForegroundColor Red
}
Write-Host "Harness log: $logPath"
Write-BattleSummary -Output $harnessOutput

if ($exitCode -ne 0) {
    exit $exitCode
}
