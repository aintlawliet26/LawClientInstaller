$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = Join-Path $root "build"
cmake -S $root -B $build -G "Visual Studio 17 2022" -A x64
cmake --build $build --config Release --target LawClientInstaller
Write-Host "Built: $build\Release\LawClientInstaller.exe"
