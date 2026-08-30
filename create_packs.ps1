param()

if (-not (Test-Path "dist")) {
    New-Item -ItemType Directory -Path "dist" | Out-Null
}

Write-Host "==================================================="
Write-Host "1. Criando Pack de BIOS (dist\MiSTer_4_ALL_Pack_BIOS.zip)..."
$biosZip = "dist\MiSTer_4_ALL_Pack_BIOS.zip"
if (Test-Path $biosZip) {
    Remove-Item $biosZip -Force
}
Compress-Archive -Path "app\bios" -DestinationPath $biosZip -Force
$bInfo = Get-Item $biosZip
$bSizeMB = [math]::Round($bInfo.Length / 1MB, 2)
Write-Host "   -> Pack BIOS criado: $bSizeMB MB"

Write-Host "==================================================="
Write-Host "2. Criando Pack de ROMs Pequenos (< 25MB)..."
$tempRomsDir = "dist\temp_roms_pequenos\roms"
if (Test-Path "dist\temp_roms_pequenos") {
    Remove-Item "dist\temp_roms_pequenos" -Recurse -Force
}
New-Item -ItemType Directory -Path $tempRomsDir -Force | Out-Null

$romFiles = Get-ChildItem -Path "app\roms" -Recurse -File | Where-Object { $_.Length -le 25MB }
foreach ($rf in $romFiles) {
    $parentName = $rf.Directory.Name
    $destFolder = Join-Path $tempRomsDir $parentName
    if (-not (Test-Path $destFolder)) {
        New-Item -ItemType Directory -Path $destFolder -Force | Out-Null
    }
    Copy-Item -Path $rf.FullName -Destination $destFolder -Force
}

$romsZip = "dist\MiSTer_4_ALL_Pack_ROMs_Pequenos.zip"
if (Test-Path $romsZip) {
    Remove-Item $romsZip -Force
}
Compress-Archive -Path "dist\temp_roms_pequenos\roms" -DestinationPath $romsZip -Force
Remove-Item "dist\temp_roms_pequenos" -Recurse -Force

$rInfo = Get-Item $romsZip
$rSizeMB = [math]::Round($rInfo.Length / 1MB, 2)
Write-Host "   -> Pack ROMs Pequenos criado: $rSizeMB MB"
Write-Host "==================================================="
Write-Host "Concluido com sucesso!"
