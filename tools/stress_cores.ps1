# stress_cores.ps1 - bateria de carga/descarga repetida, core por core.
#
# Por que existe: um core que abre de primeira e recusa na segunda nao aparece
# em teste manual - so quando o jogador fecha e reabre. Foi assim com o MAME,
# que ficava mapeado na memoria e recusava o segundo retro_load_game, e com o
# Flycast, que dispara DEBUGBREAK na segunda inicializacao e mata o processo.
# Os dois passaram despercebidos por meses porque ninguem abre o mesmo jogo
# duas vezes seguidas enquanto testa.
#
# O que faz: usa o autoteste headless que o proprio app ja tem
# (--core-selftest), que carrega, desliga e carrega de novo sem abrir janela.
# Cada par (core, rom) e exercitado N vezes, e o resultado de cada rodada e
# classificado em PASS, FAIL (o core recusou) ou CRASH (o processo morreu, que
# e o caso grave - o app nao deveria morrer nunca).
#
# Uso:
#   powershell -NoProfile -File tools\stress_cores.ps1
#   ...\stress_cores.ps1 -Cycles 5
#   ...\stress_cores.ps1 -System Arcade          # so um sistema
#   ...\stress_cores.ps1 -TimeoutSec 90          # cores pesados (PS2, GC)

[CmdletBinding()]
param(
    [int]$Cycles = 3,
    [string]$System = "",
    [int]$TimeoutSec = 60
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$app  = Join-Path $root "app"
$exe  = Get-ChildItem (Join-Path $app "Karamelo_v*.exe") -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $exe) { throw "Executavel nao encontrado em $app - rode compile_port.bat antes." }

$log = Join-Path $app "karamelo.log"

# Duas baterias ao mesmo tempo disputam o mesmo executavel e o mesmo log, e o
# resultado das duas fica sem valor. Aconteceu: uma rodada acusou dois sistemas
# de derrubar o processo, e nenhum dos dois havia crashado.
#
# A trava e um arquivo com o PID, e nao uma varredura de linhas de comando: a
# varredura enxergava o proprio processo que lanca o script - quando ele e
# chamado por outro powershell, o texto "stress_cores" aparece na linha de
# comando do pai, e a bateria se recusava a rodar por causa de si mesma.
$lock = Join-Path $env:TEMP "karamelo_stress.lock"
if (Test-Path $lock) {
    $donoPid = (Get-Content $lock -ErrorAction SilentlyContinue | Select-Object -First 1)
    $vivo = $donoPid -and (Get-Process -Id $donoPid -ErrorAction SilentlyContinue)
    if ($vivo) {
        throw ("Ja existe uma bateria rodando (PID {0}). Espere ela terminar - duas ao mesmo tempo invalidam as duas." -f $donoPid)
    }
    Remove-Item $lock -Force -ErrorAction SilentlyContinue   # trava orfa de uma rodada interrompida
}
Set-Content -Path $lock -Value $PID -Encoding ascii

# Pasta em roms/ -> core que a atende. Espelha o roteamento do menu nos casos
# obvios; para Arcade entrega o primeiro da cadeia e deixa o proprio app
# decidir, que e justamente o caminho que se quer exercitar.
$map = [ordered]@{
    "Arcade"        = "cores/arcade_fbneo.dll"
    "NES"           = "cores/nes.dll"
    "SNES"          = "cores/snes.dll"
    "Genesis"       = "cores/genesis.dll"
    "MasterSystem"  = "cores/sms.dll"
    "MegaCD"        = "cores/genesis.dll"
    "32X"           = "cores/32x.dll"
    "GameBoy"       = "cores/gb.dll"
    "GBA"           = "cores/gba.dll"
    "NDS"           = "cores/nds.dll"
    "Nintendo64"    = "cores/n64_parallel.dll"
    "PlayStation"   = "cores/psx.dll"
    "Saturn"        = "cores/saturn.dll"
    "Dreamcast"     = "cores/dreamcast.dll"
    "TurboGrafx16"  = "cores/pce.dll"
    "NeoGeo"        = "cores/neogeo.dll"
    "MSX"           = "cores/msx.dll"
    "Amiga"         = "cores/amiga.dll"
    "C64"           = "cores/c64.dll"
    "ZXSpectrum"    = "cores/spectrum.dll"
    "Atari2600"     = "cores/atari2600.dll"
    "Atari5200"     = "cores/atari5200.dll"
    "Atari7800"     = "cores/atari7800.dll"
    "Lynx"          = "cores/lynx.dll"
    "Jaguar"        = "cores/jaguar.dll"
    "ColecoVision"  = "cores/coleco.dll"
    "WonderSwan"    = "cores/wswan.dll"
    "NGP"           = "cores/ngp.dll"
    "PCFX"          = "cores/pcfx.dll"
    "3DO"           = "cores/3do.dll"
    "PlayStation2"  = "cores/ps2.dll"
    "PSP"           = "cores/psp.dll"
    "GameCube"      = "cores/gamecube.dll"
    "3DS"           = "cores/3ds.dll"
    "DOS"           = "cores/dosbox_pure.dll"
    "MegaDrive"     = "cores/genesis.dll"
}

# O que NAO e ROM. A versao anterior usava lista branca de extensoes, e todo
# formato fora dela fazia o sistema ser pulado sem aviso - a bateria testava 19
# de 35 pastas e o relatorio dizia "tudo verde".
$naoRom = @(".txt",".md",".nfo",".jpg",".png",".sav",".srm",".state",".bak",".log",".ini",".cfg",".xml",".dat")

