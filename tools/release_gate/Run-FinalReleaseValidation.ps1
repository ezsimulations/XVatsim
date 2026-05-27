[CmdletBinding()]
param(
    [string]$WorkspaceRoot = "",
    [string]$ActiveKitRoot = "",
    [string]$CustomerPackageRoot = "",
    [string]$ZipPath = "",
    [switch]$SkipBuild,
    [switch]$SkipInstalledPluginCheck
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$script:Failures = [System.Collections.Generic.List[string]]::new()
$script:ScenarioReplayLog = ""
$script:ScenarioReplayCount = 0
$script:FinalZipHash = ""
$script:FinalZipSmokeRoot = ""

function Resolve-WorkspaceRoot {
    if (-not [string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
        return (Resolve-Path -LiteralPath $WorkspaceRoot).Path
    }

    return (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..\..")).Path
}

function Resolve-ActiveKitRoot {
    param([string]$Root)

    if (-not [string]::IsNullOrWhiteSpace($ActiveKitRoot)) {
        return (Resolve-Path -LiteralPath $ActiveKitRoot).Path
    }

    $releaseRoot = Join-Path $Root "releases"
    $kitCandidates = @(Get-ChildItem -LiteralPath $releaseRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like "XVatsim_XPlaneOrg_Store_Submission_Kit_*" } |
        Sort-Object Name -Descending)

    if ($kitCandidates.Count -eq 0) {
        throw "No X-Plane.org store submission kit was found under $releaseRoot."
    }

    return $kitCandidates[0].FullName
}

function Resolve-CustomerPackageRoot {
    param([string]$KitRoot)

    if (-not [string]::IsNullOrWhiteSpace($CustomerPackageRoot)) {
        return (Resolve-Path -LiteralPath $CustomerPackageRoot).Path
    }

    $packageCandidates = @(Get-ChildItem -LiteralPath $KitRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like "XVatsim_*_Windows_XP12" } |
        Sort-Object Name -Descending)

    if ($packageCandidates.Count -eq 0) {
        throw "No customer package folder was found under $KitRoot."
    }

    return $packageCandidates[0].FullName
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

function Get-RelativePathSafe {
    param(
        [string]$Root,
        [string]$Path
    )

    $rootPath = (Resolve-Path -LiteralPath $Root).Path.TrimEnd('\') + '\'
    $fullPath = (Resolve-Path -LiteralPath $Path).Path
    if ($fullPath.StartsWith($rootPath, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $fullPath.Substring($rootPath.Length)
    }

    return $fullPath
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

function Test-ForbiddenSourceText {
    param([string]$Root)

    Write-Step "Scanning active source text"

    $scanPaths = @(
        (Join-Path $Root "README.md"),
        (Join-Path $Root "CMakeLists.txt"),
        (Join-Path $Root "brain"),
        (Join-Path $Root "core"),
        (Join-Path $Root "docs"),
        (Join-Path $Root "modules"),
        (Join-Path $Root "plugin"),
        (Join-Path $Root "tools\regression_harness")
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
                    $relative = Get-RelativePathSafe $Root $file.FullName
                    $matches.Add("${relative}:$lineNumber [$($rule.Label)] $line")
                }
            }
        }
    }

    foreach ($match in $matches) {
        Add-Failure "Forbidden source text found: $match"
    }

    if ($matches.Count -eq 0) {
        Write-Host "Active source text scan passed."
    }
}

function Test-CustomerPackageText {
    param([string]$CustomerPackageRoot)

    Write-Step "Scanning customer package text"

    $forbiddenPatterns = @(
        @{ Label = "beta wording"; Pattern = "\bbeta\b" },
        @{ Label = "preview wording"; Pattern = "\bpreview\b" },
        @{ Label = "release-candidate wording"; Pattern = "\brc[0-9]*\b|release candidate" },
        @{ Label = "internal checkpoint wording"; Pattern = "checkpoint|do_not_upload" },
        @{ Label = "debug wording"; Pattern = "\bdebug\b" }
    )

    $matches = [System.Collections.Generic.List[string]]::new()
    foreach ($file in Get-TextFilesForScan -Paths @($CustomerPackageRoot)) {
        $lineNumber = 0
        foreach ($line in Get-Content -LiteralPath $file.FullName) {
            $lineNumber++
            foreach ($rule in $forbiddenPatterns) {
                if ([regex]::IsMatch($line, $rule.Pattern, [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)) {
                    $relative = Get-RelativePathSafe $CustomerPackageRoot $file.FullName
                    $matches.Add("${relative}:$lineNumber [$($rule.Label)] $line")
                }
            }
        }
    }

    foreach ($match in $matches) {
        Add-Failure "Forbidden customer package text found: $match"
    }

    if ($matches.Count -eq 0) {
        Write-Host "Customer package text scan passed."
    }
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
        "Resources\plugins\XVatsim\win_x64\authority_source_registry.json",
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
        },
        @{
            Label = "authority_source_registry.json"
            Build = Join-Path $Root "build\dist\XVatsim\win_x64\authority_source_registry.json"
            Package = Join-Path $CustomerPackageRoot "Resources\plugins\XVatsim\win_x64\authority_source_registry.json"
            Installed = "C:\X-Plane 12\Resources\plugins\XVatsim\win_x64\authority_source_registry.json"
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

function Test-FinalZipSmoke {
    param(
        [string]$Root,
        [string]$CustomerPackageRoot,
        [string]$FinalZipPath
    )

    Write-Step "Running final store-upload zip smoke"

    if (-not (Assert-File $FinalZipPath "Final store-upload zip")) {
        return
    }

    $zipHash = Get-Sha256 $FinalZipPath
    $script:FinalZipHash = $zipHash
    Write-Host "Final zip SHA-256: $zipHash"

    $smokeRoot = Join-Path $Root ("build\package_smoke\final_release_gate_" + (Get-Date -Format "yyyyMMdd_HHmmss"))
    $script:FinalZipSmokeRoot = $smokeRoot
    $expandedRoot = Join-Path $smokeRoot "expanded"
    New-Item -ItemType Directory -Path $expandedRoot -Force | Out-Null

    Expand-Archive -LiteralPath $FinalZipPath -DestinationPath $expandedRoot -Force

    $resourceCandidates = @(Get-ChildItem -LiteralPath $expandedRoot -Recurse -Directory |
        Where-Object {
            $_.Name -eq "Resources" -and
            (Test-Path -LiteralPath (Join-Path $_.FullName "plugins\XVatsim\win_x64\XVatsim.xpl") -PathType Leaf)
        })

    if ($resourceCandidates.Count -ne 1) {
        Add-Failure "Final zip smoke expected exactly one installable Resources folder, found $($resourceCandidates.Count)."
        return
    }

    $packageRoot = Split-Path -Parent $resourceCandidates[0].FullName
    foreach ($fileName in @("README.txt", "CHANGELOG.txt", "QUICK_START.txt")) {
        [void](Assert-File (Join-Path $packageRoot $fileName) "Final zip customer document")
    }

    $forbiddenNameRegex = "(?i)(^|[\\/])(Debug|Release|RelWithDebInfo)([\\/]|$)|(^|[\\/])tmp_|desktop\.ini$|\.pdb$|\.lib$|\.exp$|\.tmp$|\.log$|\.pdf$"
    $forbiddenFiles = @(Get-ChildItem -LiteralPath $packageRoot -Recurse -File |
        Where-Object { $_.FullName -match $forbiddenNameRegex })
    foreach ($file in $forbiddenFiles) {
        Add-Failure "Forbidden file found inside final zip: $($file.FullName)"
    }

    $buildXpl = Join-Path $Root "build\dist\XVatsim\win_x64\XVatsim.xpl"
    $buildAudio = Join-Path $Root "build\dist\XVatsim\win_x64\ui_transition.mp3"
    $buildRegistry = Join-Path $Root "build\dist\XVatsim\win_x64\authority_source_registry.json"
    $smokeXpl = Join-Path $packageRoot "Resources\plugins\XVatsim\win_x64\XVatsim.xpl"
    $smokeAudio = Join-Path $packageRoot "Resources\plugins\XVatsim\win_x64\ui_transition.mp3"
    $smokeRegistry = Join-Path $packageRoot "Resources\plugins\XVatsim\win_x64\authority_source_registry.json"

    if ((Assert-File $smokeXpl "Final zip plugin") -and (Assert-File $buildXpl "Build plugin")) {
        $buildHash = Get-Sha256 $buildXpl
        $smokeHash = Get-Sha256 $smokeXpl
        if ($buildHash -ne $smokeHash) {
            Add-Failure "Final zip plugin hash does not match build hash. Build=$buildHash Zip=$smokeHash"
        }
    }

    if ((Assert-File $smokeAudio "Final zip audio") -and (Assert-File $buildAudio "Build audio")) {
        $buildHash = Get-Sha256 $buildAudio
        $smokeHash = Get-Sha256 $smokeAudio
        if ($buildHash -ne $smokeHash) {
            Add-Failure "Final zip audio hash does not match build hash. Build=$buildHash Zip=$smokeHash"
        }
    }

    if ((Assert-File $smokeRegistry "Final zip authority registry") -and (Assert-File $buildRegistry "Build authority registry")) {
        $buildHash = Get-Sha256 $buildRegistry
        $smokeHash = Get-Sha256 $smokeRegistry
        if ($buildHash -ne $smokeHash) {
            Add-Failure "Final zip authority registry hash does not match build hash. Build=$buildHash Zip=$smokeHash"
        }
    }

    Write-Host "Final zip smoke root: $smokeRoot"
}

function Write-ValidationReceipt {
    param(
        [string]$Root,
        [string]$ActiveKitRoot,
        [string]$CustomerPackageRoot,
        [string]$FinalZipPath
    )

    $materialsRoot = Join-Path $ActiveKitRoot "Store_Submission_Materials"
    if (-not (Test-Path -LiteralPath $materialsRoot -PathType Container)) {
        return
    }

    $buildXpl = Join-Path $Root "build\dist\XVatsim\win_x64\XVatsim.xpl"
    $packageXpl = Join-Path $CustomerPackageRoot "Resources\plugins\XVatsim\win_x64\XVatsim.xpl"
    $installedXpl = "C:\X-Plane 12\Resources\plugins\XVatsim\win_x64\XVatsim.xpl"
    $buildAudio = Join-Path $Root "build\dist\XVatsim\win_x64\ui_transition.mp3"
    $packageAudio = Join-Path $CustomerPackageRoot "Resources\plugins\XVatsim\win_x64\ui_transition.mp3"
    $buildRegistry = Join-Path $Root "build\dist\XVatsim\win_x64\authority_source_registry.json"
    $packageRegistry = Join-Path $CustomerPackageRoot "Resources\plugins\XVatsim\win_x64\authority_source_registry.json"

    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add("XVatsim Final Release Validation Result")
    $lines.Add("Status: PASSED")
    $lines.Add("Validated: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')")
    $lines.Add("")
    $lines.Add("Customer package:")
    $lines.Add($CustomerPackageRoot)
    $lines.Add("")
    $lines.Add("Final store-upload zip:")
    $lines.Add($FinalZipPath)
    $lines.Add("SHA256: $script:FinalZipHash")
    $lines.Add("")
    $lines.Add("Regression scenarios executed: $script:ScenarioReplayCount")
    $lines.Add("Scenario replay log: $script:ScenarioReplayLog")
    $lines.Add("Final zip smoke root: $script:FinalZipSmokeRoot")
    $lines.Add("")
    $lines.Add("Artifact hashes:")
    $lines.Add("- XVatsim.xpl build: $(Get-Sha256 $buildXpl)")
    $lines.Add("- XVatsim.xpl package: $(Get-Sha256 $packageXpl)")
    if (-not $SkipInstalledPluginCheck -and (Test-Path -LiteralPath $installedXpl -PathType Leaf)) {
        $lines.Add("- XVatsim.xpl installed: $(Get-Sha256 $installedXpl)")
    }
    $lines.Add("- ui_transition.mp3 build: $(Get-Sha256 $buildAudio)")
    $lines.Add("- ui_transition.mp3 package: $(Get-Sha256 $packageAudio)")
    $lines.Add("- authority_source_registry.json build: $(Get-Sha256 $buildRegistry)")
    $lines.Add("- authority_source_registry.json package: $(Get-Sha256 $packageRegistry)")
    $lines.Add("")
    $lines.Add("Gate coverage:")
    $lines.Add("- Release build of XVatsimRegressionHarness and XVatsimPlugin")
    $lines.Add("- Saved regression scenario replay")
    $lines.Add("- Active source text scan")
    $lines.Add("- Customer package file-set scan")
    $lines.Add("- Customer package text scan")
    $lines.Add("- Build/package/installed artifact hash checks")
    $lines.Add("- Final zip clean install smoke")

    $receiptPath = Join-Path $materialsRoot "11_Final_Validation_Result.txt"
    Set-Content -LiteralPath $receiptPath -Value $lines -Encoding ASCII
    Write-Host "Validation receipt: $receiptPath"
}

$root = Resolve-WorkspaceRoot
$buildDir = Join-Path $root "build"
$harnessExe = Join-Path $buildDir "tools\XVatsimRegressionHarness.exe"
$scenarioDir = Join-Path $root "tools\regression_harness\scenarios"
$activeKitRoot = Resolve-ActiveKitRoot -Root $root
$customerPackageRoot = Resolve-CustomerPackageRoot -KitRoot $activeKitRoot

if ([string]::IsNullOrWhiteSpace($ZipPath)) {
    $zipCandidates = @(Get-ChildItem -LiteralPath $activeKitRoot -Filter "XVatsim_1.0.0_Windows_XP12*.zip" -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending)
    if ($zipCandidates.Count -gt 0) {
        $ZipPath = $zipCandidates[0].FullName
    }
}

Write-Host "XVatsim final release validation gate"
Write-Host "Workspace: $root"
Write-Host "Customer package: $customerPackageRoot"
Write-Host "Final zip: $ZipPath"

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
        $scenarioLog = Join-Path $logDir ("final_release_scenarios_" + (Get-Date -Format "yyyyMMdd_HHmmss") + ".log")

        foreach ($scenario in $scenarios) {
            Add-Content -LiteralPath $scenarioLog -Value ("===== " + $scenario.Name + " =====")
            $scenarioOutput = & $harnessExe $scenario.FullName 2>&1
            $scenarioOutput | ForEach-Object { $_.ToString() } | Add-Content -LiteralPath $scenarioLog
            if ($LASTEXITCODE -ne 0) {
                Add-Failure "Regression scenario failed: $($scenario.Name)"
            }
        }

        $script:ScenarioReplayLog = $scenarioLog
        $script:ScenarioReplayCount = $scenarios.Count
        Write-Host "Regression scenarios executed: $($scenarios.Count)"
        Write-Host "Scenario replay log: $scenarioLog"
    }

    Test-ForbiddenSourceText -Root $root
    Test-PackageFileSet -CustomerPackageRoot $customerPackageRoot
    Test-CustomerPackageText -CustomerPackageRoot $customerPackageRoot
    Test-ArtifactHashes -Root $root -CustomerPackageRoot $customerPackageRoot
    Test-FinalZipSmoke -Root $root -CustomerPackageRoot $customerPackageRoot -FinalZipPath $ZipPath
} catch {
    Add-Failure $_.Exception.Message
}

Write-Step "Final release validation result"
if ($script:Failures.Count -gt 0) {
    Write-Host "Final release validation FAILED with $($script:Failures.Count) issue(s)." -ForegroundColor Red
    foreach ($failure in $script:Failures) {
        Write-Host "- $failure"
    }
    exit 1
}

Write-ValidationReceipt -Root $root -ActiveKitRoot $activeKitRoot -CustomerPackageRoot $customerPackageRoot -FinalZipPath $ZipPath
Write-Host "Final release validation PASSED." -ForegroundColor Green
exit 0
