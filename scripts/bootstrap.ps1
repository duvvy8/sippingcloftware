param([string]$CacheRoot)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot\..").Path
$deps = Join-Path $root '.deps'
New-Item -ItemType Directory -Force $deps | Out-Null
$lock = Get-Content -Raw "$root\dependencies.lock.json" | ConvertFrom-Json
if ($CacheRoot) {
  $cache = (Resolve-Path -LiteralPath $CacheRoot).Path
  foreach ($name in @('Qt','ffmpeg')) {
    if (-not (Test-Path "$deps\$name")) { Copy-Item -LiteralPath "$cache\$name" -Destination "$deps\$name" -Recurse }
  }
}
if (-not (Test-Path "$deps\Qt\6.10.0\msvc2022_64\lib\cmake\Qt6\Qt6Config.cmake")) {
  # Python is development-only; the installed application is entirely native.
  & py -m venv "$deps\tools"
  if ($LASTEXITCODE) { throw 'Python development tooling required for Qt bootstrap (or supply -CacheRoot).' }
  & "$deps\tools\Scripts\python.exe" -m pip install aqtinstall==3.3.0
  if ($LASTEXITCODE) { throw 'aqt installation failed' }
  & "$deps\tools\Scripts\python.exe" -m aqt install-qt windows desktop 6.10.0 win64_msvc2022_64 -m qtmultimedia -O "$deps\Qt"
  if ($LASTEXITCODE) { throw 'Qt download failed' }
}
if (-not (Test-Path "$deps\ffmpeg\$($lock.ffmpeg.folder)\include\libavcodec\avcodec.h")) {
  $archive = "$deps\ffmpeg.zip"
  Invoke-WebRequest -Uri $lock.ffmpeg.url -Headers @{Accept='application/octet-stream';'User-Agent'='native-clipper-build'} -OutFile $archive
  if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $lock.ffmpeg.sha256) { throw 'FFmpeg archive hash mismatch; refusing changed dependency.' }
  Expand-Archive -LiteralPath $archive -DestinationPath "$deps\ffmpeg" -Force
  $expanded = [IO.Path]::GetFullPath("$deps\ffmpeg\$($lock.ffmpeg.archiveRoot)")
  $expected = [IO.Path]::GetFullPath("$deps\ffmpeg\$($lock.ffmpeg.folder)")
  $boundary = [IO.Path]::GetFullPath("$deps\ffmpeg") + '\'
  if (-not $expanded.StartsWith($boundary,[StringComparison]::OrdinalIgnoreCase) -or -not $expected.StartsWith($boundary,[StringComparison]::OrdinalIgnoreCase)) { throw 'Dependency path escaped the intended cache directory.' }
  if ($expanded -ne $expected) { Move-Item -LiteralPath $expanded -Destination $expected }
}
foreach ($f in @(@('InterVariable.ttf',$lock.inter.sha256),@('GeistMono-Regular.ttf',$lock.geistMono.sha256))) {
  if ((Get-FileHash -LiteralPath "$root\assets\fonts\$($f[0])" -Algorithm SHA256).Hash.ToLowerInvariant() -ne $f[1]) { throw "Font hash mismatch: $($f[0])" }
}
Write-Host 'Dependencies ready. Run scripts/build.ps1.'
