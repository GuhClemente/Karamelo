# Every core on the buildbot, each file keeping the emulator it already is: arcade_fbneo.dll
# is MAME (docs/MOTORES.md). n64_gopher.dll is not here, build_gopher64.bat builds it.
$cores = @{
    "32x.dll"            = "picodrive_libretro.dll.zip"
    "3do.dll"            = "opera_libretro.dll.zip"
    "3ds.dll"            = "citra_libretro.dll.zip"
    "amiga.dll"          = "puae_libretro.dll.zip"
    "arcade_fbneo.dll"   = "mame_libretro.dll.zip"
    "atari2600.dll"      = "stella_libretro.dll.zip"
    "atari5200.dll"      = "a5200_libretro.dll.zip"
    "atari7800.dll"      = "prosystem_libretro.dll.zip"
    "c64.dll"            = "vice_x64_libretro.dll.zip"
    "coleco.dll"         = "gearcoleco_libretro.dll.zip"
    "dosbox_pure.dll"    = "dosbox_pure_libretro.dll.zip"
    "dreamcast.dll"      = "flycast_libretro.dll.zip"
    "fmtowns.dll"        = "geartowns_libretro.dll.zip"
    "gamecube.dll"       = "dolphin_libretro.dll.zip"
    "gb.dll"             = "gambatte_libretro.dll.zip"
    "gba.dll"            = "mgba_libretro.dll.zip"
    "genesis.dll"        = "genesis_plus_gx_libretro.dll.zip"
    "jaguar.dll"         = "virtualjaguar_libretro.dll.zip"
    "lynx.dll"           = "handy_libretro.dll.zip"
    "mame2003.dll"       = "mame2003_libretro.dll.zip"
    "mame2010.dll"       = "mame2010_libretro.dll.zip"
    "msx.dll"            = "fmsx_libretro.dll.zip"
    "n64.dll"            = "parallel_n64_libretro.dll.zip"
    "n64_mupen.dll"      = "mupen64plus_next_libretro.dll.zip"
    "n64_parallel.dll"   = "parallel_n64_libretro.dll.zip"
    "nds.dll"            = "melonds_libretro.dll.zip"
    "neocd_alt.dll"      = "neocd_libretro.dll.zip"
    "neogeo.dll"         = "geolith_libretro.dll.zip"
    "nes.dll"            = "mesen_libretro.dll.zip"
    "ngp.dll"            = "mednafen_ngp_libretro.dll.zip"
    "pc98.dll"           = "np2kai_libretro.dll.zip"
    "pce.dll"            = "mednafen_pce_libretro.dll.zip"
    "pcfx.dll"           = "mednafen_pcfx_libretro.dll.zip"
    "ps2.dll"            = "pcsx2_libretro.dll.zip"
    "ps2_play.dll"       = "play_libretro.dll.zip"
    "psp.dll"            = "ppsspp_libretro.dll.zip"
    "psx.dll"            = "mednafen_psx_libretro.dll.zip"
    "saturn.dll"         = "mednafen_saturn_libretro.dll.zip"
    "sms.dll"            = "gearsystem_libretro.dll.zip"
    "snes.dll"           = "bsnes_libretro.dll.zip"
    "spectrum.dll"       = "fuse_libretro.dll.zip"
    "wswan.dll"          = "mednafen_wswan_libretro.dll.zip"
}

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$destDirs = @(
    (Join-Path $repoRoot "app\cores"),
    (Join-Path $repoRoot "cores")
)

foreach ($dir in $destDirs) {
    if (-not (Test-Path $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
}

$baseUrl = "https://buildbot.libretro.com/nightly/windows/x86_64/latest/"
$tempDir = Join-Path $repoRoot "temp_cores_dl"
if (-not (Test-Path $tempDir)) {
    New-Item -ItemType Directory -Path $tempDir -Force | Out-Null
}

[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

foreach ($targetName in $cores.Keys) {
    $zipName = $cores[$targetName]
    $url = $baseUrl + $zipName
    $zipPath = Join-Path $tempDir $zipName
    $extractPath = Join-Path $tempDir ("ext_" + [System.IO.Path]::GetFileNameWithoutExtension($zipName))

    Write-Host "Downloading $targetName from $zipName..."
    try {
        Invoke-WebRequest -Uri $url -OutFile $zipPath -UseBasicParsing -TimeoutSec 600
        if (Test-Path $zipPath) {
            Expand-Archive -Path $zipPath -DestinationPath $extractPath -Force
            $foundDll = Get-ChildItem -Path $extractPath -Filter "*.dll" -Recurse | Select-Object -First 1
            if ($foundDll) {
                foreach ($dir in $destDirs) {
                    $finalPath = Join-Path $dir $targetName
                    Copy-Item -Path $foundDll.FullName -Destination $finalPath -Force
                }
                Write-Host "  -> SUCCESS: Installed $targetName (" $foundDll.Length "bytes )"
            } else {
                Write-Host "  -> WARNING: No DLL found in $zipName"
            }
            Remove-Item -Path $zipPath -Force -ErrorAction SilentlyContinue
            Remove-Item -Path $extractPath -Recurse -Force -ErrorAction SilentlyContinue
        }
    } catch {
        Write-Host "  -> ERROR downloading $targetName : $_"
    }
}

Remove-Item -Path $tempDir -Recurse -Force -ErrorAction SilentlyContinue
Write-Host "=== All cores downloaded and installed! ==="
