param([string]$BuildDir='build/benchmark')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$build=[IO.Path]::GetFullPath((Join-Path $root $BuildDir))
cmake -S $root -B $build -DELLA_BUILD_SCALE_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release
if($LASTEXITCODE -ne 0){throw 'Benchmark configure failed.'}
cmake --build $build --target ellaRetrievalBenchmark --config Release -j 4
if($LASTEXITCODE -ne 0){throw 'Benchmark build failed.'}
$executable=Join-Path $build 'ellaRetrievalBenchmark.exe'
if(-not(Test-Path $executable)){$executable=Join-Path $build 'Release\ellaRetrievalBenchmark.exe'}
& $executable
if($LASTEXITCODE -ne 0){throw 'Retrieval benchmark missed a release gate.'}
