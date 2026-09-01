# bump_version.ps1 - Gerenciador flexivel de versao do MiSTer 4 ALL
# Uso:
#   .\bump_version.ps1                   -> Exibe a versao atual
#   .\bump_version.ps1 0.9.0             -> Define a versao exata para 0.9.0
#   .\bump_version.ps1 set 0.9.0         -> Define a versao exata para 0.9.0
#   .\bump_version.ps1 patch             -> Incrementa patch (ex: 0.9.0 -> 0.9.1)
#   .\bump_version.ps1 minor             -> Incrementa minor (ex: 0.9.1 -> 0.10.0)
#   .\bump_version.ps1 major             -> Incrementa major (ex: 0.9.1 -> 1.0.0)

param (
    [string]$Action = "get",
    [string]$TargetVersion = ""
)

$appInfoPath = "$PSScriptRoot\include\app_info.h"
$content = Get-Content $appInfoPath -Raw

if ($content -match '#define\s+APP_VERSION\s+"([^"]+)"') {
    $currentVer = $matches[1]
    
    # Se o primeiro parametro for um numero de versao direto (ex: 0.9.0)
    if ($Action -match '^\d+(\.\d+)*$') {
        $TargetVersion = $Action
        $Action = "set"
    }

    if ($Action -eq "get") {
        Write-Host "[VERSAO ATUAL] $currentVer"
        Write-Output $currentVer
        exit 0
    }

    $newVer = $currentVer

    if ($Action -eq "set" -and $TargetVersion -ne "") {
        $newVer = $TargetVersion
    }
    elseif ($Action -eq "patch" -or $Action -eq "minor" -or $Action -eq "major") {
        $parts = $currentVer.Split('.')
        $major = if ($parts.Length -ge 1) { [int]$parts[0] } else { 0 }
        $minor = if ($parts.Length -ge 2) { [int]$parts[1] } else { 9 }
        $patch = if ($parts.Length -ge 3) { [int]$parts[2] } else { 0 }

        if ($Action -eq "major") {
            $major++
            $minor = 0
            $patch = 0
        } elseif ($Action -eq "minor") {
            $minor++
            $patch = 0
        } else {
            $patch++
        }
        $newVer = "$major.$minor.$patch"
    }

    $newExeName = "MiSTer_4_ALL_v$newVer.exe"

    $content = $content -replace '#define\s+APP_VERSION\s+"[^"]+"', "#define APP_VERSION     `"$newVer`""
    $content = $content -replace '#define\s+APP_EXE_NAME\s+"[^"]+"', "#define APP_EXE_NAME    `"$newExeName`""

    Set-Content -Path $appInfoPath -Value $content -NoNewline -Encoding UTF8
    Write-Host "[VERSION ATUALIZADA] $currentVer -> $newVer"
    Write-Output $newVer
} else {
    Write-Error "APP_VERSION nao encontrada em $appInfoPath"
    exit 1
}
