# validate_ports.ps1 - instala de verdade os ports que mudaram no catalogo.
#
# Compara data/ports.json com a versao em -BaseRef e, para cada port novo ou
# alterado (e visivel, com repositorio), roda "Karamelo.exe --install-port <id>"
# a partir de app/. O instalador baixa a release fixada e recusa qualquer
# arquivo cujo SHA-256 nao seja o do catalogo - entao "instalou" aqui quer dizer
# "versao certa, arquivo certo, executavel encontrado".
#
# Usado pelo workflow .github/workflows/ports.yml; roda igual na maquina local:
#   powershell -File tools\validate_ports.ps1 -BaseRef origin/main
#
# Escreve um resumo em Markdown em -Report (para o texto do PR) e sai com 1 se
# algum port nao instalou.

param(
    [string]$BaseRef = "origin/main",
    [string]$Report = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$oldText = git show "${BaseRef}:data/ports.json" 2>$null
$old = @{}
if ($LASTEXITCODE -eq 0 -and $oldText) {
    foreach ($p in (($oldText -join "`n") | ConvertFrom-Json).ports) { $old[$p.id] = ($p | ConvertTo-Json -Depth 6 -Compress) }
}
$new = (Get-Content data\ports.json -Raw -Encoding UTF8 | ConvertFrom-Json).ports

$changed = @()
foreach ($p in $new) {
    if ($p.hidden -or -not $p.repo) { continue }
    $now = $p | ConvertTo-Json -Depth 6 -Compress
    if (-not $old.ContainsKey($p.id) -or $old[$p.id] -ne $now) { $changed += $p }
}

$lines = @("| Port | Versao | Resultado |", "|---|---|---|")
if (-not $changed.Count) {
    Write-Host "Nenhum port alterado em relacao a $BaseRef."
    if ($Report) { Set-Content -Path $Report -Value "Nenhum port alterado." -Encoding UTF8 }
    exit 0
}

$exe = Get-ChildItem app\Karamelo_v*.exe | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $exe) { throw "Executavel nao encontrado em app\ - rode compile_port.bat antes." }

$failed = 0
foreach ($p in $changed) {
    Remove-Item -Recurse -Force (Join-Path "app\ports" $p.id) -ErrorAction SilentlyContinue
    $proc = Start-Process -FilePath $exe.FullName -WorkingDirectory "app" -ArgumentList "--install-port", $p.id -Wait -PassThru -WindowStyle Hidden
    $log = Get-Content app\karamelo.log -Tail 40 -ErrorAction SilentlyContinue | Select-String "PORT-INSTALL\] $($p.id) " | Select-Object -Last 1
    $detail = if ($log) { ($log.Line -replace '^.*-> ', '') } else { "exit $($proc.ExitCode)" }
    $ok = $proc.ExitCode -eq 0
    if (-not $ok) { $failed++ }
    $mark = if ($ok) { "OK" } else { "FALHOU: $detail" }
    $tag = if ($p.release_tag) { $p.release_tag } else { "(ultima)" }
    Write-Host ("  {0,-26} {1,-16} {2}" -f $p.id, $tag, $mark)
    $lines += "| $($p.display_name) (`$($p.id)`) | $tag | $mark |"
    Remove-Item -Recurse -Force (Join-Path "app\ports" $p.id) -ErrorAction SilentlyContinue
}

if ($Report) { Set-Content -Path $Report -Value ($lines -join "`n") -Encoding UTF8 }
if ($failed) { Write-Host "$failed port(s) nao instalaram."; exit 1 }
exit 0
