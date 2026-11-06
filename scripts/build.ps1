param([ValidateSet('Release','Debug')][string]$Configuration = 'Release', [string]$DependencyRoot = "$PSScriptRoot\..\.deps")
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot\..").Path
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC Desktop C++ workload and Windows 10 SDK required.' }
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path $cmake)) { throw 'Install the Visual Studio CMake component.' }
$qt = (Resolve-Path "$DependencyRoot\Qt\6.10.0\msvc2022_64").Path
$ffmpeg = (Resolve-Path "$DependencyRoot\ffmpeg\ffmpeg-master-latest-win64-gpl-shared").Path
$generator = if ($vs -match '\\18\\') { 'Visual Studio 18 2026' } else { 'Visual Studio 17 2022' }
& $cmake -S $root -B "$root\build" -G $generator -A x64 "-DCMAKE_PREFIX_PATH=$qt" "-DFFMPEG_ROOT=$ffmpeg"
if ($LASTEXITCODE) { throw 'CMake configure failed' }
& $cmake --build "$root\build" --config $Configuration --parallel
if ($LASTEXITCODE) { throw 'Compile failed' }
& "$qt\bin\windeployqt.exe" --release --no-translations --no-compiler-runtime "$root\build\bin\$Configuration\SippingCloftware.exe"
if ($LASTEXITCODE) { throw 'Qt deployment failed' }
& (Join-Path (Split-Path $cmake) 'ctest.exe') --test-dir "$root\build" -C $Configuration --output-on-failure
if ($LASTEXITCODE) { throw 'Tests failed' }
