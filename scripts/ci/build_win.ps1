$ErrorActionPreference = 'Stop'
$cmake = "$PSScriptRoot/../../deps/cmake/bin/cmake.exe"
$boost = (Resolve-Path "$PSScriptRoot/../../deps/boost").Path
# Match Boost's runtime-link=static from install_deps.ps1. CMake also forwards
# this setting to JsonCpp so all linked libraries use the same Release runtime.
& $cmake -S . -B build -A x64 -DBoost_ROOT="$boost" -DBoost_NO_SYSTEM_PATHS=ON -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DUSE_Z3=OFF -DUSE_CVC4=OFF
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& $cmake --build build --config Release --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Compiler build failed.' }
