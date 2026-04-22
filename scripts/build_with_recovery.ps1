param(
    [string]$BuildDir = "build/Desktop_Qt_6_11_0_MinGW_64_bit-Debug",
    [string[]]$Targets = @("appSecondBrain", "ellaMemoryBrowserTests"),
    [int]$Jobs = 4
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildPath = if ([System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir
} else {
    Join-Path $repoRoot $BuildDir
}

if (-not (Test-Path $buildPath)) {
    throw "Build directory not found: $buildPath"
}

if ($Targets.Count -eq 1 -and $Targets[0] -like "*,*") {
    $Targets = $Targets[0].Split(",") | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne "" }
}

$normalizedTargets = @()
foreach ($target in $Targets) {
    foreach ($part in ($target -split ",")) {
        $trimmed = $part.Trim()
        if ($trimmed -ne "") {
            $normalizedTargets += $trimmed
        }
    }
}
$Targets = $normalizedTargets | Select-Object -Unique
if ($Targets.Count -eq 0) {
    throw "No build targets were provided."
}

function Stop-LockingProcesses {
    foreach ($name in @("appSecondBrain", "ellaMemoryBrowserTests")) {
        Get-Process $name -ErrorAction SilentlyContinue | Stop-Process -Force
    }
}

function Rotate-StaleArtifacts {
    param([string]$Path)

    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $rotated = 0

    Get-ChildItem -Path (Join-Path $Path "CMakeFiles") -Recurse -Filter "objects.a" -ErrorAction SilentlyContinue | ForEach-Object {
        $artifactPath = $_.FullName
        $newName = "$artifactPath.stale.$stamp"
        try {
            Rename-Item -LiteralPath $artifactPath -NewName $newName -Force
            $rotated++
        } catch {
            try {
                Remove-Item -LiteralPath $artifactPath -Force
                $rotated++
            } catch {
                Write-Warning "Could not rotate/remove artifact: $artifactPath"
            }
        }
    }

    $qmlCachePath = Join-Path $Path ".rcc\qmlcache"
    if (Test-Path $qmlCachePath) {
        try {
            Remove-Item -Recurse -Force $qmlCachePath
            $rotated++
        } catch {
            Write-Warning "Could not clean qml cache directory: $qmlCachePath"
        }
    }

    Get-ChildItem -Path $Path -Filter "*.exe" -ErrorAction SilentlyContinue | ForEach-Object {
        $exePath = $_.FullName
        if ($_.Name -in @("appSecondBrain.exe", "ellaMemoryBrowserTests.exe")) {
            try {
                Remove-Item -LiteralPath $exePath -Force
            } catch {
                Write-Warning "Could not remove executable: $exePath"
            }
        }
    }

    Write-Host "Rotated/removed stale artifacts: $rotated"
}

function Invoke-Build {
    param([string]$Path, [string[]]$BuildTargets, [int]$BuildJobs)

    Push-Location $Path
    try {
    $builder = Get-Command mingw32-make -ErrorAction SilentlyContinue
    if ($builder) {
        $cmdLine = "mingw32-make -j$BuildJobs " + ($BuildTargets -join " ")
        cmd /c $cmdLine | ForEach-Object { Write-Host $_ }
        $code = $LASTEXITCODE
        return [int]$code
    }

    $cmdLine = "cmake --build . --target " + ($BuildTargets -join " ") + " -j $BuildJobs"
    cmd /c $cmdLine | ForEach-Object { Write-Host $_ }
    $code = $LASTEXITCODE
    return [int]$code
    } finally {
        Pop-Location
    }
}

Write-Host "Building targets: $($Targets -join ', ')"
$exitCode = Invoke-Build -Path $buildPath -BuildTargets $Targets -BuildJobs $Jobs
if ($exitCode -eq 0) {
    Write-Host "Build succeeded on first attempt."
    exit 0
}

Write-Warning "Build failed (exit code $exitCode). Applying recovery and retrying once..."
Stop-LockingProcesses
Start-Sleep -Milliseconds 800
Rotate-StaleArtifacts -Path $buildPath
Start-Sleep -Milliseconds 400

$retryExitCode = Invoke-Build -Path $buildPath -BuildTargets $Targets -BuildJobs $Jobs
if ($retryExitCode -ne 0) {
    throw "Build failed after recovery retry (exit code $retryExitCode)."
}

Write-Host "Build succeeded after recovery retry."
