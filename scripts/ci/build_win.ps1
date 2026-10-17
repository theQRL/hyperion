$ErrorActionPreference = 'Stop'
$cmake = "$PSScriptRoot/../../deps/cmake/bin/cmake.exe"
$boost = (Resolve-Path "$PSScriptRoot/../../deps/boost").Path
& $cmake -S . -B build -A x64 -DBoost_ROOT="$boost" -DBoost_NO_SYSTEM_PATHS=ON -DUSE_Z3=OFF -DUSE_CVC4=OFF
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& $cmake --build build --config Release --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Compiler build failed.' }
