<#
.SYNOPSIS
  Builds a distribution zip, manifest and changelog for one launcher version (Windows, portable layout).

.EXAMPLE
  powershell -File scripts\package.ps1 -Version 0.1.3 -GameExe C:\path\to\ste-recomp.exe -FsrDir C:\path\to\fsr1 -Out C:\releases

.DESCRIPTION
  1. builds launcher.exe (Release) with the version string, unless -NoBuild
  2. stages  <Out>\<Name>-v<Version>-alpha\  with launcher.exe, the game exe, the VC runtime DLLs, ui\ assets\ config\ (from the
     repository, not from build\bin), fsr1\, LICENSE, CREDITS.md, THIRD_PARTY_NOTICES.md, GAME_DATA_NOTICE.md, README.txt, portable.txt
  3. writes <Out>\<Name>-v<Version>-alpha-win64.zip (forward-slash entries), manifest-v<Version>.txt (SHA-256 of every file,
     status against the previous release folder) and CHANGES-v<Version>.txt (header + packaging\changes\<Version>.txt)

  Refuses to continue when: the game exe is older than compat.vsync_vrr_fix_build in config\profile.json, a shipped file
  is not covered by CREDITS.md (fonts), or personal text (user names, local paths) is found in the shipped text files.
  Nothing outside <Out> and the build folder is written. Existing outputs for the same version are not overwritten.
#>
param(
    [Parameter(Mandatory)][string]$Version,
    [Parameter(Mandatory)][string]$GameExe,
    [string]$FsrDir,                                  # folder with the game's fsr1 shaders (default: fsr1 next to the game exe)
    [string]$Out,                                     # default: <repo>\release-out
    [string]$Name = 'ReplayRevival-STE',
    [string]$Previous,                                # earlier release folder, for the [OK]/[CHANGED]/[NEW] column
    [string]$CrtDir,                                  # folder with msvcp140.dll, vcruntime140.dll, vcruntime140_1.dll (default: newest in Visual Studio's Redist)
    [string]$BuildDir,                                # default: <repo>\build
    [switch]$NoBuild
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'lib_publish.ps1')
if (-not $Out) { $Out = Join-Path $repo 'release-out' }
if (-not $BuildDir) { $BuildDir = Join-Path $repo 'build' }
$folderName = "$Name-v$Version-alpha"
$stage = Join-Path $Out $folderName
$zipPath = Join-Path $Out "$folderName-win64.zip"
$manifestPath = Join-Path $Out "manifest-v$Version.txt"
$changesPath = Join-Path $Out "CHANGES-v$Version.txt"
$changesSrc = Join-Path $repo "packaging\changes\$Version.txt"

function Fail($m) { Write-Error $m; exit 1 }

# ---- checks before anything is written
if (-not (Test-Path $GameExe)) { Fail "Game exe not found: $GameExe" }
if (-not (Test-Path $changesSrc)) { Fail "Write the user-facing changes first: $changesSrc" }
foreach ($p in $stage, $zipPath, $manifestPath, $changesPath) { if (Test-Path $p) { Fail "Already exists (move it away first): $p" } }
$gameVer = (Get-Item $GameExe).VersionInfo.ProductVersion          # e.g. 0.1.0+c260
if ($gameVer -notmatch '\+c(\d+)') { Fail "The game exe has no build tag in its version ($gameVer)." }
$gameBuild = [int]$Matches[1]
$profile = Get-Content (Join-Path $repo 'config\profile.json') -Raw | ConvertFrom-Json
$fixBuild = [int]$profile.compat.vsync_vrr_fix_build
if ($gameBuild -lt $fixBuild) { Fail "Game build c$gameBuild is older than compat.vsync_vrr_fix_build ($fixBuild) in config\profile.json." }
if (-not $FsrDir) { $FsrDir = Join-Path (Split-Path $GameExe -Parent) 'fsr1' }
if (-not (Test-Path (Join-Path $FsrDir 'ffx_fsr1.h'))) { Fail "fsr1 folder not found (use -FsrDir): $FsrDir" }
if (-not $CrtDir) {
    $vsw = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vs = & $vsw -latest -products * -property installationPath
    $CrtDir = Get-ChildItem (Join-Path $vs 'VC\Redist\MSVC') -Directory | Sort-Object Name -Descending |
        ForEach-Object { Get-ChildItem (Join-Path $_.FullName 'x64') -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue } |
        Select-Object -First 1 -ExpandProperty FullName
}
$crtFiles = 'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll'
foreach ($c in $crtFiles) { if (-not (Test-Path (Join-Path $CrtDir $c))) { Fail "Missing $c in $CrtDir (use -CrtDir)." } }
if (-not $Previous) {
    $Previous = Get-ChildItem $Out -Directory -Filter "$Name-v*-alpha" -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName
}

