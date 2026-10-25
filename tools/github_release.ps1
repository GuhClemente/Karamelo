# github_release.ps1 - publica no GitHub Releases os mesmos arquivos que o
# deploy_all.bat acabou de mandar para karamelo-emu.com.
#
# Nada e recompilado aqui: sobe o que esta em dist\ (o zip Win64, o
# Karamelo.exe e, quando existirem, os pacotes Linux e macOS), entao a release
# do GitHub e o site ficam com os mesmos arquivos, byte a byte.
#
# - Tag: v<versao>, criada no commit atual (HEAD). O commit precisa estar no
#   GitHub (git push) - senao a release apontaria para um codigo que ninguem ve.
# - Notas: a secao "## Versao <versao>" do docs/SITE_SYNC.md; sem ela, o campo
#   "notes" do dist\version.json.
# - Se a release ja existe (deploy rodado de novo, ou o pacote macOS chegando
#   depois), os arquivos sao substituidos e nada mais muda.
#
# Usa o "gh" (GitHub CLI) logado na maquina. Uso manual:
#   powershell -File tools\github_release.ps1 -Version 0.9.78
#   powershell -File tools\github_release.ps1 -Version 0.9.78 -DryRun

param(
    [Parameter(Mandatory = $true)][string]$Version,
    [switch]$DryRun
)

$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$tag = "v$Version"

if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
    Write-Host "  [AVISO] gh (GitHub CLI) nao encontrado - release do GitHub nao criada."
    Write-Host "          Instale em https://cli.github.com e rode: gh auth login"
    exit 1
}
gh auth status *> $null
if ($LASTEXITCODE -ne 0) {
    Write-Host "  [AVISO] gh nao esta logado - rode: gh auth login"
    exit 1
}

$head = (git rev-parse HEAD).Trim()
$remote = git branch -r --contains $head 2>$null | Where-Object { $_ -match 'origin/' }
if (-not $remote) {
    Write-Host "  [AVISO] o commit atual ($($head.Substring(0, 7))) nao esta no GitHub."
    Write-Host "          Rode git push e depois: powershell -File tools\github_release.ps1 -Version $Version"
    exit 1
}

# Karamelo_linux / Karamelo_mac nao tem versao no nome: so entram junto do
# pacote desta versao, senao um binario de uma build antiga iria na release.
$assets = @("dist\Karamelo_v${Version}_Win64.zip", "dist\Karamelo.exe") | Where-Object { Test-Path $_ }
foreach ($p in @(@("Linux64.tar.gz", "Karamelo_linux"), @("macOS_arm64.tar.gz", "Karamelo_mac"))) {
    $pkg = "dist\Karamelo_v${Version}_$($p[0])"
    if ((Test-Path $pkg) -and (Test-Path "dist\$($p[1])")) { $assets += $pkg, "dist\$($p[1])" }
}
$assets = @($assets)
if (-not ($assets -contains "dist\Karamelo_v${Version}_Win64.zip")) {
    Write-Host "  [AVISO] dist\Karamelo_v${Version}_Win64.zip nao existe - nada publicado no GitHub."
    exit 1
}

# Notas: a secao da versao no SITE_SYNC.md vai ate o proximo "---" ou "## ".
$notes = ""
$sync = Get-Content docs\SITE_SYNC.md -Encoding UTF8
$start = -1
for ($i = 0; $i -lt $sync.Count; $i++) {
    if ($sync[$i] -match "^## Vers.o $([regex]::Escape($Version))\b") { $start = $i + 1; break }
}
if ($start -ge 0) {
    $body = @()
    for ($i = $start; $i -lt $sync.Count; $i++) {
        if ($sync[$i] -match '^(---|## )') { break }
        $body += $sync[$i]
    }
    $notes = ($body -join "`n").Trim()
}
if (-not $notes -and (Test-Path dist\version.json)) {
    $notes = (Get-Content dist\version.json -Raw -Encoding UTF8 | ConvertFrom-Json).notes
}

# SHA-256 de cada arquivo no fim das notas: quem baixa pelo GitHub confere igual
# ao auto-update.
$hashes = @("", "SHA-256:", "")
foreach ($a in $assets) {
    $h = (Get-FileHash $a -Algorithm SHA256).Hash.ToLower()
    $hashes += "- ``$(Split-Path $a -Leaf)``: ``$h``"
}
$notesFile = Join-Path $env:TEMP "karamelo_release_notes_$Version.md"
[IO.File]::WriteAllText($notesFile, ($notes + "`n" + ($hashes -join "`n") + "`n"), (New-Object Text.UTF8Encoding $false))

Write-Host "  Release $tag no commit $($head.Substring(0, 7)):"
foreach ($a in $assets) { Write-Host ("    {0,-40} {1,8:N1} MB" -f (Split-Path $a -Leaf), ((Get-Item $a).Length / 1MB)) }
if ($DryRun) {
    Write-Host "  [DRY-RUN] notas:"
    Get-Content $notesFile -Encoding UTF8 | ForEach-Object { Write-Host "    $_" }
    exit 0
}

gh release view $tag *> $null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  Release $tag ja existe - substituindo os arquivos..."
    gh release upload $tag @assets --clobber
    if ($LASTEXITCODE -eq 0) { gh release edit $tag --notes-file $notesFile | Out-Null }
} else {
    gh release create $tag @assets --title "Karamelo $tag" --notes-file $notesFile --target $head
}
if ($LASTEXITCODE -ne 0) {
    Write-Host "  [AVISO] falha ao publicar a release $tag no GitHub."
    exit 1
}
Write-Host "  https://github.com/GuhClemente/Karamelo/releases/tag/$tag"
exit 0
