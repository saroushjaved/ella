param(
    [string]$Version = "5.4.0.20240606",
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptDir
$toolsDir = Join-Path $projectRoot "tools\tesseract"
$installerDir = Join-Path $scriptDir ".downloads"
$installerName = "tesseract-ocr-w64-setup-$Version.exe"
$installerUrl = "https://github.com/UB-Mannheim/tesseract/releases/download/v$Version/$installerName"
$installerPath = Join-Path $installerDir $installerName

function Test-TesseractBundle {
    $exePath = Join-Path $toolsDir "tesseract.exe"
    $engDataPath = Join-Path $toolsDir "tessdata\eng.traineddata"
    return (Test-Path $exePath) -and (Test-Path $engDataPath)
}

function Resolve-TesseractInstallDir {
    $candidates = @(
        $toolsDir,
        (Join-Path $env:ProgramFiles "Tesseract-OCR"),
        (Join-Path $env:LOCALAPPDATA "Tesseract-OCR"),
        (Join-Path ${env:ProgramFiles(x86)} "Tesseract-OCR")
    ) | Where-Object { $_ -and $_ -ne "" }

    foreach ($candidate in $candidates) {
        $exeCandidate = Join-Path $candidate "tesseract.exe"
        if (Test-Path $exeCandidate) {
            return $candidate
        }
    }

    return $null
}

if ((Test-TesseractBundle) -and (-not $Force)) {
    Write-Host "Tesseract bundle already present at $toolsDir"
    exit 0
}

New-Item -ItemType Directory -Force -Path $installerDir | Out-Null
New-Item -ItemType Directory -Force -Path $toolsDir | Out-Null

if ($Force -or -not (Test-Path $installerPath)) {
    Write-Host "Downloading $installerName ..."
    Invoke-WebRequest -Uri $installerUrl -OutFile $installerPath
}

Write-Host "Installing bundled Tesseract to $toolsDir ..."
$installerArgs = @(
    "/SP-",
    "/VERYSILENT",
    "/SUPPRESSMSGBOXES",
    "/NORESTART",
    "/DIR=$toolsDir"
)

$installProcess = Start-Process -FilePath $installerPath -ArgumentList $installerArgs -Wait -PassThru
if ($installProcess.ExitCode -ne 0) {
    throw "Tesseract installer exited with code $($installProcess.ExitCode)"
}

$exePath = Join-Path $toolsDir "tesseract.exe"
if (-not (Test-Path $exePath)) {
    $detectedInstallDir = Resolve-TesseractInstallDir
    if ($detectedInstallDir -and (Test-Path (Join-Path $detectedInstallDir "tesseract.exe"))) {
        Write-Host "Installer used a different location: $detectedInstallDir"
        Write-Host "Copying runtime files into $toolsDir ..."
        Copy-Item -Path (Join-Path $detectedInstallDir "*") -Destination $toolsDir -Recurse -Force
    }
}

$exePath = Join-Path $toolsDir "tesseract.exe"
if (-not (Test-Path $exePath)) {
    throw "tesseract.exe was not found in $toolsDir after installation"
}

$engDataPath = Join-Path $toolsDir "tessdata\eng.traineddata"
if (-not (Test-Path $engDataPath)) {
    $tessdataDir = Join-Path $toolsDir "tessdata"
    New-Item -ItemType Directory -Force -Path $tessdataDir | Out-Null
    $engUrl = "https://github.com/tesseract-ocr/tessdata_fast/raw/main/eng.traineddata"
    Write-Host "Downloading missing eng.traineddata ..."
    Invoke-WebRequest -Uri $engUrl -OutFile $engDataPath
}

$tessdataDir = Join-Path $toolsDir "tessdata"
if (Test-Path $tessdataDir) {
    $keepFiles = @(
        "eng.traineddata",
        "osd.traineddata",
        "eng.user-patterns",
        "eng.user-words",
        "pdf.ttf"
    )

    Get-ChildItem -Path $tessdataDir -File -ErrorAction SilentlyContinue |
        Where-Object { $keepFiles -notcontains $_.Name } |
        Remove-Item -Force -ErrorAction SilentlyContinue

    $scriptDirPath = Join-Path $tessdataDir "script"
    if (Test-Path $scriptDirPath) {
        Remove-Item -Path $scriptDirPath -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "Bundled Tesseract is ready."
Write-Host "Executable: $exePath"