# ---- build
if (-not $NoBuild) {
    $env:BUILD_DIR = $BuildDir
    & (Join-Path $PSScriptRoot 'build.cmd') "-DLAUNCHER_VERSION_STRING=$Version" "-DLAUNCHER_CONSOLE=OFF"  # Windows-subsystem launcher: no console window
    if ($LASTEXITCODE -ne 0) { Fail 'The build failed.' }
}
$launcherExe = Join-Path $BuildDir 'bin\launcher.exe'
if (-not (Test-Path $launcherExe)) { Fail "launcher.exe not found: $launcherExe" }

# ---- stage
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item $launcherExe $stage
Copy-Item $GameExe (Join-Path $stage 'ste-recomp.exe')
foreach ($c in $crtFiles) { Copy-Item (Join-Path $CrtDir $c) $stage }
foreach ($d in 'ui', 'assets', 'config') { robocopy (Join-Path $repo $d) (Join-Path $stage $d) /E /XF *.bak *.zip /NFL /NDL /NJH /NJS /NP | Out-Null }
robocopy $FsrDir (Join-Path $stage 'fsr1') /E /NFL /NDL /NJH /NJS /NP | Out-Null
foreach ($f in 'LICENSE', 'CREDITS.md') { Copy-Item (Join-Path $repo $f) $stage }
Copy-Item (Join-Path $repo 'packaging\THIRD_PARTY_NOTICES.md') $stage
Copy-Item (Join-Path $repo 'packaging\GAME_DATA_NOTICE.md') $stage
(Get-Content (Join-Path $repo 'packaging\README.txt.in') -Raw -Encoding utf8).Replace('@VERSION@', $Version) |
    Set-Content (Join-Path $stage 'README.txt') -Encoding utf8 -NoNewline
New-Item -ItemType File (Join-Path $stage 'portable.txt') | Out-Null

# ---- checks on what is shipped
$stage = (Get-Item $stage).FullName   # long form of the path (8.3 short names would break the relative paths below)
$files = Get-ChildItem $stage -Recurse -File | Sort-Object FullName
$credits = Get-Content (Join-Path $stage 'CREDITS.md') -Raw
foreach ($ttf in $files | Where-Object { $_.Extension -eq '.ttf' }) {
    if (-not $credits.Contains($ttf.Name)) { Fail "Font $($ttf.Name) is shipped but not listed in CREDITS.md (shipped font files)." }
}
foreach ($bad in $files | Where-Object { $_.Extension -in '.xbe', '.iso', '.pdb', '.dmp', '.bak', '.log' }) { Fail "Must not be shipped: $($bad.Name)" }
$personal = Find-PersonalText $stage
if ($personal) { $personal | ForEach-Object { Write-Warning ("personal text in " + $_.Path) }; Fail 'Personal text found in the package.' }

