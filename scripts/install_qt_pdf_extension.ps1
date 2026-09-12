param(
    [Parameter(Mandatory = $true)]
    [string]$QtRoot,
    [string]$ArchivePath = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$downloadUrl = 'https://download.qt.io/online/qtsdkrepository/windows_x86/extensions/qtpdf/6111/mingw/extensions.qtpdf.6111.win64_mingw/6.11.1-0-202605090527qtpdf-Windows-Windows_11_24H2-Mingw-Windows-Windows_11_24H2-X86_64.7z'
$expectedSha256 = '4CDF6130F0B4405361C0D1DAEA9A88579454E5092F0C0F3330D04E17E3F44FC1'
$qtRootPath = [IO.Path]::GetFullPath($QtRoot)

if (-not (Test-Path -LiteralPath $qtRootPath -PathType Container)) {
    throw "Qt root does not exist: $qtRootPath"
}

$sevenZipCommand = Get-Command 7z.exe -ErrorAction SilentlyContinue
if ($sevenZipCommand) {
    $sevenZipPath = $sevenZipCommand.Source
} else {
    $fallback = 'C:\Program Files\7-Zip\7z.exe'
    if (Test-Path -LiteralPath $fallback -PathType Leaf) {
        $sevenZipPath = $fallback
    } else {
        throw '7-Zip is required to install the Qt PDF extension.'
    }
}

$downloadedArchive = $false
if ($ArchivePath) {
    $archive = [IO.Path]::GetFullPath($ArchivePath)
} else {
    $archive = Join-Path ([IO.Path]::GetTempPath()) ("ella-qtpdf-" + [Guid]::NewGuid().ToString('N') + '.7z')
    Invoke-WebRequest -Uri $downloadUrl -OutFile $archive -UseBasicParsing
    $downloadedArchive = $true
}

try {
    if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) {
        throw "Qt PDF archive does not exist: $archive"
    }

    $actualSha256 = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
    if ($actualSha256 -ne $expectedSha256) {
        throw "Qt PDF archive hash mismatch. Expected $expectedSha256; received $actualSha256."
    }

    & $sevenZipPath x -y "-o$qtRootPath" $archive
    if ($LASTEXITCODE -ne 0) {
        throw "7-Zip failed to extract Qt PDF with exit code $LASTEXITCODE."
    }

    $required = @(
        'bin\Qt6Pdf.dll',
        'bin\Qt6PdfQuick.dll',
        'lib\cmake\Qt6Pdf\Qt6PdfConfig.cmake',
        'lib\cmake\Qt6PdfQuick\Qt6PdfQuickConfig.cmake',
        'qml\QtQuick\Pdf\pdfquickplugin.dll'
    )
    foreach ($relativePath in $required) {
        $installedPath = Join-Path $qtRootPath $relativePath
        if (-not (Test-Path -LiteralPath $installedPath -PathType Leaf)) {
            throw "Qt PDF extension is incomplete; missing $relativePath."
        }
    }

    Write-Host "Installed verified Qt PDF 6.11.1 extension into $qtRootPath"
} finally {
    if ($downloadedArchive -and (Test-Path -LiteralPath $archive -PathType Leaf)) {
        Remove-Item -LiteralPath $archive -Force
    }
}
