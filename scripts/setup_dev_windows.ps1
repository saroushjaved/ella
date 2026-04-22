param(
    [string]$QtVersion = "6.11.0",
    [switch]$InstallTesseract,
    [switch]$CheckOnly
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$qtRoot = Join-Path "C:\Qt" $QtVersion
$qtMingwRoot = Join-Path $qtRoot "mingw_64"
$qtBin = Join-Path $qtMingwRoot "bin"
$qtCmakeDir = Join-Path $qtMingwRoot "lib\cmake\Qt6"
$qtMultimediaDir = Join-Path $qtMingwRoot "lib\cmake\Qt6Multimedia"
$qtPdfDir = Join-Path $qtMingwRoot "lib\cmake\Qt6Pdf"
$qtShaderToolsDir = Join-Path $qtMingwRoot "lib\cmake\Qt6ShaderTools"

function Test-CommandAvailable {
    param([string]$Name)
    return $null -ne (Get-Command $Name -ErrorAction SilentlyContinue)
}

function Add-CheckResult {
    param(
        [System.Collections.ArrayList]$Results,
        [string]$Name,
        [bool]$Passed,
        [string]$Hint
    )
    $null = $Results.Add([PSCustomObject]@{
        Name   = $Name
        Passed = $Passed
        Hint   = $Hint
    })
}

function Print-Checks {
    param([object[]]$Items)
    foreach ($item in $Items) {
        if ($item.Passed) {
            Write-Host "[OK]   $($item.Name)" -ForegroundColor Green
        } else {
            Write-Host "[MISS] $($item.Name)" -ForegroundColor Yellow
            if ($item.Hint) {
                Write-Host "       $($item.Hint)"
            }
        }
    }
}

$checks = New-Object System.Collections.ArrayList

Add-CheckResult -Results $checks -Name "Git available on PATH" `
    -Passed (Test-CommandAvailable -Name "git") `
    -Hint "Install Git for Windows: https://git-scm.com/download/win"

Add-CheckResult -Results $checks -Name "CMake available on PATH" `
    -Passed (Test-CommandAvailable -Name "cmake") `
    -Hint "Install CMake 3.16+: https://cmake.org/download/"

$qtRootExists = Test-Path $qtRoot
Add-CheckResult -Results $checks -Name "Qt root exists: $qtRoot" `
    -Passed $qtRootExists `
    -Hint "Install Qt $QtVersion with MinGW 64-bit via Qt Online Installer."

Add-CheckResult -Results $checks -Name "Qt MinGW bin exists" `
    -Passed (Test-Path $qtBin) `
    -Hint "Expected path: $qtBin"

Add-CheckResult -Results $checks -Name "Qt6 CMake package exists" `
    -Passed (Test-Path $qtCmakeDir) `
    -Hint "Expected path: $qtCmakeDir"

Add-CheckResult -Results $checks -Name "Qt Multimedia module installed" `
    -Passed (Test-Path $qtMultimediaDir) `
    -Hint "Install Qt module: qtmultimedia"

Add-CheckResult -Results $checks -Name "Qt PDF module installed" `
    -Passed (Test-Path $qtPdfDir) `
    -Hint "Install Qt module: qtpdf"

Add-CheckResult -Results $checks -Name "Qt Shader Tools module installed" `
    -Passed (Test-Path $qtShaderToolsDir) `
    -Hint "Install Qt module: qtshadertools"

$mingwMakePath = Join-Path $qtBin "mingw32-make.exe"
$mingwAvailable = (Test-CommandAvailable -Name "mingw32-make") -or (Test-Path $mingwMakePath)
Add-CheckResult -Results $checks -Name "MinGW make available" `
    -Passed $mingwAvailable `
    -Hint "Ensure mingw32-make.exe is installed and on PATH (or present at $mingwMakePath)."

Write-Host ""
Write-Host "ELLA Windows development environment checks" -ForegroundColor Cyan
Write-Host "Repository: $repoRoot"
Write-Host "Qt version target: $QtVersion"
Write-Host ""

Print-Checks -Items $checks

$missing = @($checks | Where-Object { -not $_.Passed })
if ($missing.Count -gt 0) {
    Write-Host ""
    Write-Host "Missing required prerequisites were detected." -ForegroundColor Yellow
    if (-not $CheckOnly) {
        throw "Cannot continue until required tools are installed."
    }
} else {
    Write-Host ""
    Write-Host "All required prerequisites look good." -ForegroundColor Green
}

if ($InstallTesseract) {
    Write-Host ""
    Write-Host "Bootstrapping bundled Tesseract..." -ForegroundColor Cyan
    $setupTesseractScript = Join-Path $PSScriptRoot "setup_tesseract.ps1"
    if (-not (Test-Path $setupTesseractScript)) {
        throw "Required script not found: $setupTesseractScript"
    }
    & $setupTesseractScript
}

Write-Host ""
Write-Host "Next steps:" -ForegroundColor Cyan
Write-Host "1) cmake -S . -B build-dev -G ""MinGW Makefiles"" -DCMAKE_BUILD_TYPE=Debug"
Write-Host "2) cmake --build build-dev --target appSecondBrain ellaMemoryBrowserTests --config Debug -j 4"
Write-Host "3) ctest --test-dir build-dev --output-on-failure -C Debug"
