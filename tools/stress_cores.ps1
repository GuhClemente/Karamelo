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
#   ...\stress_cores.ps1 -Driver 2               # tudo em Vulkan
#   ...\stress_cores.ps1 -Driver 3               # tudo em DirectX 11

[CmdletBinding()]
param(
    [int]$Cycles = 3,
    [string]$System = "",
    [int]$TimeoutSec = 60,
    # 0=Auto (padrao do app) 1=OpenGL 2=Vulkan 3=DirectX 11 4=Software.
    # Vazio deixa o autoteste no padrao dele. Existe porque a bateria testava
    # so o caminho Auto: nenhum core era exercitado em Vulkan ou DirectX 11,
    # que sao justamente onde moram as diferencas entre um core e outro.
    [ValidateRange(-1, 4)]
    [int]$Driver = -1
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
# Pasta em roms/ -> core que a atende. Mapeia todos os 41 DLLs e 35 sistemas.
$map = [ordered]@{
    "Arcade-FBNeo"  = @{ Dll = "cores/arcade_fbneo.dll"; Dir = "Arcade" }
    "Arcade-MAME03" = @{ Dll = "cores/mame2003.dll";     Dir = "Arcade" }
    "Arcade-MAME10" = @{ Dll = "cores/mame2010.dll";     Dir = "Arcade" }
    "NES"           = @{ Dll = "cores/nes.dll";          Dir = "NES" }
    "SNES"          = @{ Dll = "cores/snes.dll";         Dir = "SNES" }
    "Genesis"       = @{ Dll = "cores/genesis.dll";      Dir = "Genesis" }
    "MasterSystem"  = @{ Dll = "cores/sms.dll";          Dir = "MasterSystem" }
    "MegaCD"        = @{ Dll = "cores/genesis.dll";      Dir = "MegaCD" }
    "32X"           = @{ Dll = "cores/32x.dll";          Dir = "32X" }
    "GameBoy"       = @{ Dll = "cores/gb.dll";           Dir = "GameBoy" }
    "GBA"           = @{ Dll = "cores/gba.dll";          Dir = "GBA" }
    "NDS"           = @{ Dll = "cores/nds.dll";          Dir = "NDS" }
    "Nintendo64"    = @{ Dll = "cores/n64_gopher.dll";   Dir = "Nintendo64" }
    "N64-Parallel"  = @{ Dll = "cores/n64_parallel.dll"; Dir = "Nintendo64" }
    "N64-Mupen"     = @{ Dll = "cores/n64_mupen.dll";    Dir = "Nintendo64" }
    "N64-Generic"   = @{ Dll = "cores/n64.dll";          Dir = "Nintendo64" }
    "PlayStation"   = @{ Dll = "cores/psx.dll";          Dir = "PlayStation" }
    "Saturn"        = @{ Dll = "cores/saturn.dll";       Dir = "Saturn" }
    "Dreamcast"     = @{ Dll = "cores/dreamcast.dll";    Dir = "Dreamcast" }
    "TurboGrafx16"  = @{ Dll = "cores/pce.dll";          Dir = "TurboGrafx16" }
    "NeoGeo"        = @{ Dll = "cores/neogeo.dll";       Dir = "NeoGeo" }
    "NeoGeo-CD"     = @{ Dll = "cores/neocd_alt.dll";    Dir = "NeoGeo" }
    "MSX"           = @{ Dll = "cores/msx.dll";          Dir = "MSX" }
    "Amiga"         = @{ Dll = "cores/amiga.dll";        Dir = "Amiga" }
    "C64"           = @{ Dll = "cores/c64.dll";          Dir = "C64" }
    "ZXSpectrum"    = @{ Dll = "cores/spectrum.dll";     Dir = "ZXSpectrum" }
    "Atari2600"     = @{ Dll = "cores/atari2600.dll";    Dir = "Atari2600" }
    "Atari5200"     = @{ Dll = "cores/atari5200.dll";    Dir = "Atari5200";     TestRom = "atari5200.a52" }
    "Atari7800"     = @{ Dll = "cores/atari7800.dll";    Dir = "Atari7800" }
    "Lynx"          = @{ Dll = "cores/lynx.dll";         Dir = "Lynx";          TestRom = "lynx.lnx" }
    "Jaguar"        = @{ Dll = "cores/jaguar.dll";       Dir = "Jaguar" }
    "ColecoVision"  = @{ Dll = "cores/coleco.dll";       Dir = "ColecoVision" }
    "WonderSwan"    = @{ Dll = "cores/wswan.dll";        Dir = "WonderSwan";    TestRom = "wswan.ws" }
    "NGP"           = @{ Dll = "cores/ngp.dll";          Dir = "NGP";           TestRom = "ngp.ngp" }
    "PCFX"          = @{ Dll = "cores/pcfx.dll";         Dir = "PCFX";          TestRom = "pcfx.cue" }
    "3DO"           = @{ Dll = "cores/3do.dll";          Dir = "3DO";           TestRom = "3do.iso" }
    "PlayStation2"  = @{ Dll = "cores/ps2.dll";          Dir = "PlayStation2" }
    "PS2-Play"      = @{ Dll = "cores/ps2_play.dll";     Dir = "PlayStation2" }
    "PSP"           = @{ Dll = "cores/psp.dll";          Dir = "PSP" }
    "GameCube"      = @{ Dll = "cores/gamecube.dll";     Dir = "GameCube" }
    "3DS"           = @{ Dll = "cores/3ds.dll";          Dir = "3DS" }
    "DOS"           = @{ Dll = "cores/dosbox_pure.dll";  Dir = "DOS" }
}

# O que NAO e ROM.
$naoRom = @(".txt",".nfo",".jpg",".png",".sav",".srm",".state",".bak",".log",".ini",".cfg",".xml",".dat")

$targets = @()
$pulados = @()
foreach ($sys in $map.Keys) {
    if ($System -and $sys -ne $System) { continue }
    $cfg = $map[$sys]
    $dll = Join-Path $app $cfg.Dll
    $romDir = Join-Path $app "roms\$($cfg.Dir)"
    if (-not (Test-Path $dll)) { $pulados += "$sys (core ausente: $($cfg.Dll))"; continue }

    $romPath = $null
    if (Test-Path $romDir) {
        $rom = Get-ChildItem $romDir -File -ErrorAction SilentlyContinue |
               Where-Object { $naoRom -notcontains $_.Extension.ToLower() -and $_.Extension -ne "" } |
               Sort-Object Name | Select-Object -First 1
        if ($rom) {
            $romPath = "roms/$($cfg.Dir)/$($rom.Name)"
        }
    }
    if (-not $romPath -and $cfg.TestRom) {
        $testRomDir = Join-Path $root "test_roms"
        $synthFile = Join-Path $testRomDir $cfg.TestRom
        if (-not (Test-Path $synthFile)) {
            $genPy = Join-Path $root "tools\generate_test_roms.py"
            if (Test-Path $genPy) {
                python $genPy 2>$null
            }
        }
        if (Test-Path $synthFile) {
            $romPath = "../test_roms/$($cfg.TestRom)"
        }
    }
    if (-not $romPath) { $pulados += "$sys (sem ROM em roms/$($cfg.Dir))"; continue }
    $targets += [pscustomobject]@{ Sys = $sys; Dll = $cfg.Dll; Rom = $romPath }
}

if (-not $System) {
    $mappedDirs = @($map.Values | ForEach-Object { $_.Dir } | Select-Object -Unique)
    foreach ($d in (Get-ChildItem (Join-Path $app "roms") -Directory -ErrorAction SilentlyContinue)) {
        if ($mappedDirs -notcontains $d.Name -and (Get-ChildItem $d.FullName -File -ErrorAction SilentlyContinue)) {
            $pulados += "$($d.Name) (sistema fora do mapa deste script)"
        }
    }
}

if ($targets.Count -eq 0) {
    # Uma checkout limpa (sem cores nem ROMs, que nunca vao para o git) nao tem
    # o que testar - isso nao e um core falhando, entao nao reprova o build.
    Write-Host "  Nenhum par (core, rom) em app\cores\ + app\roms\ - bateria pulada." -ForegroundColor DarkYellow
    Remove-Item $lock -Force -ErrorAction SilentlyContinue
    exit 0
}

Write-Host ""
Write-Host ("Bateria: {0} sistemas x {1} ciclos, cada ciclo = carrega, desliga, carrega de novo" -f $targets.Count, $Cycles) -ForegroundColor Cyan
Write-Host ("Executavel: {0}" -f $exe.Name) -ForegroundColor DarkGray
if ($Driver -ge 0) {
    $nomes = @("Auto", "OpenGL", "Vulkan", "DirectX 11", "Software")
    Write-Host ("Driver de video forcado: {0}" -f $nomes[$Driver]) -ForegroundColor DarkGray
}
Write-Host ("-" * 78)

$results = @()

foreach ($t in $targets) {
    # Cada ciclo aparece assim que termina. Antes o nome do sistema era escrito
    # e o resultado so saia depois de todos os ciclos: um core que estoura o
    # timeout deixava a tela parada por minutos, sem sinal de vida, e parecia
    # travado quando estava trabalhando.
    Write-Host -NoNewline ("  {0,-14}[" -f $t.Sys)
    $marks = ""
    $worst = "PASS"
    $sw = [System.Diagnostics.Stopwatch]::StartNew()

    for ($i = 1; $i -le $Cycles; $i++) {
        # O veredito vem do codigo de saida, nao do log. O autoteste devolve
        # 0 quando passa e 1 quando o core recusa; qualquer outro valor e o
        # codigo da excecao que matou o processo. Ler o log por posicao de byte
        # parecia equivalente e nao e: com duas baterias rodando ao mesmo tempo
        # os offsets se cruzaram e ciclos que passaram foram contados como
        # processo morto - dois sistemas foram acusados de crash sem nunca
        # terem crashado.
        $argLine = '--core-selftest "{0}" "{1}" "{0}" "{1}"' -f $t.Dll, $t.Rom
        if ($Driver -ge 0) { $argLine += ' --driver ' + $Driver }
        $p = Start-Process -FilePath $exe.FullName -WorkingDirectory $app -PassThru -WindowStyle Hidden `
             -ArgumentList $argLine
        if (-not $p.WaitForExit($TimeoutSec * 1000)) {
            try { $p.Kill() } catch {}
            $marks += "T"; Write-Host -NoNewline "T" -ForegroundColor Red; $worst = "TIMEOUT"; continue
        }

        switch ($p.ExitCode) {
            0 { $marks += "."; Write-Host -NoNewline "." -ForegroundColor Green }
            1 { $marks += "F"; Write-Host -NoNewline "F" -ForegroundColor Yellow; if ($worst -eq "PASS") { $worst = "FAIL" } }
            default {
                $marks += "X"
                Write-Host -NoNewline "X" -ForegroundColor Red
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
    Write-Host ("]  {0}  ({1:N0}s)" -f $worst, $sw.Elapsed.TotalSeconds) -ForegroundColor $color
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
