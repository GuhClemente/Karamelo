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
}

$romExt = @(".zip",".7z",".chd",".cue",".iso",".adf",".rom",".bin",".nes",".sfc",".smc",
            ".md",".gen",".gb",".gbc",".gba",".nds",".n64",".z64",".v64",".pce",".ws",
            ".wsc",".ngp",".ngc",".lnx",".a26",".a52",".a78",".col",".tap",".d64",".dsk",".msx",".cas")

$targets = @()
foreach ($sys in $map.Keys) {
    if ($System -and $sys -ne $System) { continue }
    $dll = Join-Path $app $map[$sys]
    if (-not (Test-Path $dll)) { continue }
    $romDir = Join-Path $app "roms\$sys"
    if (-not (Test-Path $romDir)) { continue }
    $rom = Get-ChildItem $romDir -File -ErrorAction SilentlyContinue |
           Where-Object { $romExt -contains $_.Extension.ToLower() } |
           Sort-Object Name | Select-Object -First 1
    if (-not $rom) { continue }
    $targets += [pscustomobject]@{ Sys = $sys; Dll = $map[$sys]; Rom = "roms/$sys/$($rom.Name)" }
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
        $before = if (Test-Path $log) { (Get-Item $log).Length } else { 0 }

        # Os quatro argumentos exercitam carregar, desligar e carregar de novo
        # dentro do mesmo processo - o cenario que quebrava.
        $p = Start-Process -FilePath $exe.FullName -WorkingDirectory $app -PassThru -WindowStyle Hidden `
             -ArgumentList "--core-selftest", $t.Dll, $t.Rom, $t.Dll, $t.Rom
        if (-not $p.WaitForExit($TimeoutSec * 1000)) {
            try { $p.Kill() } catch {}
            $marks += "T"; $worst = "TIMEOUT"; continue
        }

        $tail = ""
        if (Test-Path $log) {
            $fs = [System.IO.File]::Open($log, 'Open', 'Read', 'ReadWrite')
            try {
                $fs.Seek($before, 'Begin') | Out-Null
                $sr = New-Object System.IO.StreamReader($fs)
                $tail = $sr.ReadToEnd()
            } finally { $fs.Close() }
        }

        if ($tail -match "RESULT=PASS") {
            $marks += "."
        } elseif ($tail -match "RESULT=FAIL") {
            $marks += "F"
            if ($worst -eq "PASS") { $worst = "FAIL" }
        } else {
            # Sem linha de RESULT: o processo morreu antes de escrever.
            $marks += "X"
            $worst = "CRASH"
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

Write-Host ""
if ($crash.Count) { exit 2 } elseif ($fail.Count) { exit 1 } else { exit 0 }
