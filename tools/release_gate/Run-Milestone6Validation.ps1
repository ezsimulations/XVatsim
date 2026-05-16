[CmdletBinding()]
param(
    [string]$WorkspaceRoot = "",
    [switch]$SkipBuild,
    [switch]$SkipInstalledPluginCheck
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$script:Failures = [System.Collections.Generic.List[string]]::new()

function Resolve-WorkspaceRoot {
    if (-not [string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
        return (Resolve-Path -LiteralPath $WorkspaceRoot).Path
    }

    return (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..\..")).Path
}

function Write-Step {
    param([string]$Message)
    Write-Host ""
    Write-Host "== $Message =="
}

function Add-Failure {
    param([string]$Message)
    $script:Failures.Add($Message)
    Write-Host "FAIL: $Message" -ForegroundColor Red
}

function Assert-File {
    param(
        [string]$Path,
        [string]$Label
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        Add-Failure "$Label missing: $Path"
        return $false
    }

    return $true
}

function Assert-Directory {
    param(
        [string]$Path,
        [string]$Label
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        Add-Failure "$Label missing: $Path"
        return $false
    }

    return $true
}

function Get-Sha256 {
    param([string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Invoke-Checked {
    param(
        [string]$Executable,
        [string[]]$Arguments,
        [string]$Label
    )

    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Label failed with exit code $LASTEXITCODE"
    }
}

function Get-TextFilesForScan {
    param([string[]]$Paths)

    $allowedExtensions = @(".cmake", ".cpp", ".h", ".hpp", ".json", ".md", ".ps1", ".scn", ".txt")
    $files = [System.Collections.Generic.List[System.IO.FileInfo]]::new()

    foreach ($path in $Paths) {
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            $item = Get-Item -LiteralPath $path
            if ($allowedExtensions -contains $item.Extension.ToLowerInvariant()) {
                $files.Add($item)
            }
            continue
        }

        if (Test-Path -LiteralPath $path -PathType Container) {
            Get-ChildItem -LiteralPath $path -Recurse -File | ForEach-Object {
                if ($_.FullName -like "*Archived_Previous_Test_Packages_do_not_ship*") {
                    return
                }
                if ($_.FullName -like "*\tools\release_gate\*") {
                    return
                }
                if ($allowedExtensions -contains $_.Extension.ToLowerInvariant()) {
                    $files.Add($_)
                }
            }
        }
    }

    return $files
}

function Test-ForbiddenText {
    param(
        [string]$Root,
        [string]$ActiveKitRoot
    )

    Write-Step "Scanning active source and release text"

    $scanPaths = @(
        (Join-Path $Root "README.md"),
        (Join-Path $Root "CMakeLists.txt"),
        (Join-Path $Root "brain"),
        (Join-Path $Root "core"),
        (Join-Path $Root "docs"),
        (Join-Path $Root "modules"),
        (Join-Path $Root "plugin"),
        (Join-Path $Root "tools\regression_harness"),
        (Join-Path $ActiveKitRoot "KIT_OVERVIEW.txt"),
        (Join-Path $ActiveKitRoot "Store_Submission_Materials"),
        (Join-Path $ActiveKitRoot "XVatsim_1.0.0_Windows_XP12")
    )

    $forbiddenPatterns = @(
        @{ Label = "old HTTP user-agent metadata"; Pattern = "XVatsim/0\.1" },
        @{ Label = "bootstrap plugin wording"; Pattern = "bootstrap plugin" },
        @{ Label = "old plugin bootstrap log wording"; Pattern = "Plugin bootstrap" },
        @{ Label = "old brain-ready log wording"; Pattern = "Brain ready" },
        @{ Label = "fresh-start user-facing wording"; Pattern = "fresh-start" }
    )

    $matches = [System.Collections.Generic.List[string]]::new()
    foreach ($file in Get-TextFilesForScan -Paths $scanPaths) {
        $lineNumber = 0
        foreach ($line in Get-Content -LiteralPath $file.FullName) {
            $lineNumber++
            foreach ($rule in $forbiddenPatterns) {
                if ([regex]::IsMatch($line, $rule.Pattern, [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)) {
                    $relative = [System.IO.Path]::GetRelativePath($Root, $file.FullName)
                    $matches.Add("${relative}:$lineNumber [$($rule.Label)] $line")
                }
            }
        }
    }

    if ($matches.Count -gt 0) {
        foreach ($match in $matches) {
            Add-Failure "Forbidden text found: $match"
        }
        return
    }

    Write-Host "Forbidden text scan passed."
}

function Test-PackageFileSet {
    param([string]$CustomerPackageRoot)

    Write-Step "Checking customer package file set"

    if (-not (Assert-Directory $CustomerPackageRoot "Customer package root")) {
        return
    }

    $forbiddenNameRegex = "(?i)(^|[\\/])(Debug|Release|RelWithDebInfo)([\\/]|$)|(^|[\\/])tmp_|desktop\.ini$|\.pdb$|\.lib$|\.exp$|\.tmp$|\.log$|\.pdf$"
    $forbiddenFiles = @(Get-ChildItem -LiteralPath $CustomerPackageRoot -Recurse -File |
        Where-Object { $_.FullName -match $forbiddenNameRegex })

    foreach ($file in $forbiddenFiles) {
        Add-Failure "Forbidden package file found: $($file.FullName)"
    }

    $expectedFiles = @(
        "Resources\plugins\XVatsim\win_x64\XVatsim.xpl",
        "Resources\plugins\XVatsim\win_x64\ui_transition.mp3",
        "README.txt",
        "CHANGELOG.txt",
        "QUICK_START.txt"
    )

    foreach ($relativePath in $expectedFiles) {
        [void](Assert-File (Join-Path $CustomerPackageRoot $relativePath) "Expected package file")
    }

    if ($forbiddenFiles.Count -eq 0) {
        Write-Host "Customer package file set passed."
    }
}

function Test-ArtifactHashes {
    param(
        [string]$Root,
        [string]$CustomerPackageRoot
    )

    Write-Step "Checking build, package, and installed artifact hashes"

    $artifactPairs = @(
        @{
            Label = "XVatsim.xpl"
            Build = Join-Path $Root "build\dist\XVatsim\win_x64\XVatsim.xpl"
            Package = Join-Path $CustomerPackageRoot "Resources\plugins\XVatsim\win_x64\XVatsim.xpl"
            Installed = "C:\X-Plane 12\Resources\plugins\XVatsim\win_x64\XVatsim.xpl"
        },
        @{
            Label = "ui_transition.mp3"
            Build = Join-Path $Root "build\dist\XVatsim\win_x64\ui_transition.mp3"
            Package = Join-Path $CustomerPackageRoot "Resources\plugins\XVatsim\win_x64\ui_transition.mp3"
            Installed = "C:\X-Plane 12\Resources\plugins\XVatsim\win_x64\ui_transition.mp3"
        }
    )

    foreach ($artifact in $artifactPairs) {
        $buildPath = $artifact.Build
        $packagePath = $artifact.Package
        $installedPath = $artifact.Installed

        $ready = (Assert-File $buildPath "$($artifact.Label) build artifact") -and
            (Assert-File $packagePath "$($artifact.Label) package artifact")

        if (-not $SkipInstalledPluginCheck) {
            $ready = $ready -and (Assert-File $installedPath "$($artifact.Label) installed artifact")
        }

        if (-not $ready) {
            continue
        }

        $buildHash = Get-Sha256 $buildPath
        $packageHash = Get-Sha256 $packagePath

        if ($buildHash -ne $packageHash) {
            Add-Failure "$($artifact.Label) package hash does not match build hash. Build=$buildHash Package=$packageHash"
        }

        if (-not $SkipInstalledPluginCheck) {
            $installedHash = Get-Sha256 $installedPath
            if ($buildHash -ne $installedHash) {
                Add-Failure "$($artifact.Label) installed hash does not match build hash. Build=$buildHash Installed=$installedHash"
            }
        }

        Write-Host "$($artifact.Label) build/package hash: $buildHash"
    }
}

function Test-CheckpointSmoke {
    param(
        [string]$Root,
        [string]$ActiveKitRoot
    )

    Write-Step "Running internal checkpoint package smoke"

    $auditPath = Join-Path $ActiveKitRoot "Store_Submission_Materials\10_V1_Release_Audit.txt"
    $checkpointZip = Join-Path $ActiveKitRoot "Store_Submission_Materials\Internal_Checkpoints\XVatsim_1.0.0_Windows_XP12_M5_checkpoint_do_not_upload.zip"

    if (-not ((Assert-File $auditPath "Release audit") -and (Assert-File $checkpointZip "Internal checkpoint zip"))) {
        return
    }

    $auditText = Get-Content -LiteralPath $auditPath -Raw
    $expectedHashMatch = [regex]::Match($auditText, "SHA256:\s*([0-9A-Fa-f]{64})")
    if (-not $expectedHashMatch.Success) {
        Add-Failure "Release audit does not contain checkpoint SHA256."
        return
    }

    $expectedZipHash = $expectedHashMatch.Groups[1].Value.ToUpperInvariant()
    $actualZipHash = Get-Sha256 $checkpointZip
    if ($expectedZipHash -ne $actualZipHash) {
        Add-Failure "Internal checkpoint zip hash mismatch. Audit=$expectedZipHash Actual=$actualZipHash"
        return
    }

    $smokeRoot = Join-Path $Root ("build\package_smoke\milestone6_gate_" + (Get-Date -Format "yyyyMMdd_HHmmss"))
    $expandedRoot = Join-Path $smokeRoot "expanded"
    $cleanInstallRoot = Join-Path $smokeRoot "Clean_X-Plane_12"

    New-Item -ItemType Directory -Path $expandedRoot -Force | Out-Null
    New-Item -ItemType Directory -Path $cleanInstallRoot -Force | Out-Null

    Expand-Archive -LiteralPath $checkpointZip -DestinationPath $expandedRoot -Force

    $resourceCandidates = @(Get-ChildItem -LiteralPath $expandedRoot -Recurse -Directory |
        Where-Object {
            $_.Name -eq "Resources" -and
            (Test-Path -LiteralPath (Join-Path $_.FullName "plugins\XVatsim\win_x64\XVatsim.xpl") -PathType Leaf)
        })

    if ($resourceCandidates.Count -ne 1) {
        Add-Failure "Checkpoint smoke expected exactly one installable Resources folder, found $($resourceCandidates.Count)."
        return
    }

    Copy-Item -LiteralPath $resourceCandidates[0].FullName -Destination $cleanInstallRoot -Recurse -Force

    $smokeXpl = Join-Path $cleanInstallRoot "Resources\plugins\XVatsim\win_x64\XVatsim.xpl"
    $smokeAudio = Join-Path $cleanInstallRoot "Resources\plugins\XVatsim\win_x64\ui_transition.mp3"

    [void](Assert-File $smokeXpl "Smoke install plugin")
    [void](Assert-File $smokeAudio "Smoke install audio")

    $buildXpl = Join-Path $Root "build\dist\XVatsim\win_x64\XVatsim.xpl"
    if ((Test-Path -LiteralPath $smokeXpl -PathType Leaf) -and (Test-Path -LiteralPath $buildXpl -PathType Leaf)) {
        $smokeHash = Get-Sha256 $smokeXpl
        $buildHash = Get-Sha256 $buildXpl
        if ($smokeHash -ne $buildHash) {
            Add-Failure "Smoke install plugin hash does not match build hash. Build=$buildHash Smoke=$smokeHash"
        }
    }

    Write-Host "Internal checkpoint smoke root: $smokeRoot"
}

$root = Resolve-WorkspaceRoot
$buildDir = Join-Path $root "build"
$harnessExe = Join-Path $buildDir "tools\XVatsimRegressionHarness.exe"
$scenarioDir = Join-Path $root "tools\regression_harness\scenarios"
$activeKitRoot = Join-Path $root "releases\XVatsim_XPlaneOrg_Store_Submission_Kit_1.0.0_2026-05-15"
$customerPackageRoot = Join-Path $activeKitRoot "XVatsim_1.0.0_Windows_XP12"

Write-Host "XVatsim Milestone 6 validation gate"
Write-Host "Workspace: $root"

try {
    if (-not $SkipBuild) {
        Write-Step "Building release targets"

        $knownCmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        if (Test-Path -LiteralPath $knownCmake -PathType Leaf) {
            $cmake = $knownCmake
        } else {
            $cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
            if ($null -eq $cmakeCommand) {
                throw "CMake was not found. Install Visual Studio CMake tools or put cmake.exe on PATH."
            }
            $cmake = $cmakeCommand.Source
        }

        Invoke-Checked $cmake @("--build", $buildDir, "--config", "Release", "--target", "XVatsimRegressionHarness", "XVatsimPlugin") "Release build"
    } else {
        Write-Step "Skipping build by request"
    }

    Write-Step "Running saved regression scenarios"
    if ((Assert-File $harnessExe "Regression harness executable") -and (Assert-Directory $scenarioDir "Scenario directory")) {
        $scenarios = @(Get-ChildItem -LiteralPath $scenarioDir -Filter "*.scn" | Sort-Object Name)
        if ($scenarios.Count -eq 0) {
            Add-Failure "No regression scenarios were found."
        }

        $logDir = Join-Path $root "build\logs"
        New-Item -ItemType Directory -Path $logDir -Force | Out-Null
        $scenarioLog = Join-Path $logDir ("milestone6_scenarios_" + (Get-Date -Format "yyyyMMdd_HHmmss") + ".log")

        foreach ($scenario in $scenarios) {
            Add-Content -LiteralPath $scenarioLog -Value ("===== " + $scenario.Name + " =====")
            $scenarioOutput = & $harnessExe $scenario.FullName 2>&1
            $scenarioOutput | ForEach-Object { $_.ToString() } | Add-Content -LiteralPath $scenarioLog
            if ($LASTEXITCODE -ne 0) {
                Add-Failure "Regression scenario failed: $($scenario.Name)"
            }
        }

        Write-Host "Regression scenarios executed: $($scenarios.Count)"
        Write-Host "Scenario replay log: $scenarioLog"
    }

    Test-ForbiddenText -Root $root -ActiveKitRoot $activeKitRoot
    Test-PackageFileSet -CustomerPackageRoot $customerPackageRoot
    Test-ArtifactHashes -Root $root -CustomerPackageRoot $customerPackageRoot
    Test-CheckpointSmoke -Root $root -ActiveKitRoot $activeKitRoot
} catch {
    Add-Failure $_.Exception.Message
}

Write-Step "Milestone 6 validation result"
if ($script:Failures.Count -gt 0) {
    Write-Host "Milestone 6 validation FAILED with $($script:Failures.Count) issue(s)." -ForegroundColor Red
    foreach ($failure in $script:Failures) {
        Write-Host "- $failure"
    }
    exit 1
}

Write-Host "Milestone 6 validation PASSED." -ForegroundColor Green
exit 0
