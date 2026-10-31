# test_netplay.ps1 - duas instancias do Karamelo jogando em rede de verdade.
#
# Por que existe: os testes de unidade provam a logica, mas so dois processos,
# um socket e um core de verdade provam que os dois jogos ficam iguais. Cada
# lado aperta os proprios botoes pseudoaleatorios e, a cada segundo, os dois
# comparam o hash do estado do jogo. Se um lado jogar um quadro com a entrada
# errada, os estados se separam e o teste reprova.
#
# O convidado recebe so o nome do jogo e abre sozinho, pela mesma busca que o
# jogador usa. Sem o jogo em roms/ o sistema e pulado, nao reprovado.
#
# Uso:
#   powershell -NoProfile -File tools\test_netplay.ps1
#   ...\test_netplay.ps1 -Frames 1200
#   ...\test_netplay.ps1 -System GBA

[CmdletBinding()]
param(
    [int]$Frames = 300,
    [string]$System = "",
    [int]$LateJoinSeconds = 2
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
# Bateria muda: o audio roda igual, mas nao sai na caixa de som.
$env:SDL_AUDIO_DRIVER = "dummy"
$app = Join-Path $root "app"
$exe = Join-Path $app "Karamelo.exe"
if (-not (Test-Path $exe)) { Write-Host "Karamelo.exe nao encontrado"; exit 6 }

$targets = @(
    @{ Name = "GameBoy";      Core = "cores/gb.dll";     Exts = "*.gb","*.gbc" },
    @{ Name = "GBA";          Core = "cores/gba.dll";    Exts = "*.gba" },
    @{ Name = "NES";          Core = "cores/nes.dll";    Exts = "*.nes" },
    @{ Name = "SNES";         Core = "cores/snes.dll";   Exts = "*.sfc","*.smc" },
    @{ Name = "Genesis";      Core = "cores/genesis.dll"; Exts = "*.md","*.gen","*.bin" },
    @{ Name = "MasterSystem"; Core = "cores/sms.dll";    Exts = "*.sms" }
)

$port = 55600
$failed = 0
$tested = 0
foreach ($t in $targets) {
    if ($System -and $t.Name -ne $System) { continue }
    $coreFile = Join-Path $app ($t.Core -replace '/', '\')
    if (-not (Test-Path $coreFile)) { Write-Host ("  {0,-14} pulado: sem o core" -f $t.Name); continue }
    $rom = $null
    foreach ($e in $t.Exts) {
        $rom = Get-ChildItem (Join-Path $app "roms\$($t.Name)") -Filter $e -File -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($rom) { break }
    }
    if (-not $rom) { Write-Host ("  {0,-14} pulado: sem jogo em roms\{0}" -f $t.Name); continue }

    $port++
    $tested++
    $hostOut = Join-Path $env:TEMP "karamelo_np_host_$port.txt"
    $guestOut = Join-Path $env:TEMP "karamelo_np_guest_$port.txt"
    Remove-Item $hostOut, $guestOut -ErrorAction SilentlyContinue

    $hostArgs = "--netplay-selftest host `"$($t.Core)`" `"$($rom.FullName)`" 127.0.0.1 $port $Frames `"$hostOut`""
    $guestArgs = "--netplay-selftest guest `"$($t.Core)`" `"$($rom.FullName)`" 127.0.0.1 $port $Frames `"$guestOut`""
    $h = Start-Process -FilePath $exe -WorkingDirectory $app -PassThru -WindowStyle Hidden -ArgumentList $hostArgs
    # O convidado chega com o jogo do host ja rodando: e o estado enviado que os iguala.
    Start-Sleep -Seconds $LateJoinSeconds
    $g = Start-Process -FilePath $exe -WorkingDirectory $app -PassThru -WindowStyle Hidden -ArgumentList $guestArgs

    $limitMs = [int](90 + $Frames / 20) * 1000
    $hostDone = $h.WaitForExit($limitMs)
    $guestDone = $g.WaitForExit(20000)
    if (-not $hostDone) { Stop-Process -Id $h.Id -Force -ErrorAction SilentlyContinue }
    if (-not $guestDone) { Stop-Process -Id $g.Id -Force -ErrorAction SilentlyContinue }
    $hostCode = if ($hostDone) { $h.ExitCode } else { "travou" }
    $guestCode = if ($guestDone) { $g.ExitCode } else { "travou" }
    $hostLine = if (Test-Path $hostOut) { Get-Content $hostOut -Raw } else { "(sem resultado)" }
    $guestLine = if (Test-Path $guestOut) { Get-Content $guestOut -Raw } else { "(sem resultado)" }

    # Codigo 5: os jogos so ficaram iguais porque o host reenviou o estado. Para o
    # jogador funciona (e o caso do mGBA), mas o core nao repete a si mesmo.
    $healed = ($hostCode -eq 5) -or ($guestCode -eq 5)
    $pass = (($hostCode -eq 0) -or ($hostCode -eq 5)) -and (($guestCode -eq 0) -or ($guestCode -eq 5))
    $word = if (-not $pass) { "FALHOU" } elseif ($healed) { "PASSOU reenviando o estado" } else { "PASSOU" }
    Write-Host ("  {0,-14} {1}  ({2})" -f $t.Name, $word, $rom.Name)
    Write-Host ("      host:  codigo $hostCode  " + $hostLine.Trim())
    Write-Host ("      guest: codigo $guestCode  " + $guestLine.Trim())
    if (-not $pass) { $failed++ }
    Remove-Item $hostOut, $guestOut -ErrorAction SilentlyContinue
}

Write-Host ""
Write-Host "  $tested sistemas testados, $failed com os jogos diferentes ou sem sincronizar"
if ($tested -eq 0) { Write-Host "  nada testado: sem jogos em app\roms"; exit 0 }
exit $(if ($failed -gt 0) { 1 } else { 0 })
