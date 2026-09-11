# Exercises the exact installer validator embedded in ComponentManager.cpp.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.IO.Compression
$repoRoot = Split-Path -Parent $PSScriptRoot
$source = Get-Content -LiteralPath (Join-Path $repoRoot 'src/components/ComponentManager.cpp') -Raw
$script = [regex]::Match($source, '(?s)const char extractScript\[\] = R"PS\((.*?)\)PS";').Groups[1].Value
if (-not $script) { throw 'Could not find the component archive validator.' }
$scratch = Join-Path $repoRoot ('build/component-archive-tests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch -Force | Out-Null
$validator = Join-Path $scratch 'extract.ps1'
Set-Content -LiteralPath $validator -Value $script -Encoding utf8
$cases = @(
    @{name='valid'; paths=@('bin/tool.exe','models/model.bin'); limit=20; valid=$true},
    @{name='traversal'; paths=@('../escape.txt'); limit=20; valid=$false},
    @{name='absolute'; paths=@('/escape.txt'); limit=20; valid=$false},
    @{name='drive'; paths=@('C:/escape.txt'); limit=20; valid=$false},
    @{name='ads'; paths=@('tool.exe:payload'); limit=20; valid=$false},
    @{name='reserved'; paths=@('bin/CON.txt'); limit=20; valid=$false},
    @{name='trailing-dot'; paths=@('bin/tool.'); limit=20; valid=$false},
    @{name='case-collision'; paths=@('bin/tool.exe','BIN/TOOL.EXE'); limit=20; valid=$false},
    @{name='expanded-limit'; paths=@('tool.exe'); limit=1; valid=$false},
    @{name='symlink'; paths=@('link'); limit=20; valid=$false; link=$true}
)
foreach ($case in $cases) {
    $archive = Join-Path $scratch ($case.name + '.zip')
    $zip = [IO.Compression.ZipFile]::Open($archive,1)
    try {
        foreach ($entryPath in $case.paths) {
            $entry = $zip.CreateEntry($entryPath)
            if ($case.ContainsKey('link')) { $entry.ExternalAttributes = -1577123840 }
            $stream = $entry.Open()
            try { $bytes = [Text.Encoding]::UTF8.GetBytes('content'); $stream.Write($bytes,0,$bytes.Length) } finally { $stream.Dispose() }
        }
    } finally { $zip.Dispose() }
    $destination = Join-Path $scratch ($case.name + '-out')
    $caseArguments = @('-NoProfile','-NonInteractive','-File',$validator,'-Archive',$archive,'-Destination',$destination,'-Limit',$case.limit)
    $stdout = Join-Path $scratch ($case.name + '.stdout.txt')
    $stderr = Join-Path $scratch ($case.name + '.stderr.txt')
    $process = Start-Process -FilePath powershell.exe -ArgumentList $caseArguments -Wait -PassThru -NoNewWindow `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $succeeded = $process.ExitCode -eq 0
    if ($succeeded -ne $case.valid) { throw "Archive validator gave the wrong result for $($case.name)" }
    if (-not $case.valid -and (Get-ChildItem -LiteralPath $destination -File -Recurse -ErrorAction SilentlyContinue)) { throw "Invalid archive wrote files: $($case.name)" }
    Write-Host "PASS $($case.name)"
}
if (Test-Path -LiteralPath (Join-Path $scratch 'escape.txt')) { throw 'Archive escaped its destination.' }
Write-Host "All $($cases.Count) component archive safety scenarios passed. Fixtures: $scratch"
