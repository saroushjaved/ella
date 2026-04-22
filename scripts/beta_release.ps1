param(
    [string]$Version = "0.9.0-beta.1",
    [string]$BuildDir = "build/beta-release",
    [string]$ArtifactRoot = "artifacts",
    [switch]$SkipTests
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildPath = Join-Path $repoRoot $BuildDir
$artifactDir = Join-Path $repoRoot (Join-Path $ArtifactRoot "ella-beta-win64-$Version")
$stageDir = Join-Path $artifactDir "staging"
$portableDir = Join-Path $artifactDir "portable"
$qtConfPath = Join-Path $repoRoot "packaging/qt.conf"
$installerIconPath = Join-Path $repoRoot "packaging/assets/ella_icon.ico"
$innoScriptPath = Join-Path $repoRoot "packaging/ella-beta.iss"
$buildRecoveryScript = Join-Path $repoRoot "scripts/build_with_recovery.ps1"

function Resolve-ExecutablePath {
    param(
        [string]$Name,
        [string[]]$CandidatePaths = @()
    )

    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd -and (Test-Path $cmd.Source)) {
        return $cmd.Source
    }

    foreach ($candidate in $CandidatePaths) {
        if ($candidate -and (Test-Path $candidate)) {
            return (Resolve-Path $candidate).Path
        }
    }

    return $null
}

function Resolve-QtToolCandidate {
    param([string]$ToolName)

    $qtRoot = "C:\\Qt"
    if (-not (Test-Path $qtRoot)) {
        return $null
    }

    $versions = Get-ChildItem -Path $qtRoot -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending
    foreach ($versionDir in $versions) {
        $toolPath = Join-Path $versionDir.FullName ("mingw_64\\bin\\" + $ToolName)
        if (Test-Path $toolPath) {
            return $toolPath
        }
    }
    return $null
}

function Resolve-MingwBinPath {
    $toolsRoot = "C:\\Qt\\Tools"
    if (-not (Test-Path $toolsRoot)) {
        return $null
    }

    $mingwDirs = Get-ChildItem -Path $toolsRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like "mingw*_64" } |
        Sort-Object Name -Descending
    foreach ($mingwDir in $mingwDirs) {
        $path = Join-Path $mingwDir.FullName "bin"
        if (Test-Path $path) {
            return $path
        }
    }
    return $null
}

function Prepend-PathIfExists {
    param([string]$PathValue)
    if (-not $PathValue) { return }
    if (-not (Test-Path $PathValue)) { return }
    $current = $env:PATH -split ';'
    if ($current -notcontains $PathValue) {
        $env:PATH = $PathValue + ";" + $env:PATH
    }
}

$cmakeExe = Resolve-ExecutablePath -Name "cmake.exe" -CandidatePaths @(
    "C:\\Program Files\\CMake\\bin\\cmake.exe",
    "C:\\Qt\\Tools\\CMake_64\\bin\\cmake.exe"
)

if (-not $cmakeExe) {
    throw "cmake.exe was not found. Install CMake or ensure it is available on PATH."
}

$windeployqtExe = Resolve-ExecutablePath -Name "windeployqt.exe" -CandidatePaths @(
    "C:\\Qt\\6.11.0\\mingw_64\\bin\\windeployqt.exe",
    "C:\\Qt\\6.10.0\\mingw_64\\bin\\windeployqt.exe",
    (Resolve-QtToolCandidate -ToolName "windeployqt.exe")
)

$isccExe = Resolve-ExecutablePath -Name "iscc.exe" -CandidatePaths @(
    "C:\\Program Files (x86)\\Inno Setup 6\\ISCC.exe",
    "C:\\Program Files\\Inno Setup 6\\ISCC.exe",
    (Join-Path $env:LOCALAPPDATA "Programs\\Inno Setup 6\\ISCC.exe")
)

Prepend-PathIfExists (Split-Path $cmakeExe -Parent)
Prepend-PathIfExists (Split-Path $windeployqtExe -Parent)
Prepend-PathIfExists (Resolve-MingwBinPath)

function Assert-StageRuntime {
    param([string]$StagePath)

    $requiredFiles = @(
        "appSecondBrain.exe",
        "qt.conf",
        "Qt6Core.dll",
        "Qt6Gui.dll",
        "Qt6Qml.dll",
        "Qt6Quick.dll",
        "Qt6Sql.dll"
    )

    foreach ($name in $requiredFiles) {
        $path = Join-Path $StagePath $name
        if (-not (Test-Path $path)) {
            throw "Missing required runtime file in stage: $name"
        }
    }

    $smokeDataDir = Join-Path $StagePath "smoke_appdata"
    New-Item -ItemType Directory -Force -Path $smokeDataDir | Out-Null

    $hadExistingAppData = [System.Environment]::GetEnvironmentVariable("ELLA_APP_DATA_DIR", "Process") -ne $null
    $previousAppData = [System.Environment]::GetEnvironmentVariable("ELLA_APP_DATA_DIR", "Process")
    [System.Environment]::SetEnvironmentVariable("ELLA_APP_DATA_DIR", $smokeDataDir, "Process")
    try {
        $proc = Start-Process -FilePath (Join-Path $StagePath "appSecondBrain.exe") `
            -WorkingDirectory $StagePath `
            -PassThru
        Start-Sleep -Seconds 4
    } finally {
        if ($hadExistingAppData) {
            [System.Environment]::SetEnvironmentVariable("ELLA_APP_DATA_DIR", $previousAppData, "Process")
        } else {
            [System.Environment]::SetEnvironmentVariable("ELLA_APP_DATA_DIR", $null, "Process")
        }
    }

    if ($proc.HasExited -and $proc.ExitCode -ne 0) {
        throw "Staged app exited unexpectedly during smoke launch with code $($proc.ExitCode)"
    }

    if (-not $proc.HasExited) {
        Stop-Process -Id $proc.Id -Force
    }
}

