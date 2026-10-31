# test_social.ps1 - chat, convite e voz pela rede de verdade.
#
# Por que existe: a primeira versao do chat passava em todos os testes e nao
# entregava mensagem nenhuma. O aviso na rede local saia pela VPN do PC em vez da
# placa de rede, e os testes chamavam a funcao de receber direto, sem rede. Aqui
# sao dois processos e sockets reais: mensagem e presenca nos dois sentidos,
# convite, aceite e a partida conectada.
#
# -RealMic usa o microfone e a caixa de som de verdade do PC, e nao entra no
# build porque depende de ter um microfone ligado. Sem a opcao, a voz roda no
# driver de audio falso do SDL, que testa todo o caminho menos o aparelho.
#
# Uso:
#   powershell -NoProfile -File tools\test_social.ps1
#   ...\test_social.ps1 -RealMic

[CmdletBinding()]
param(
    [switch]$RealMic,
    [int]$ChatPort = 55637
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$app = Join-Path $root "app"
$exe = Join-Path $app "Karamelo.exe"
if (-not (Test-Path $exe)) { Write-Host "Karamelo.exe nao encontrado"; exit 6 }

if (Get-NetTCPConnection -LocalPort 55435 -State Listen -ErrorAction SilentlyContinue) {
    Write-Host "  chat e convite: pulado, ha uma partida real aberta na porta 55435"
    $socialOk = $true
} else {
    $env:SDL_AUDIO_DRIVER = "dummy"
    $work = Join-Path $env:TEMP ("karamelo_social_" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force (Join-Path $work "a"), (Join-Path $work "b") | Out-Null
    $outA = Join-Path $work "a.txt"
    $outB = Join-Path $work "b.txt"
    # Pastas separadas: cada lado com a propria configuracao, como dois PCs. Cada um
    # escuta na propria porta, porque broadcast entre dois processos no mesmo PC
    # depende do driver; o resultado diz em quantas placas o broadcast foi aceito.
    $a = Start-Process -FilePath $exe -WorkingDirectory (Join-Path $work "a") -PassThru -WindowStyle Hidden -ArgumentList "--social-selftest a $ChatPort `"$outA`""
    $b = Start-Process -FilePath $exe -WorkingDirectory (Join-Path $work "b") -PassThru -WindowStyle Hidden -ArgumentList "--social-selftest b $ChatPort `"$outB`""
    $doneA = $a.WaitForExit(60000)
    $doneB = $b.WaitForExit(10000)
    if (-not $doneA) { Stop-Process -Id $a.Id -Force -ErrorAction SilentlyContinue }
    if (-not $doneB) { Stop-Process -Id $b.Id -Force -ErrorAction SilentlyContinue }
    $codeA = if ($doneA) { $a.ExitCode } else { "travou" }
    $codeB = if ($doneB) { $b.ExitCode } else { "travou" }
    $socialOk = ($codeA -eq 0) -and ($codeB -eq 0)
    Write-Host ("  chat e convite: " + $(if ($socialOk) { "PASSOU" } else { "FALHOU" }))
    Write-Host ("      a: codigo $codeA  " + $(if (Test-Path $outA) { (Get-Content $outA -Raw).Trim() } else { "(sem resultado)" }))
    Write-Host ("      b: codigo $codeB  " + $(if (Test-Path $outB) { (Get-Content $outB -Raw).Trim() } else { "(sem resultado)" }))
    Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
}

if ($RealMic) { Remove-Item Env:SDL_AUDIO_DRIVER -ErrorAction SilentlyContinue } else { $env:SDL_AUDIO_DRIVER = "dummy" }
$work = Join-Path $env:TEMP ("karamelo_voice_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force $work | Out-Null
$out = Join-Path $work "voice.txt"
$seconds = if ($RealMic) { 4 } else { 2 }
$v = Start-Process -FilePath $exe -WorkingDirectory $work -PassThru -WindowStyle Hidden -ArgumentList "--voice-selftest 55638 $seconds `"$out`""
$done = $v.WaitForExit(30000)
if (-not $done) { Stop-Process -Id $v.Id -Force -ErrorAction SilentlyContinue }
$code = if ($done) { $v.ExitCode } else { "travou" }
$voiceOk = $code -eq 0
$label = if ($RealMic) { "voz com microfone real" } else { "voz" }
Write-Host ("  ${label}: " + $(if ($voiceOk) { "PASSOU" } else { "FALHOU" }))
Write-Host ("      codigo $code  " + $(if (Test-Path $out) { (Get-Content $out -Raw).Trim() } else { "(sem resultado)" }))
Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue

exit $(if ($socialOk -and $voiceOk) { 0 } else { 1 })
