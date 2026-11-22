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
    [string]$PackBuilder,                             # build_button_pack.py of the game project (default: the game repo's mission-l6	ools\mods copy)
    [switch]$Overlay,                                 # also ship ste_overlay.dll (the in-game overlay); done automatically when the game's capabilities.json lists "overlay"
    [string]$ToolsMods,                               # folder with iso_to_mod.py, bsf_edit.py, step_diff.py, pw_bundle.py, bio_ui.py, solo_ui.py, texlib.py (+ build_biollante.py, build_solo_destruction.py) (the game project's mod tools); shipped as tools\mods
    [string]$EmbeddedPython,                          # python-3.15.2-embed-amd64.zip (python.org); shipped as tools\python so recipe mods can be built without Python installed
    [string]$BundledMods,                             # folder holding mod folders that ship in the download (installed once, switched OFF)
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
    $ErrorActionPreference = 'Continue'   # VsDevCmd may print harmless messages on stderr; the exit code decides
    & (Join-Path $PSScriptRoot 'build.cmd') "-DLAUNCHER_VERSION_STRING=$Version" "-DLAUNCHER_CONSOLE=OFF"  # Windows-subsystem launcher: no console window
    $ErrorActionPreference = 'Stop'
    if ($LASTEXITCODE -ne 0) { Fail 'The build failed.' }
}
$launcherExe = Join-Path $BuildDir 'bin\launcher.exe'
if (-not (Test-Path $launcherExe)) { Fail "launcher.exe not found: $launcherExe" }

# ---- stage
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item $launcherExe $stage
Copy-Item $GameExe (Join-Path $stage 'ste-recomp.exe')
foreach ($c in $crtFiles) { Copy-Item (Join-Path $CrtDir $c) $stage }
# features the game reports (the launcher reads it to show or hide settings rows)
$capFile = Join-Path (Split-Path $GameExe -Parent) 'capabilities.json'
if (Test-Path $capFile) { Copy-Item $capFile (Join-Path $stage 'capabilities.json') } else { Write-Warning "No capabilities.json next to $GameExe" }
# The in-game overlay: ste_overlay.dll (plain C interface, include\ste_overlay_c.h). It needs msvcp140.dll / vcruntime140.dll / vcruntime140_1.dll next to it,
# which are already shipped for the launcher (the game program itself stays plain C and loads the DLL at run time).
$wantOverlay = $Overlay.IsPresent
if (Test-Path $capFile) { try { if ((Get-Content $capFile -Raw | ConvertFrom-Json).features -contains 'overlay') { $wantOverlay = $true } } catch { Write-Warning "capabilities.json is not valid JSON" } }
if ($wantOverlay) {
    $ovDll = Join-Path $BuildDir 'bin\ste_overlay.dll'
    if (-not (Test-Path $ovDll)) { Fail "ste_overlay.dll not found: $ovDll (the build makes it)" }
    Copy-Item $ovDll $stage
    Write-Host 'overlay  : ste_overlay.dll included'
} else { Write-Host 'overlay  : not included (the game does not list "overlay" in capabilities.json; use -Overlay to force)' }
# Button-prompt texture packs: built from our Kenney zip (CC0, pinned hash). The zip itself is not shipped.
if (-not $PackBuilder) { $PackBuilder = Join-Path $repo '..\ste-recomp\mission-l6\tools\mods\build_button_pack.py' }
$kenneyZip = Get-ChildItem (Join-Path $repo 'assets\glyphs') -Filter 'kenney_input-prompts*.zip' | Select-Object -First 1
if ((Test-Path $PackBuilder) -and $kenneyZip) {
    & python -I $PackBuilder --zip $kenneyZip.FullName --out (Join-Path $stage 'texturepacks')
    if ($LASTEXITCODE -ne 0) { Fail 'Building the button prompt packs failed.' }
} else { Write-Warning 'Button prompt packs NOT built (builder or Kenney zip missing): the game will not show other button styles.' }

foreach ($d in 'ui', 'assets', 'config') { robocopy (Join-Path $repo $d) (Join-Path $stage $d) /E /XF *.bak *.zip /NFL /NDL /NJH /NJS /NP | Out-Null }
robocopy $FsrDir (Join-Path $stage 'fsr1') /E /NFL /NDL /NJH /NJS /NP | Out-Null
foreach ($f in 'LICENSE', 'CREDITS.md') { Copy-Item (Join-Path $repo $f) $stage }
Copy-Item (Join-Path $repo 'packaging\THIRD_PARTY_NOTICES.md') $stage
Copy-Item (Join-Path $repo 'packaging\GAME_DATA_NOTICE.md') $stage
# Recipe mods are built on the player's PC by the game project's standard-library Python tools (tools\mods) with an embedded Python.
if ($ToolsMods) {
    foreach ($f in 'iso_to_mod.py', 'bsf_edit.py', 'step_diff.py', 'pw_bundle.py', 'bio_ui.py', 'solo_ui.py', 'texlib.py') { if (-not (Test-Path (Join-Path $ToolsMods $f))) { Fail "Mod tool missing: $f in $ToolsMods" } }
    $tm = Join-Path $stage 'tools\mods'; New-Item -ItemType Directory -Force $tm | Out-Null
    foreach ($f in 'iso_to_mod.py', 'bsf_edit.py', 'step_diff.py', 'pw_bundle.py', 'bio_ui.py', 'solo_ui.py', 'texlib.py', 'build_biollante.py', 'build_solo_destruction.py') { if (Test-Path (Join-Path $ToolsMods $f)) { Copy-Item (Join-Path $ToolsMods $f) $tm } }
    Write-Host 'mod tools: tools\mods included'
}
if ($EmbeddedPython) {
    $pyPin = 'D012F3AA29CF07436DAE8AEA1FEC0FD195C2612A0B059B1F915AE0AFB9AD1F62'   # python-3.15.2-embed-amd64.zip, python.org release page
    if ((Get-FileHash $EmbeddedPython -Algorithm SHA256).Hash -ne $pyPin) { Fail 'The embedded Python zip does not match the pinned SHA-256.' }
    Expand-Archive $EmbeddedPython (Join-Path $stage 'tools\python') -Force
    Write-Host 'python    : embedded Python 3.15.2 included (tools\python)'
}
if ($BundledMods) {
    foreach ($d in Get-ChildItem $BundledMods -Directory) {
        if (Test-Path (Join-Path $d.FullName '_built')) { Fail "Bundled mod $($d.Name) contains _built (game data): not shipped." }
        robocopy $d.FullName (Join-Path (Join-Path $stage 'bundled_mods') $d.Name) /E /XD _built /NFL /NDL /NJH /NJS /NP | Out-Null
    }
    Write-Host 'mods      : bundled_mods included'
}
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
$L.Add('data\, mods\, texturepacks\ except the two button_prompts_* packs, shadercache\, saves\, logs, *.log, *.err, *.out, *.dmp, screenshots, *.pdb, *.bak, other candidate exes, any default.xbe / game files / disc images, tools\ (except tools\mods and tools\python), docs\, tests\, .agent-state, .claude, play*.bat, xiso_unpack_tool.exe.')
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