New-Item -ItemType Directory -Force -Path $buildPath | Out-Null
New-Item -ItemType Directory -Force -Path $artifactDir | Out-Null
New-Item -ItemType Directory -Force -Path $stageDir | Out-Null
New-Item -ItemType Directory -Force -Path $portableDir | Out-Null

Push-Location $buildPath

$cacheFile = Join-Path $buildPath "CMakeCache.txt"
$cacheDir = Join-Path $buildPath "CMakeFiles"
if (Test-Path $cacheFile) {
    Remove-Item -LiteralPath $cacheFile -Force
}
if (Test-Path $cacheDir) {
    Remove-Item -LiteralPath $cacheDir -Recurse -Force
}

$buildId = "beta-" + (Get-Date -Format "yyyyMMdd-HHmmss")
& $cmakeExe `
    -S $repoRoot `
    -B . `
    -G "MinGW Makefiles" `
    -DCMAKE_BUILD_TYPE=Release `
    "-DELLA_RELEASE_VERSION=$Version" `
    "-DELLA_RELEASE_CHANNEL=beta" `
    "-DELLA_BUILD_ID=$buildId"

if (Test-Path $buildRecoveryScript) {
    & $buildRecoveryScript `
        -BuildDir $BuildDir `
        -Targets "appSecondBrain","ellaMemoryBrowserTests" `
        -Jobs 4
} else {
    cmake --build . --target appSecondBrain --config Release -j 4
    cmake --build . --target ellaMemoryBrowserTests --config Release -j 4
}

if (-not $SkipTests) {
    $testExe = Join-Path $buildPath "ellaMemoryBrowserTests.exe"
    if (-not (Test-Path $testExe)) {
        throw "Test executable not found: $testExe"
    }
    & $testExe
}

$appExe = Join-Path $buildPath "appSecondBrain.exe"
if (-not (Test-Path $appExe)) {
    throw "App executable not found: $appExe"
}

Copy-Item $appExe -Destination $stageDir -Force
if (Test-Path $qtConfPath) {
    Copy-Item $qtConfPath -Destination $stageDir -Force
}
if (Test-Path $installerIconPath) {
    Copy-Item $installerIconPath -Destination (Join-Path $stageDir "ella_icon.ico") -Force
}

$toolsDir = Join-Path $repoRoot "tools"
if (Test-Path $toolsDir) {
    Copy-Item $toolsDir -Destination (Join-Path $stageDir "tools") -Recurse -Force
}

if ($windeployqtExe) {
    & $windeployqtExe --qmldir (Join-Path $repoRoot "src/ui") --dir $stageDir $appExe
} else {
    Write-Warning "windeployqt.exe not found on PATH. Portable bundle may miss Qt runtime files."
}

Assert-StageRuntime -StagePath $stageDir

$portableZip = Join-Path $artifactDir ("ella-beta-win64-$Version-portable.zip")
if (Test-Path $portableZip) { Remove-Item $portableZip -Force }
Compress-Archive -Path (Join-Path $stageDir "*") -DestinationPath $portableZip -Force

$installerPath = $null
if ($isccExe -and (Test-Path $innoScriptPath)) {
    & $isccExe "/DAppVersion=$Version" "/DSourceDir=$stageDir" "/DOutputDir=$artifactDir" $innoScriptPath
    $installerPath = Get-ChildItem -Path $artifactDir -Filter "*$Version*.exe" | Select-Object -First 1 | ForEach-Object { $_.FullName }
} elseif (-not $isccExe) {
    Write-Warning "ISCC.exe (Inno Setup) was not found. Installer artifact was skipped."
} else {
    Write-Warning "Inno Setup script not found at $innoScriptPath. Installer artifact was skipped."
}

$hashRows = @()
foreach ($artifact in @($portableZip, $installerPath) | Where-Object { $_ -and (Test-Path $_) }) {
    $hash = Get-FileHash $artifact -Algorithm SHA256
    $hashRows += [PSCustomObject]@{
        Artifact = Split-Path $artifact -Leaf
        SHA256 = $hash.Hash
    }
}

$hashRows | Format-Table -AutoSize | Out-String | Set-Content (Join-Path $artifactDir "SHA256SUMS.txt")

$manifest = [ordered]@{
    version = $Version
    createdAtUtc = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
    artifacts = $hashRows
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $artifactDir "release-manifest.json")

Pop-Location

Write-Host "Beta release artifacts created at: $artifactDir"
