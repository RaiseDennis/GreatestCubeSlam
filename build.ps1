# Configures and builds the game with the portable toolchain from setup.ps1.
#   .\build.ps1            -> Release build
#   .\build.ps1 -Run       -> build, then launch
#   .\build.ps1 -Debug     -> Debug build
param([switch]$Run, [switch]$Debug)
$ErrorActionPreference = 'Stop'

$root  = $PSScriptRoot
$tools = Join-Path $root '.tools'
$cmakeBin = Get-ChildItem (Join-Path $tools 'cmake-*\bin') -Directory -ErrorAction SilentlyContinue | Select-Object -First 1
$gccBin   = Join-Path $tools 'mingw64\bin'
if (-not $cmakeBin -or -not (Test-Path $gccBin)) {
    Write-Host 'Toolchain missing, running setup.ps1 first...'
    & (Join-Path $root 'setup.ps1')
    $cmakeBin = Get-ChildItem (Join-Path $tools 'cmake-*\bin') -Directory | Select-Object -First 1
}
$env:PATH = "$($cmakeBin.FullName);$gccBin;$env:PATH"

$config = if ($Debug) { 'Debug' } else { 'Release' }
$build  = Join-Path $root "build\$config"

cmake -S $root -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$config" "-DCMAKE_C_COMPILER=gcc" "-DCMAKE_CXX_COMPILER=g++"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build $build --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$exe = Join-Path $build 'CubeSlam.exe'
Write-Host "`nBuilt: $exe"
if ($Run) { Push-Location $build; try { & $exe } finally { Pop-Location } }
