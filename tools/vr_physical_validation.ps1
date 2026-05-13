param(
    [ValidateSet("Disabled", "Optional", "Required")]
    [string]$Mode = "Required",

    [string]$Label = "manual",

    [switch]$SbsMirror,

    [switch]$MissingRuntime,

    [string]$RuntimeJson,

    [switch]$SkipBaseline
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$vsDevCmd = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"
$preset = "windows-msvc-dawn-vendor-openxr"
$buildDir = Join-Path $repoRoot "build\$preset"
$exe = Join-Path $buildDir "dusk.exe"
$disc = Join-Path $repoRoot "game.ciso"
$logDir = Join-Path $repoRoot "validation-logs"
$screenshotDir = Join-Path $repoRoot "validation-screenshots"

$modeValues = @{
    Disabled = 0
    Optional = 1
    Required = 2
}

function Invoke-VsDevCommand {
    param([Parameter(Mandatory = $true)][string]$Command)

    cmd.exe /c "call ""$vsDevCmd"" -arch=x64 && $Command"
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE`: $Command"
    }
}

if (-not (Test-Path -LiteralPath $vsDevCmd)) {
    throw "Visual Studio developer command script not found: $vsDevCmd"
}

if (-not (Test-Path -LiteralPath $disc)) {
    throw "Local disc image not found: $disc"
}

New-Item -ItemType Directory -Force -Path $logDir | Out-Null
New-Item -ItemType Directory -Force -Path $screenshotDir | Out-Null

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$safeLabel = ($Label -replace "[^A-Za-z0-9_.-]", "_")
$mirrorLabel = if ($SbsMirror) { "sbs" } else { "defaultmirror" }
$runtimeLabel = if ($MissingRuntime) { "missingruntime" } else { "runtime" }
$logPath = Join-Path $logDir "$timestamp-$safeLabel-$($Mode.ToLowerInvariant())-$mirrorLabel-$runtimeLabel.log"
$duskStdoutPath = Join-Path $logDir "$timestamp-$safeLabel-$($Mode.ToLowerInvariant())-$mirrorLabel-$runtimeLabel-dusk-stdout.log"
$duskStderrPath = Join-Path $logDir "$timestamp-$safeLabel-$($Mode.ToLowerInvariant())-$mirrorLabel-$runtimeLabel-dusk-stderr.log"

$oldSbsMirror = [Environment]::GetEnvironmentVariable("AURORA_XR_MIRROR_SBS", "Process")
$oldRuntimeJson = [Environment]::GetEnvironmentVariable("XR_RUNTIME_JSON", "Process")

try {
    Start-Transcript -Path $logPath

    Write-Host "Dusk VR physical validation helper"
    Write-Host "Repo: $repoRoot"
    Write-Host "Git revision: $(git -C $repoRoot rev-parse --short HEAD)"
    Write-Host "Preset: $preset"
    Write-Host "Executable: $exe"
    Write-Host "Disc image: $disc"
    Write-Host "Screenshot directory: $screenshotDir"
    Write-Host "Mode: $Mode ($($modeValues[$Mode]))"
    Write-Host "Label: $Label"
    Write-Host "SBS mirror: $($SbsMirror.IsPresent)"
    Write-Host "Missing runtime negative test: $($MissingRuntime.IsPresent)"
    Write-Host "Explicit runtime manifest: $RuntimeJson"

    if (-not $SkipBaseline) {
        Invoke-VsDevCommand "cmake --build --preset $preset --target dusk_openxr_probe dusk"
        Invoke-VsDevCommand "ctest --test-dir ""$buildDir"" -L openxr --output-on-failure"
    }

    if (-not (Test-Path -LiteralPath $exe)) {
        throw "Built executable not found: $exe"
    }

    if ($SbsMirror) {
        [Environment]::SetEnvironmentVariable("AURORA_XR_MIRROR_SBS", "1", "Process")
    } else {
        [Environment]::SetEnvironmentVariable("AURORA_XR_MIRROR_SBS", $null, "Process")
    }

    if ($MissingRuntime) {
        $missingRuntimePath = Join-Path $buildDir "missing-openxr-runtime.json"
        [Environment]::SetEnvironmentVariable("XR_RUNTIME_JSON", $missingRuntimePath, "Process")
        Write-Host "XR_RUNTIME_JSON=$missingRuntimePath"
    } elseif (-not [string]::IsNullOrWhiteSpace($RuntimeJson)) {
        if (-not (Test-Path -LiteralPath $RuntimeJson)) {
            throw "Explicit OpenXR runtime manifest not found: $RuntimeJson"
        }
        [Environment]::SetEnvironmentVariable("XR_RUNTIME_JSON", $RuntimeJson, "Process")
        Write-Host "XR_RUNTIME_JSON=$RuntimeJson"
    } else {
        [Environment]::SetEnvironmentVariable("XR_RUNTIME_JSON", $null, "Process")
    }

    $duskArgs = @(
        "--backend",
        "vulkan",
        "--cvar",
        "backend.xrMode=$($modeValues[$Mode])",
        $disc
    )
    $process = Start-Process `
        -FilePath $exe `
        -ArgumentList $duskArgs `
        -Wait `
        -PassThru `
        -RedirectStandardOutput $duskStdoutPath `
        -RedirectStandardError $duskStderrPath
    $exitCode = $process.ExitCode

    if (Test-Path -LiteralPath $duskStdoutPath) {
        Write-Host "--- Dusk stdout: $duskStdoutPath ---"
        Get-Content -LiteralPath $duskStdoutPath
    }
    if (Test-Path -LiteralPath $duskStderrPath) {
        Write-Host "--- Dusk stderr: $duskStderrPath ---"
        Get-Content -LiteralPath $duskStderrPath
    }

    Write-Host "Dusk exit code: $exitCode"

    if ($MissingRuntime -and $Mode -eq "Required") {
        if ($exitCode -eq 0) {
            throw "Required XR startup unexpectedly succeeded with a missing runtime"
        }
    } elseif ($exitCode -ne 0) {
        throw "Dusk exited with code $exitCode"
    }
} finally {
    [Environment]::SetEnvironmentVariable("AURORA_XR_MIRROR_SBS", $oldSbsMirror, "Process")
    [Environment]::SetEnvironmentVariable("XR_RUNTIME_JSON", $oldRuntimeJson, "Process")

    try {
        Stop-Transcript | Out-Null
    } catch {
    }

    Write-Host "Validation transcript: $logPath"
}
