param([Parameter(Mandatory=$true)][string]$StageDir, [Parameter(Mandatory=$true)][string]$ReportDir)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent $PSScriptRoot
$manifest = Get-Content -LiteralPath (Join-Path $repoRoot 'packaging/core-runtime.json') -Raw | ConvertFrom-Json
$stage = [IO.Path]::GetFullPath($StageDir).TrimEnd('\','/')
foreach ($name in $manifest.required) {
    if (-not (Test-Path -LiteralPath (Join-Path $stage $name) -PathType Leaf)) { throw "Required runtime file missing: $name" }
}
$files = @(Get-ChildItem -LiteralPath $stage -File -Recurse | ForEach-Object {
    $relative = $_.FullName.Substring($stage.Length + 1).Replace('\','/')
    foreach ($pattern in $manifest.forbiddenPatterns) { if ($relative -match $pattern) { throw "Forbidden core-runtime file: $relative" } }
    if ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Runtime contains a linked file: $relative" }
    [pscustomobject][ordered]@{ path=$relative; bytes=$_.Length; sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
})
[long]$installed = ($files | Measure-Object -Property bytes -Sum).Sum
New-Item -ItemType Directory -Path $ReportDir -Force | Out-Null
[ordered]@{ schemaVersion=1; installedBytes=$installed; fileCount=$files.Count; files=@($files | Sort-Object { $_.bytes } -Descending) } |
    ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $ReportDir 'size-report.json') -Encoding utf8
if ($installed -gt $manifest.sizeLimits.installedMaximumBytes) { throw 'Installed core exceeds the 300 MB release ceiling.' }
$spdxFiles = @($files | ForEach-Object -Begin { $index=0 } -Process {
    $index++
    [ordered]@{ SPDXID="SPDXRef-File-$index"; fileName='./' + $_.path; checksums=@(@{algorithm='SHA256';checksum=$_.sha256}); licenseConcluded='NOASSERTION'; copyrightText='NOASSERTION' }
})
[ordered]@{ spdxVersion='SPDX-2.3'; dataLicense='CC0-1.0'; SPDXID='SPDXRef-DOCUMENT'; name='ELLA Windows core runtime'; documentNamespace=('https://spdx.org/spdxdocs/ella-' + [guid]::NewGuid()); creationInfo=@{created=[DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ');creators=@('Tool: ELLA verify_release.ps1')}; files=$spdxFiles } |
    ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $ReportDir 'ella-runtime.spdx.json') -Encoding utf8
Write-Host ("Verified {0} runtime files, {1:N1} MB installed." -f $files.Count,($installed/1000000))
