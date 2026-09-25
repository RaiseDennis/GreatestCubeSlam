# Downloads a portable, self-contained toolchain into .tools/ (no admin, no system changes):
#   - CMake
#   - WinLibs MinGW-w64 GCC (UCRT, includes ninja + mingw32-make)
# SFML itself is fetched and built from source by CMake (see CMakeLists.txt).
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$root  = $PSScriptRoot
$tools = Join-Path $root '.tools'
New-Item -ItemType Directory -Force $tools | Out-Null

$cmakeVer = '3.31.8'
$cmakeUrl = "https://github.com/Kitware/CMake/releases/download/v$cmakeVer/cmake-$cmakeVer-windows-x86_64.zip"
$gccUrl   = 'https://github.com/brechtsanders/winlibs_mingw/releases/download/16.2.0posix-14.0.0-ucrt-r1/winlibs-x86_64-posix-seh-gcc-16.2.0-mingw-w64ucrt-14.0.0-r1.zip'

function Get-Tool($name, $url, $check) {
    if (Test-Path (Join-Path $tools $check)) { Write-Host "[ok] $name already installed"; return }
    $zip = Join-Path $tools "$name.zip"
    Write-Host "[..] downloading $name"
    Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
    Write-Host "[..] extracting $name"
    Expand-Archive -Path $zip -DestinationPath $tools -Force
    Remove-Item $zip
    Write-Host "[ok] $name"
}

Get-Tool 'cmake' $cmakeUrl "cmake-$cmakeVer-windows-x86_64\bin\cmake.exe"
Get-Tool 'mingw' $gccUrl   'mingw64\bin\g++.exe'

Write-Host "`nToolchain ready. Run .\build.ps1 to build the game."