# ---- zip (forward-slash entries, one root folder)
Add-Type -AssemblyName System.IO.Compression
$fs = [IO.File]::Create($zipPath)
$zip = New-Object IO.Compression.ZipArchive($fs, 'Create')
foreach ($f in $files) {
    $rel = $f.FullName.Substring($stage.Length + 1).Replace('\', '/')
    $e = $zip.CreateEntry("$folderName/$rel", 'Optimal')
    $s = $e.Open(); $i = [IO.File]::OpenRead($f.FullName); $i.CopyTo($s); $i.Dispose(); $s.Dispose()
}
$zip.Dispose(); $fs.Dispose()
$zipHash = (Get-FileHash $zipPath).Hash
$zipSize = (Get-Item $zipPath).Length

# ---- manifest
$today = Get-Date -Format 'yyyy-MM-dd'
$L = New-Object System.Collections.Generic.List[string]
$L.Add("RELEASE MANIFEST v$Version - Godzilla: Save the Earth recomp, early alpha (a Replay Revival recomp)")
$L.Add("Written $today. Packaged as $folderName-win64.zip. Game exe: ste-recomp.exe = build c$gameBuild, version $gameVer, taken from: $(Split-Path $GameExe -Leaf).")
$L.Add("Columns: [status vs $(if ($Previous) { Split-Path $Previous -Leaf } else { 'no previous release' })] | path inside the release folder | bytes | SHA-256")
$L.Add('')
$L.Add('=== FILES SHIPPED ===')
$total = 0
foreach ($f in $files) {
    $rel = $f.FullName.Substring($stage.Length + 1)
    $h = (Get-FileHash $f.FullName).Hash
    $status = 'NEW'
    if ($Previous -and (Test-Path (Join-Path $Previous $rel))) { $status = if ((Get-FileHash (Join-Path $Previous $rel)).Hash -eq $h) { 'OK' } else { 'CHANGED' } }
    $L.Add("[$status] | $rel | $($f.Length) | $h")
    $total += $f.Length
}
$L.Add('')
$L.Add("Total: $($files.Count) files, $([math]::Round($total / 1MB, 1)) MB uncompressed.")
$L.Add("Zip: $folderName-win64.zip | $zipSize bytes | SHA-256 $zipHash")
$L.Add('')
$L.Add('=== EXPLICIT EXCLUDES (never in the zip) ===')
$L.Add('data\, mods\, texturepacks\, shadercache\, saves\, logs, *.log, *.err, *.out, *.dmp, screenshots, *.pdb, *.bak, other candidate exes, any default.xbe / game files / disc images, tools\, docs\, tests\, .agent-state, .claude, play*.bat, xiso_unpack_tool.exe.')
$L.Add('')
$L.Add('=== NOTES ===')
$L.Add("profile.json: compat.vsync_vrr_fix_build = $fixBuild (game build c$gameBuild is at or above it); input_map.supported = $($profile.launch.input_map.supported).")
$L.Add("Visual C++ runtime DLLs from: $(Split-Path $CrtDir -Leaf) ($((Get-Item (Join-Path $stage 'msvcp140.dll')).VersionInfo.FileVersion)).")
$L.Add('Checked by the script: fonts are credited in CREDITS.md, no game files or debug files, no personal paths in text files.')
Set-Content $manifestPath $L -Encoding utf8

# ---- changes
$hdr = "Replay Revival - Godzilla: Save the Earth recomp`r`nCHANGES in v$Version. The game program is ste-recomp.exe $gameVer.`r`n`r`n"
Set-Content $changesPath ($hdr + (Get-Content $changesSrc -Raw -Encoding utf8)) -Encoding utf8 -NoNewline

Write-Host "zip      : $zipPath"
Write-Host "size     : $zipSize bytes"
Write-Host "sha256   : $zipHash"
Write-Host "manifest : $manifestPath"
Write-Host "changes  : $changesPath"
Write-Host 'Next: unzip into an empty folder, start it with a real game folder, play ~30 s, check data\ saves\ shadercache\ only, no [CRASH].'
