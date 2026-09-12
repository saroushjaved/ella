param(
    [string]$Version = '1.0.0-rc.1',
    [string]$BuildDir = 'build/release',
    [string]$ArtifactRoot = 'artifacts',
    [string]$QtRoot = '',
    [string]$MingwBin = '',
    [string]$PreviousSizeReport = '',
    [switch]$SkipBuild,
    [switch]$SkipTests,
    [switch]$PortableOnly
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$manifest = Get-Content -LiteralPath (Join-Path $repoRoot 'packaging/core-runtime.json') -Raw | ConvertFrom-Json
if ($Version -notmatch '^[A-Za-z0-9][A-Za-z0-9.+-]*$') { throw 'Version contains invalid path characters.' }

function Resolve-WorkspacePath([string]$Path) {
    $candidate = if ([IO.Path]::IsPathRooted($Path)) { $Path } else { Join-Path $repoRoot $Path }
    $resolved = [IO.Path]::GetFullPath($candidate)
    $prefix = $repoRoot.TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    if (-not $resolved.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw "Path must remain inside the repository: $Path" }
    return $resolved
}
function Reset-ReleaseDirectory([string]$Path) {
    $checked = Resolve-WorkspacePath $Path
    if ($checked -eq $buildPath -or -not $checked.StartsWith($artifactPath + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clean outside the selected artifact directory: $checked"
    }
    if (Test-Path -LiteralPath $checked) {
        $item = Get-Item -LiteralPath $checked
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Refusing to clean linked directory: $checked" }
        Remove-Item -LiteralPath $checked -Recurse -Force
    }
    New-Item -ItemType Directory -Path $checked -Force | Out-Null
}
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
$buildPath = Resolve-WorkspacePath $BuildDir
$artifactPath = Resolve-WorkspacePath $ArtifactRoot
$releasePath = Resolve-WorkspacePath (Join-Path $ArtifactRoot "ella-win64-$Version")
$stagePath = Join-Path $releasePath 'staging'
if (-not $QtRoot) { $QtRoot = if ($env:QT_ROOT_DIR) { $env:QT_ROOT_DIR } else { "C:\Qt\$($manifest.qtVersion)\mingw_64" } }
if (-not $MingwBin) { $MingwBin = "C:\Qt\Tools\$($manifest.compiler)\bin" }
$QtRoot = [IO.Path]::GetFullPath($QtRoot)
$MingwBin = [IO.Path]::GetFullPath($MingwBin)
$qtBin = Join-Path $QtRoot 'bin'
$env:PATH = "$qtBin;$MingwBin;$env:PATH"
$cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
$cmake = if ($cmakeCommand) { $cmakeCommand.Source } else { 'C:\Qt\Tools\CMake_64\bin\cmake.exe' }
if ((-not $SkipBuild -or -not $SkipTests) -and -not (Test-Path -LiteralPath $cmake -PathType Leaf)) {
    throw 'CMake is required to build or test a release.'
}
$windeploy = Join-Path $qtBin 'windeployqt.exe'
$qtpaths = Join-Path $qtBin 'qtpaths.exe'
foreach ($required in @($windeploy,$qtpaths,(Join-Path $MingwBin 'g++.exe'))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Required pinned tool is missing: $required" }
}
$actualQtVersion = & $qtpaths --qt-version
if ($LASTEXITCODE -ne 0 -or $actualQtVersion.Trim() -ne $manifest.qtVersion) { throw "Expected Qt $($manifest.qtVersion); qtpaths reported '$actualQtVersion'." }
New-Item -ItemType Directory -Path $releasePath -Force | Out-Null
Reset-ReleaseDirectory $stagePath
if (-not $SkipBuild) {
    $configure = @('-S',$repoRoot,'-B',$buildPath,'-G','MinGW Makefiles','-DCMAKE_BUILD_TYPE=Release',
        "-DQt6_DIR=$QtRoot/lib/cmake/Qt6", "-DCMAKE_CXX_COMPILER=$MingwBin/g++.exe",
        "-DCMAKE_MAKE_PROGRAM=$MingwBin/mingw32-make.exe", '-DELLA_LOCAL_WINDEPLOYQT=OFF',
        '-DELLA_ENABLE_MEDIA_PLAYBACK=OFF','-DELLA_BUILD_SEMANTIC_HELPER=OFF','-DELLA_ENABLE_IPO=ON','-DBUILD_TESTING=ON',
        "-DELLA_RELEASE_VERSION=$Version",'-DELLA_RELEASE_CHANNEL=release-candidate',
        ('-DELLA_BUILD_ID=release-' + (Get-Date -Format yyyyMMddHHmmss)))
    Invoke-Checked $cmake $configure
    Invoke-Checked $cmake @('--build',$buildPath,'--config','Release','-j','4')
}
if (-not $SkipTests) {
    Invoke-Checked (Join-Path (Split-Path $cmake -Parent) 'ctest.exe') @('--test-dir',$buildPath,'--output-on-failure','-C','Release')
}
$appSource = Join-Path $buildPath $manifest.application
if (-not (Test-Path -LiteralPath $appSource)) { $appSource = Join-Path $buildPath ('Release/' + $manifest.application) }
if (-not (Test-Path -LiteralPath $appSource)) { throw "Application is missing from $buildPath" }
Copy-Item -LiteralPath $appSource -Destination $stagePath
foreach ($file in $manifest.files) {
    $source = Resolve-WorkspacePath $file.source
    $destination = Join-Path $stagePath $file.destination
    New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination
}
# Scan core QML imports; optional media QML must not pull media DLLs into core.
$scanPath = Join-Path $releasePath 'qml-imports'
Reset-ReleaseDirectory $scanPath
Get-ChildItem -LiteralPath (Join-Path $repoRoot 'src/ui') -Recurse -Filter '*.qml' |
    Where-Object { -not (Select-String -LiteralPath $_.FullName -Pattern '^\s*import\s+QtMultimedia' -Quiet) } |
    ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $scanPath $_.Name) }
