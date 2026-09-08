[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateRange(1, [int]::MaxValue)]
    [int]$ExpectedScenarioCount,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$ReceiptRelativePath,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$ReceiptTitle
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$script:ConfigurationStarted = $false
$script:BuildStarted = $false
$script:ScenarioExecutionStarted = $false

function Get-Sha256 {
    param([Parameter(Mandatory = $true)][string]$Path)

    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Get-RelativePath {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Path
    )

    $rootPrefix = [System.IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    if (-not $fullPath.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Path is outside the repository: $fullPath"
    }

    return $fullPath.Substring($rootPrefix.Length).Replace('\', '/')
}

function Resolve-CMakeExecutable {
    $visualStudioCMake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if (Test-Path -LiteralPath $visualStudioCMake -PathType Leaf) {
        return $visualStudioCMake
    }

    $cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
    if ($null -eq $cmakeCommand) {
        throw "CMake was not found. Install Visual Studio CMake tools or put cmake.exe on PATH."
    }

    return $cmakeCommand.Source
}

function Invoke-LoggedCommand {
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string]$LogPath
    )

    Add-Content -LiteralPath $LogPath -Value "===== $Label ====="
    Add-Content -LiteralPath $LogPath -Value ("Executable: " + $Executable)
    Add-Content -LiteralPath $LogPath -Value ("Arguments: " + ($Arguments -join " "))

    & $Executable @Arguments 2>&1 |
        ForEach-Object {
            $line = $_.ToString()
            Write-Host $line
            Add-Content -LiteralPath $LogPath -Value $line
        }
    $exitCode = $LASTEXITCODE
    Add-Content -LiteralPath $LogPath -Value "Exit code: $exitCode"

    if ($exitCode -ne 0) {
        throw "$Label failed with exit code $exitCode"
    }
}

function Get-ScenarioSetFingerprint {
    param(
        [Parameter(Mandatory = $true)][System.IO.FileInfo[]]$Scenarios
    )

    $incrementalHash = [System.Security.Cryptography.IncrementalHash]::CreateHash(
        [System.Security.Cryptography.HashAlgorithmName]::SHA256)
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    $separator = [byte[]]@(0)

    try {
        foreach ($scenario in $Scenarios) {
            $relativeName = "tools/regression_harness/scenarios/$($scenario.Name)"
            $incrementalHash.AppendData($utf8.GetBytes($relativeName))
            $incrementalHash.AppendData($separator)
            $incrementalHash.AppendData([System.IO.File]::ReadAllBytes($scenario.FullName))
            $incrementalHash.AppendData($separator)
        }

        return [System.BitConverter]::ToString($incrementalHash.GetHashAndReset()).Replace('-', '')
    } finally {
        $incrementalHash.Dispose()
    }
}

