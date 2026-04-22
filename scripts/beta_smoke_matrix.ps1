param(
    [string]$BuildDir = "build/Desktop_Qt_6_11_0_MinGW_64_bit-Debug",
    [int]$Cycles = 5,
    [switch]$CleanAppDataEachCycle
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildPath = Join-Path $repoRoot $BuildDir
$appExe = Join-Path $buildPath "appSecondBrain.exe"

function Resolve-QtRuntimeBin {
    $windeploy = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
    if ($windeploy -and (Test-Path $windeploy.Source)) {
        return Split-Path $windeploy.Source -Parent
    }

    $candidates = @(
        "C:\\Qt\\6.11.0\\mingw_64\\bin",
        "C:\\Qt\\6.10.0\\mingw_64\\bin",
        "C:\\Qt\\6.9.0\\mingw_64\\bin"
    )

    foreach ($candidate in $candidates) {
        if (Test-Path (Join-Path $candidate "Qt6Gui.dll")) {
            return $candidate
        }
    }

    return $null
}

if (-not (Test-Path $appExe)) {
    throw "appSecondBrain.exe not found at $appExe"
}

$qtRuntimeBin = Resolve-QtRuntimeBin
if (-not $qtRuntimeBin) {
    Write-Warning "Qt runtime bin could not be resolved automatically. Smoke run assumes PATH already contains Qt DLL locations."
}

for ($i = 1; $i -le $Cycles; $i++) {
    Write-Host "Smoke cycle $i/$Cycles - launch and shutdown"
    $runtimeDir = Join-Path $buildPath ("smoke_runtime_" + $i)
    if ($CleanAppDataEachCycle) {
        $runtimeDir = Join-Path $buildPath ("smoke_runtime_" + $i + "_" + (Get-Date -Format "yyyyMMdd-HHmmss"))
        New-Item -ItemType Directory -Path $runtimeDir -Force | Out-Null
    }

    $hadExistingAppData = [System.Environment]::GetEnvironmentVariable("ELLA_APP_DATA_DIR", "Process") -ne $null
    $previousAppData = [System.Environment]::GetEnvironmentVariable("ELLA_APP_DATA_DIR", "Process")
    $previousPath = [System.Environment]::GetEnvironmentVariable("PATH", "Process")
    [System.Environment]::SetEnvironmentVariable("ELLA_APP_DATA_DIR", $runtimeDir, "Process")
    if ($qtRuntimeBin) {
        [System.Environment]::SetEnvironmentVariable("PATH", ($qtRuntimeBin + ";" + $previousPath), "Process")
    }

    try {
        $proc = Start-Process -FilePath $appExe -PassThru -WorkingDirectory $buildPath
        Start-Sleep -Seconds 4
    } finally {
        if ($hadExistingAppData) {
            [System.Environment]::SetEnvironmentVariable("ELLA_APP_DATA_DIR", $previousAppData, "Process")
        } else {
            [System.Environment]::SetEnvironmentVariable("ELLA_APP_DATA_DIR", $null, "Process")
        }
        [System.Environment]::SetEnvironmentVariable("PATH", $previousPath, "Process")
    }

    if ($proc.HasExited) {
        throw "App exited unexpectedly during smoke cycle $i with code $($proc.ExitCode)"
    }

    Stop-Process -Id $proc.Id -Force
    Start-Sleep -Milliseconds 700
}

Write-Host "Smoke matrix complete. All cycles passed."
