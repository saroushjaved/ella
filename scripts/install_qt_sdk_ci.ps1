param(
    [Parameter(Mandatory = $true)]
    [string]$Python,
    [Parameter(Mandatory = $true)]
    [string]$OutputRoot,
    [string]$QtVersion = '6.11.1',
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$pythonPath = [IO.Path]::GetFullPath($Python)
$outputPath = [IO.Path]::GetFullPath($OutputRoot)
$patchScript = Join-Path $PSScriptRoot 'patch_aqt_qt_611.py'

if (-not (Test-Path -LiteralPath $pythonPath -PathType Leaf)) {
    throw "Python does not exist: $pythonPath"
}
if ($QtVersion -ne '6.11.1') {
    throw "This pinned CI bootstrap supports Qt 6.11.1; received $QtVersion."
}

function Invoke-Checked([string[]]$Arguments) {
    & $pythonPath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Python command failed with exit code ${LASTEXITCODE}: $($Arguments -join ' ')"
    }
}

Invoke-Checked @(
    '-m', 'pip', 'install', '--disable-pip-version-check',
    'setuptools==84.0.0', 'py7zr==1.1.3', 'aqtinstall==3.3.0'
)
Invoke-Checked @($patchScript)

$qtArguments = @(
    '-m', 'aqt', 'install-qt', 'windows', 'desktop', $QtVersion, 'win64_mingw',
    '--outputdir', $outputPath, '--modules', 'qtshadertools'
)
$toolArguments = @(
    '-m', 'aqt', 'install-tool', 'windows', 'desktop', 'tools_mingw1310',
    'qt.tools.win64_mingw1310', '--outputdir', $outputPath
)
if ($DryRun) {
    $qtArguments += '--dry-run'
    $toolArguments += '--dry-run'
}

Invoke-Checked $qtArguments
Invoke-Checked $toolArguments

if (-not $DryRun) {
    $required = @(
        "$QtVersion\mingw_64\bin\qmake.exe",
        "$QtVersion\mingw_64\bin\windeployqt.exe",
        "$QtVersion\mingw_64\lib\cmake\Qt6\Qt6Config.cmake",
        'Tools\mingw1310_64\bin\g++.exe',
        'Tools\mingw1310_64\bin\mingw32-make.exe'
    )
    foreach ($relativePath in $required) {
        if (-not (Test-Path -LiteralPath (Join-Path $outputPath $relativePath) -PathType Leaf)) {
            throw "Qt SDK installation is incomplete; missing $relativePath."
        }
    }
}

Write-Host "Qt $QtVersion SDK installation$(if ($DryRun) { ' dry run' }) completed."