function Remove-ValidatedProofBuildDirectory {
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [Parameter(Mandatory = $true)][string]$ProofBuildDirectory
    )

    $canonicalRoot = [System.IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\')
    $canonicalBuildParent = [System.IO.Path]::GetFullPath(
        (Join-Path $canonicalRoot 'build')).TrimEnd('\')
    $expectedTarget = [System.IO.Path]::GetFullPath(
        (Join-Path $canonicalBuildParent 'v2-proof-baseline')).TrimEnd('\')
    $canonicalTarget = [System.IO.Path]::GetFullPath($ProofBuildDirectory).TrimEnd('\')
    $targetParent = [System.IO.Path]::GetFullPath(
        (Split-Path -Parent $canonicalTarget)).TrimEnd('\')

    if (-not $canonicalTarget.Equals($expectedTarget, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing proof-build cleanup because the target is not exact. Expected=$expectedTarget Actual=$canonicalTarget"
    }
    if (-not $targetParent.Equals($canonicalBuildParent, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing proof-build cleanup because the target parent is not the repository build directory."
    }
    if ($canonicalTarget.Equals($canonicalRoot, [System.StringComparison]::OrdinalIgnoreCase) -or
        $canonicalTarget.Equals($canonicalBuildParent, [System.StringComparison]::OrdinalIgnoreCase) -or
        $canonicalTarget.Equals([System.IO.Path]::GetPathRoot($canonicalTarget).TrimEnd('\'), [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing unsafe proof-build cleanup target: $canonicalTarget"
    }

    Write-Host "Validated proof build directory: $canonicalTarget"
    if (-not (Test-Path -LiteralPath $canonicalTarget)) {
        Write-Host "Proof build directory does not exist; no cleanup required."
        return
    }
    if (-not (Test-Path -LiteralPath $canonicalTarget -PathType Container)) {
        throw "Proof build target exists but is not a directory: $canonicalTarget"
    }

    $resolvedTarget = (Resolve-Path -LiteralPath $canonicalTarget).Path.TrimEnd('\')
    if (-not $resolvedTarget.Equals($expectedTarget, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing proof-build cleanup because the resolved target changed. Expected=$expectedTarget Actual=$resolvedTarget"
    }

    $targetItem = Get-Item -LiteralPath $resolvedTarget -Force
    if (($targetItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Refusing to remove a proof-build directory that is a reparse point: $resolvedTarget"
    }

    Write-Host "Removing previous proof build directory: $resolvedTarget"
    Remove-Item -LiteralPath $resolvedTarget -Recurse -Force
}

function Publish-ReceiptAtomically {
    param(
        [Parameter(Mandatory = $true)][string]$StagedPath,
        [Parameter(Mandatory = $true)][string]$DestinationPath,
        [Parameter(Mandatory = $true)][string]$BackupPath
    )

    if (Test-Path -LiteralPath $DestinationPath -PathType Leaf) {
        [System.IO.File]::Replace($StagedPath, $DestinationPath, $BackupPath, $true)
        if (Test-Path -LiteralPath $BackupPath -PathType Leaf) {
            Remove-Item -LiteralPath $BackupPath -Force
        }
    } else {
        [System.IO.File]::Move($StagedPath, $DestinationPath)
    }
}

$totalTimer = [System.Diagnostics.Stopwatch]::StartNew()

try {
    $root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
    $scenarioDirectory = Join-Path $root 'tools\regression_harness\scenarios'
    $proofBuildDirectory = [System.IO.Path]::GetFullPath(
        (Join-Path $root 'build\v2-proof-baseline'))
    if ([System.IO.Path]::IsPathRooted($ReceiptRelativePath)) {
        throw "ReceiptRelativePath must be repository-relative."
    }
    $outputsDirectory = [System.IO.Path]::GetFullPath(
        (Join-Path $root 'outputs')).TrimEnd('\')
    $receiptPath = [System.IO.Path]::GetFullPath(
        (Join-Path $root $ReceiptRelativePath))
    $outputsPrefix = $outputsDirectory + '\'
    if (-not $receiptPath.StartsWith(
            $outputsPrefix,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Receipt path must be inside the repository outputs directory: $receiptPath"
    }
    if (-not [System.IO.Path]::GetExtension($receiptPath).Equals(
            '.md',
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Receipt path must name a Markdown file."
    }
    $immutableStepOneReceipt = [System.IO.Path]::GetFullPath(
        (Join-Path $root 'outputs\v2_step_01_windows_proof_baseline_receipt.md'))
    if ($receiptPath.Equals(
            $immutableStepOneReceipt,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "The accepted Step 1 receipt is immutable and cannot be overwritten."
    }
    $scriptPath = $MyInvocation.MyCommand.Path

    Write-Host "XVatsim V2 Windows proof baseline"
    Write-Host "Workspace: $root"
    Write-Host "Expected scenarios: $ExpectedScenarioCount"

    if (-not (Test-Path -LiteralPath $scenarioDirectory -PathType Container)) {
        throw "Scenario directory is missing: $scenarioDirectory"
    }

    $scenarioArray = [System.IO.FileInfo[]]@(
        Get-ChildItem -LiteralPath $scenarioDirectory -Filter '*.scn' -File)
    $scenarioComparison = [System.Comparison[System.IO.FileInfo]]{
        param($left, $right)

        $result = [System.StringComparer]::OrdinalIgnoreCase.Compare($left.Name, $right.Name)
        if ($result -eq 0) {
            $result = [System.StringComparer]::Ordinal.Compare($left.Name, $right.Name)
        }
        return $result
    }
    [System.Array]::Sort($scenarioArray, $scenarioComparison)

    $discoveredCount = $scenarioArray.Count
    Write-Host "Discovered scenarios: $discoveredCount"
    if ($discoveredCount -eq 0) {
        throw "No regression scenarios were found."
    }

    $scenarioSetFingerprint = Get-ScenarioSetFingerprint -Scenarios $scenarioArray
    Write-Host "Scenario-set SHA-256: $scenarioSetFingerprint"

    if ($discoveredCount -ne $ExpectedScenarioCount) {
        throw "Scenario count mismatch: expected=$ExpectedScenarioCount discovered=$discoveredCount"
    }

    $startingHead = (& git -C $root rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to resolve the starting HEAD commit."
    }
    $branch = (& git -C $root branch --show-current).Trim()
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($branch)) {
        throw "Unable to resolve the current branch."
    }
    $workingTreeState = @(& git -C $root status --porcelain=v1 --untracked-files=all)
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to capture the repository working-tree state."
    }

    Remove-ValidatedProofBuildDirectory -RepositoryRoot $root -ProofBuildDirectory $proofBuildDirectory

    $logDirectory = Join-Path $proofBuildDirectory 'logs'
    New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
    $runStamp = Get-Date -Format 'yyyyMMdd_HHmmss'
    $configureLog = Join-Path $logDirectory "configure_$runStamp.log"
    $buildLog = Join-Path $logDirectory "build_$runStamp.log"
    $scenarioLog = Join-Path $logDirectory "scenarios_$runStamp.log"

    $cmake = Resolve-CMakeExecutable
    $cmakeVersionOutput = @(& $cmake --version)
    if ($LASTEXITCODE -ne 0 -or $cmakeVersionOutput.Count -eq 0) {
        throw "Unable to read the CMake version."
    }
    $cmakeVersion = $cmakeVersionOutput[0].Trim()

    $configureTimer = [System.Diagnostics.Stopwatch]::StartNew()
    $script:ConfigurationStarted = $true
    Write-Host "Configuration started: true"
    Invoke-LoggedCommand -Executable $cmake -Label 'Fresh CMake configuration' -LogPath $configureLog -Arguments @(
        '-S', $root,
        '-B', $proofBuildDirectory,
        '-G', 'Visual Studio 18 2026',
        '-A', 'x64',
        "-DXPLANE_SDK_ROOT=$(Join-Path $root 'SDK')",
        '-DXVATSIM_BUILD_PLUGIN=ON',
        '-DXVATSIM_BUILD_REGRESSION_HARNESS=ON',
        '-DXVATSIM_BUILD_PREFLIGHT_BUILDER=OFF',
        '-DCMAKE_CXX_FLAGS=/D_SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS /EHsc'
    )
    $configureTimer.Stop()

    $buildTimer = [System.Diagnostics.Stopwatch]::StartNew()
    $script:BuildStarted = $true
    Write-Host "Build started: true"
    Invoke-LoggedCommand -Executable $cmake -Label 'Release target build' -LogPath $buildLog -Arguments @(
        '--build', $proofBuildDirectory,
        '--config', 'Release',
        '--target', 'XVatsimRegressionHarness', 'XVatsimPlugin'
    )
    $buildTimer.Stop()

    $harnessPath = Join-Path $proofBuildDirectory 'tools\XVatsimRegressionHarness.exe'
    $pluginPath = Join-Path $proofBuildDirectory 'dist\XVatsim\win_x64\XVatsim.xpl'
    foreach ($artifact in @($harnessPath, $pluginPath)) {
        if (-not (Test-Path -LiteralPath $artifact -PathType Leaf)) {
            throw "Required Release artifact is missing: $artifact"
        }
        if ((Get-Item -LiteralPath $artifact).Length -le 0) {
            throw "Required Release artifact is empty: $artifact"
        }
    }

    $scenarioTimer = [System.Diagnostics.Stopwatch]::StartNew()
    $script:ScenarioExecutionStarted = $true
    Write-Host "Scenario execution started: true"
    $executedCount = 0
    $passedCount = 0
    $failedCount = 0
    $failedScenarios = [System.Collections.Generic.List[string]]::new()

    foreach ($scenario in $scenarioArray) {
        $executedCount++
        $singleTimer = [System.Diagnostics.Stopwatch]::StartNew()
        Add-Content -LiteralPath $scenarioLog -Value "===== $($scenario.Name) ====="

        try {
            $scenarioOutput = @(& $harnessPath $scenario.FullName 2>&1)
            $scenarioExitCode = $LASTEXITCODE
            foreach ($line in $scenarioOutput) {
                Add-Content -LiteralPath $scenarioLog -Value $line.ToString()
            }
        } catch {
            $scenarioExitCode = 1
            Add-Content -LiteralPath $scenarioLog -Value $_.Exception.Message
        }

        $singleTimer.Stop()
        Add-Content -LiteralPath $scenarioLog -Value (
            "Exit code: $scenarioExitCode; elapsed ms: $($singleTimer.ElapsedMilliseconds)")

        if ($scenarioExitCode -eq 0) {
            $passedCount++
        } else {
            $failedCount++
            $failedScenarios.Add($scenario.Name)
            Write-Host "FAIL: $($scenario.Name) (exit $scenarioExitCode)" -ForegroundColor Red
        }

        if (($executedCount % 25) -eq 0 -or $executedCount -eq $discoveredCount) {
            Write-Host "Scenario progress: $executedCount/$discoveredCount"
        }
    }
    $scenarioTimer.Stop()

    Write-Host "Expected scenarios: $ExpectedScenarioCount"
    Write-Host "Discovered scenarios: $discoveredCount"
    Write-Host "Executed scenarios: $executedCount"
    Write-Host "Passed scenarios: $passedCount"
    Write-Host "Failed scenarios: $failedCount"

    if ($executedCount -ne $discoveredCount) {
        throw "Scenario execution count mismatch: discovered=$discoveredCount executed=$executedCount"
    }
    if ($failedCount -ne 0) {
        throw "Regression failures: $($failedScenarios -join ', ')"
    }
    if ($passedCount -ne $ExpectedScenarioCount) {
        throw "Regression pass count mismatch: expected=$ExpectedScenarioCount passed=$passedCount"
    }

    $harnessItem = Get-Item -LiteralPath $harnessPath
    $pluginItem = Get-Item -LiteralPath $pluginPath
    $harnessHash = Get-Sha256 $harnessPath
    $pluginHash = Get-Sha256 $pluginPath
    $scriptHash = Get-Sha256 $scriptPath

    $totalTimer.Stop()
    $receiptLines = [System.Collections.Generic.List[string]]::new()
    $receiptLines.Add(('# {0}' -f $ReceiptTitle))
    $receiptLines.Add('')
    $receiptLines.Add('Status: WINDOWS PROOF PASSED — LIVE PROOF PENDING')
    $receiptLines.Add("Validated local: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')")
    $receiptLines.Add("Validated UTC: $([DateTime]::UtcNow.ToString('yyyy-MM-dd HH:mm:ss')) UTC")
    $receiptLines.Add('')
    $receiptLines.Add('## Repository')
    $receiptLines.Add('')
    $receiptLines.Add(('- Branch: `{0}`' -f $branch))
    $receiptLines.Add(('- Starting HEAD: `{0}`' -f $startingHead))
    $receiptLines.Add('- Starting HEAD is the committed base; the following uncommitted files were the working tree under test.')
    $receiptLines.Add('- The receipt is atomically published after this pre-publication working-tree snapshot.')
    $receiptLines.Add('')
    $receiptLines.Add('```text')
    if ($workingTreeState.Count -eq 0) {
        $receiptLines.Add('<clean>')
    } else {
        foreach ($stateLine in $workingTreeState) {
            $receiptLines.Add($stateLine)
        }
    }
    $receiptLines.Add('```')
    $receiptLines.Add('')
    $receiptLines.Add('## Configuration And Build')
    $receiptLines.Add('')
    $receiptLines.Add(('- CMake: `{0}`' -f $cmake))
    $receiptLines.Add(('- CMake version: `{0}`' -f $cmakeVersion))
    $receiptLines.Add('- Generator: `Visual Studio 18 2026`')
    $receiptLines.Add('- Platform: `x64`')
    $receiptLines.Add('- Configuration: `Release`')
    $receiptLines.Add(('- Proof build directory: `{0}`' -f (Get-RelativePath -Root $root -Path $proofBuildDirectory)))
    $receiptLines.Add('- Targets: `XVatsimRegressionHarness`, `XVatsimPlugin`')
    $receiptLines.Add('')
    $receiptLines.Add('## Regression')
    $receiptLines.Add('')
    $receiptLines.Add("- Expected: $ExpectedScenarioCount")
    $receiptLines.Add("- Discovered: $discoveredCount")
    $receiptLines.Add("- Executed: $executedCount")
    $receiptLines.Add("- Passed: $passedCount")
    $receiptLines.Add("- Failed: $failedCount")
    $receiptLines.Add(('- First scenario: `{0}`' -f $scenarioArray[0].Name))
    $receiptLines.Add(('- Last scenario: `{0}`' -f $scenarioArray[$scenarioArray.Count - 1].Name))
    $receiptLines.Add(('- Scenario-set SHA-256: `{0}`' -f $scenarioSetFingerprint))
    $receiptLines.Add('- Fingerprint input per scenario: UTF-8 repository-relative filename with forward slashes, NUL, raw file bytes, NUL; scenarios use ordinal case-insensitive filename order with ordinal tie-break.')
    $receiptLines.Add(('- Detailed log: `{0}`' -f (Get-RelativePath -Root $root -Path $scenarioLog)))
    $receiptLines.Add('')
    $receiptLines.Add('## Elapsed Time')
    $receiptLines.Add('')
    $receiptLines.Add("- Configure: $($configureTimer.Elapsed.ToString())")
    $receiptLines.Add("- Release build: $($buildTimer.Elapsed.ToString())")
    $receiptLines.Add("- Full regression: $($scenarioTimer.Elapsed.ToString())")
    $receiptLines.Add("- Total validation: $($totalTimer.Elapsed.ToString())")
    $receiptLines.Add('')
    $receiptLines.Add('## SHA-256 Evidence')
    $receiptLines.Add('')
    $receiptLines.Add(('- Runner: `{0}`' -f (Get-RelativePath -Root $root -Path $scriptPath)))
    $receiptLines.Add(('  - SHA-256: `{0}`' -f $scriptHash))
    $receiptLines.Add(('- Harness: `{0}`' -f (Get-RelativePath -Root $root -Path $harnessPath)))
    $receiptLines.Add("  - Bytes: $($harnessItem.Length)")
    $receiptLines.Add(('  - SHA-256: `{0}`' -f $harnessHash))
    $receiptLines.Add(('- Plugin: `{0}`' -f (Get-RelativePath -Root $root -Path $pluginPath)))
    $receiptLines.Add("  - Bytes: $($pluginItem.Length)")
    $receiptLines.Add(('  - SHA-256: `{0}`' -f $pluginHash))
    $receiptLines.Add('')
    $receiptLines.Add('## Boundary Confirmation')
    $receiptLines.Add('')
    $receiptLines.Add('- This validation did not build, modify, install, or repackage any V1.2.3 release artifact.')
    $receiptLines.Add('- No saved regression scenario was changed by the validation entry point.')
    $receiptLines.Add('- The receipt was staged under the ignored proof build directory and published only after complete success.')

    $receiptStagingDirectory = Join-Path $proofBuildDirectory 'receipt'
    New-Item -ItemType Directory -Path $receiptStagingDirectory -Force | Out-Null
    $stagedReceipt = Join-Path $receiptStagingDirectory 'v2_step_01_windows_proof_baseline_receipt.md.tmp'
    $receiptBackup = Join-Path $receiptStagingDirectory 'v2_step_01_windows_proof_baseline_receipt.md.backup'
    $utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllLines($stagedReceipt, $receiptLines, $utf8WithoutBom)

    $receiptParentDirectory = Split-Path -Parent $receiptPath
    if (-not (Test-Path -LiteralPath $receiptParentDirectory -PathType Container)) {
        throw "Receipt output directory is missing: $receiptParentDirectory"
    }
    Publish-ReceiptAtomically -StagedPath $stagedReceipt -DestinationPath $receiptPath -BackupPath $receiptBackup

    Write-Host "Runner SHA-256: $scriptHash"
    Write-Host "Harness SHA-256: $harnessHash"
    Write-Host "Plugin SHA-256: $pluginHash"
    Write-Host "Receipt: $receiptPath"
    Write-Host "XVatsim V2 Windows proof baseline PASSED." -ForegroundColor Green
    exit 0
} catch {
    if ($totalTimer.IsRunning) {
        $totalTimer.Stop()
    }
    Write-Host "Configuration started: $($script:ConfigurationStarted.ToString().ToLowerInvariant())"
    Write-Host "Build started: $($script:BuildStarted.ToString().ToLowerInvariant())"
    Write-Host "Scenario execution started: $($script:ScenarioExecutionStarted.ToString().ToLowerInvariant())"
    Write-Host "XVatsim V2 Windows proof baseline FAILED: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