$targets = @()
$pulados = @()
foreach ($sys in $map.Keys) {
    if ($System -and $sys -ne $System) { continue }
    $dll = Join-Path $app $map[$sys]
    $romDir = Join-Path $app "roms\$sys"
    if (-not (Test-Path $dll))    { $pulados += "$sys (core ausente: $($map[$sys]))"; continue }
    if (-not (Test-Path $romDir)) { $pulados += "$sys (sem pasta roms/$sys)"; continue }
    $rom = Get-ChildItem $romDir -File -ErrorAction SilentlyContinue |
           Where-Object { $naoRom -notcontains $_.Extension.ToLower() -and $_.Extension -ne "" } |
           Sort-Object Name | Select-Object -First 1
    if (-not $rom) { $pulados += "$sys (pasta vazia)"; continue }
    $targets += [pscustomobject]@{ Sys = $sys; Dll = $map[$sys]; Rom = "roms/$sys/$($rom.Name)" }
}

if (-not $System) {
    foreach ($d in (Get-ChildItem (Join-Path $app "roms") -Directory -ErrorAction SilentlyContinue)) {
        if (-not $map.Contains($d.Name) -and (Get-ChildItem $d.FullName -File -ErrorAction SilentlyContinue)) {
            $pulados += "$($d.Name) (sistema fora do mapa deste script)"
        }
    }
}

if ($targets.Count -eq 0) { throw "Nenhum par (core, rom) encontrado. Confira app\roms\ e app\cores\." }

Write-Host ""
Write-Host ("Bateria: {0} sistemas x {1} ciclos, cada ciclo = carrega, desliga, carrega de novo" -f $targets.Count, $Cycles) -ForegroundColor Cyan
Write-Host ("Executavel: {0}" -f $exe.Name) -ForegroundColor DarkGray
Write-Host ("-" * 78)

$results = @()

foreach ($t in $targets) {
    $line = "  {0,-14}" -f $t.Sys
    Write-Host -NoNewline $line
    $marks = ""
    $worst = "PASS"

    for ($i = 1; $i -le $Cycles; $i++) {
        # O veredito vem do codigo de saida, nao do log. O autoteste devolve
        # 0 quando passa e 1 quando o core recusa; qualquer outro valor e o
        # codigo da excecao que matou o processo. Ler o log por posicao de byte
        # parecia equivalente e nao e: com duas baterias rodando ao mesmo tempo
        # os offsets se cruzaram e ciclos que passaram foram contados como
        # processo morto - dois sistemas foram acusados de crash sem nunca
        # terem crashado.
        $argLine = '--core-selftest "{0}" "{1}" "{0}" "{1}"' -f $t.Dll, $t.Rom
        $p = Start-Process -FilePath $exe.FullName -WorkingDirectory $app -PassThru -WindowStyle Hidden `
             -ArgumentList $argLine
        if (-not $p.WaitForExit($TimeoutSec * 1000)) {
            try { $p.Kill() } catch {}
            $marks += "T"; $worst = "TIMEOUT"; continue
        }

        switch ($p.ExitCode) {
            0 { $marks += "." }
            1 { $marks += "F"; if ($worst -eq "PASS") { $worst = "FAIL" } }
            default {
                $marks += "X"
                $worst  = "CRASH"
                $codes  = if ($codes) { $codes } else { @{} }
                $codes[$t.Sys] = ("0x{0:X8}" -f $p.ExitCode)
            }
        }
    }

    $color = switch ($worst) {
        "PASS"    { "Green" }
        "FAIL"    { "Yellow" }
        default   { "Red" }
    }
    Write-Host ("[{0}]  {1}" -f $marks, $worst) -ForegroundColor $color
    $results += [pscustomobject]@{ Sistema = $t.Sys; Marcas = $marks; Resultado = $worst; Rom = $t.Rom }
}

Write-Host ("-" * 78)
Write-Host "  . = ciclo ok    F = core recusou    X = processo morreu    T = travou" -ForegroundColor DarkGray
Write-Host ""

$crash = @($results | Where-Object { $_.Resultado -eq "CRASH" -or $_.Resultado -eq "TIMEOUT" })
$fail  = @($results | Where-Object { $_.Resultado -eq "FAIL" })

Write-Host ("  {0} sistemas ok, {1} com recusa, {2} derrubaram ou travaram o processo" -f
            ($results.Count - $fail.Count - $crash.Count), $fail.Count, $crash.Count)

if ($crash.Count) {
    Write-Host ""
    Write-Host "  O app morreu nestes - e o caso que nunca deveria acontecer:" -ForegroundColor Red
    $crash | ForEach-Object { Write-Host ("    {0,-14} {1}" -f $_.Sistema, $_.Rom) -ForegroundColor Red }
}
if ($fail.Count) {
    Write-Host ""
    Write-Host "  Recusaram carregar (pode ser romset/BIOS, nao necessariamente bug):" -ForegroundColor Yellow
    $fail | ForEach-Object { Write-Host ("    {0,-14} {1}" -f $_.Sistema, $_.Rom) -ForegroundColor Yellow }
}

Remove-Item $lock -Force -ErrorAction SilentlyContinue

if ($pulados.Count) {
    Write-Host ""
    Write-Host ("  NAO testados ({0}) - o verde acima nao fala por eles:" -f $pulados.Count) -ForegroundColor DarkYellow
    $pulados | ForEach-Object { Write-Host ("    " + $_) -ForegroundColor DarkGray }
}

Write-Host ""
if ($crash.Count) { exit 2 } elseif ($fail.Count) { exit 1 } else { exit 0 }
