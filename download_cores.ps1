$cores = @{
    "gba.dll"         = "mgba_libretro.dll.zip"
    "gb.dll"          = "gambatte_libretro.dll.zip"
    "psp.dll"         = "ppsspp_libretro.dll.zip"
    "dosbox_pure.dll" = "dosbox_pure_libretro.dll.zip"
    "32x.dll"         = "picodrive_libretro.dll.zip"
    "jaguar.dll"      = "virtualjaguar_libretro.dll.zip"
    "atari5200.dll"   = "a5200_libretro.dll.zip"
    "coleco.dll"      = "gearcoleco_libretro.dll.zip"
    "lynx.dll"        = "handy_libretro.dll.zip"
    "ngp.dll"         = "mednafen_ngp_libretro.dll.zip"
    "wswan.dll"       = "mednafen_wswan_libretro.dll.zip"
    "pcfx.dll"        = "mednafen_pcfx_libretro.dll.zip"
    "3do.dll"         = "opera_libretro.dll.zip"
    "msx.dll"         = "fmsx_libretro.dll.zip"
    "amiga.dll"       = "puae_libretro.dll.zip"
    "c64.dll"         = "vice_x64_libretro.dll.zip"
    "spectrum.dll"    = "fuse_libretro.dll.zip"
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
        Invoke-WebRequest -Uri $url -OutFile $zipPath -UseBasicParsing -TimeoutSec 30
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