Invoke-Checked $windeploy @('--release','--no-translations','--no-system-d3d-compiler','--no-opengl-sw',
    '--qmldir',$scanPath,'--dir',$stagePath,(Join-Path $stagePath $manifest.application))
# ELLA selects Qt Quick Controls Basic before loading QML. windeployqt follows
# transitive style metadata and may copy every alternative style; they cannot be
# selected in this build and add several megabytes to both artifacts.
$unusedStylePaths = @(
    'qml/QtQuick/Controls/Fusion','qml/QtQuick/Controls/Imagine',
    'qml/QtQuick/Controls/Material','qml/QtQuick/Controls/Universal',
    'qml/QtQuick/Controls/FluentWinUI3','qml/QtQuick/Controls/Windows',
    'qml/QtQuick/NativeStyle','styles','iconengines',
    'Qt6QuickControls2Fusion.dll','Qt6QuickControls2FusionStyleImpl.dll',
    'Qt6QuickControls2Imagine.dll','Qt6QuickControls2ImagineStyleImpl.dll',
    'Qt6QuickControls2Material.dll','Qt6QuickControls2MaterialStyleImpl.dll',
    'Qt6QuickControls2Universal.dll','Qt6QuickControls2UniversalStyleImpl.dll',
    'Qt6QuickControls2FluentWinUI3StyleImpl.dll','Qt6QuickControls2WindowsStyleImpl.dll',
    'Qt6Widgets.dll'
)
$stagePrefix = [IO.Path]::GetFullPath($stagePath).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
foreach ($relative in $unusedStylePaths) {
    $target = [IO.Path]::GetFullPath((Join-Path $stagePath $relative))
    if (-not $target.StartsWith($stagePrefix,[StringComparison]::OrdinalIgnoreCase)) { throw "Unsafe staged prune path: $relative" }
    if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Recurse -Force }
}
Invoke-Checked (Join-Path $MingwBin 'strip.exe') @('--strip-unneeded',(Join-Path $stagePath $manifest.application))
$licensesSource = Join-Path $QtRoot 'licenses'
if (-not (Test-Path -LiteralPath $licensesSource)) { $licensesSource = Join-Path (Split-Path (Split-Path $QtRoot -Parent) -Parent) 'Licenses' }
if (-not (Test-Path -LiteralPath $licensesSource)) { $licensesSource = Join-Path $repoRoot 'packaging/licenses/qt' }
if (-not (Test-Path -LiteralPath $licensesSource)) { throw 'Qt license texts are missing; cannot publish an incomplete runtime.' }
Copy-Item -LiteralPath $licensesSource -Destination (Join-Path $stagePath 'licenses') -Recurse
$mingwLicenseSource = Join-Path (Split-Path $MingwBin -Parent) 'licenses'
if (-not (Test-Path -LiteralPath $mingwLicenseSource -PathType Container)) { throw 'MinGW runtime license texts are missing; cannot publish an incomplete runtime.' }
Copy-Item -LiteralPath $mingwLicenseSource -Destination (Join-Path $stagePath 'licenses/mingw') -Recurse
& (Join-Path $PSScriptRoot 'verify_release.ps1') -StageDir $stagePath -ReportDir $releasePath
$portable = Join-Path $releasePath "ella-win64-$Version-portable.zip"
if (Test-Path -LiteralPath $portable) { Remove-Item -LiteralPath $portable -Force }
Compress-Archive -Path (Join-Path $stagePath '*') -DestinationPath $portable -CompressionLevel Optimal
$outputs = @($portable)
if (-not $PortableOnly) {
    $iscc = Get-Command iscc.exe -ErrorAction SilentlyContinue
    $isccCandidates = @(
        $(if ($iscc) { $iscc.Source }),
        $(if ($env:LOCALAPPDATA) { Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe' }),
        'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
        'C:\Program Files\Inno Setup 6\ISCC.exe'
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) }
    $isccPath = $isccCandidates | Select-Object -First 1
    if (-not $isccPath -or -not (Test-Path -LiteralPath $isccPath)) { throw 'Inno Setup 6 is required. Use -PortableOnly only for a non-release package check.' }
    Invoke-Checked $isccPath @("/DAppVersion=$Version","/DSourceDir=$stagePath","/DOutputDir=$releasePath",(Join-Path $repoRoot 'packaging/ella-beta.iss'))
    $installer = Join-Path $releasePath "ella-win64-$Version-installer.exe"
    if (-not (Test-Path -LiteralPath $installer)) { throw 'Installer was not produced.' }
    $installerBytes = (Get-Item -LiteralPath $installer).Length
    if ($installerBytes -gt $manifest.sizeLimits.installerMaximumBytes) { throw 'Installer exceeds the 150 MB release ceiling.' }
    if ($installerBytes -gt $manifest.sizeLimits.installerTargetBytes) { Write-Warning 'Installer exceeds the 100 MB target.' }
    $outputs += $installer
}
$artifacts = @($outputs | ForEach-Object {
    [ordered]@{ name=[IO.Path]::GetFileName($_); bytes=(Get-Item -LiteralPath $_).Length; sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant() }
})
$artifacts | ForEach-Object { "$($_.sha256)  $($_.name)" } | Set-Content -LiteralPath (Join-Path $releasePath 'SHA256SUMS.txt') -Encoding ascii
$report = Get-Content -LiteralPath (Join-Path $releasePath 'size-report.json') -Raw | ConvertFrom-Json
if ($PreviousSizeReport) {
    $previous = Get-Content -LiteralPath $PreviousSizeReport -Raw | ConvertFrom-Json
    if ($previous.installedBytes -gt 0 -and $report.installedBytes -gt ($previous.installedBytes * 1.05)) { Write-Warning 'Installed runtime size grew more than 5% compared with the supplied baseline.' }
}
[ordered]@{ version=$Version; qtVersion=$manifest.qtVersion; createdAtUtc=[DateTime]::UtcNow.ToString('o'); installedBytes=$report.installedBytes; artifacts=$artifacts; testsSkipped=[bool]$SkipTests } |
    ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $releasePath 'release-manifest.json') -Encoding utf8
Write-Host "Release candidate artifacts: $releasePath"
