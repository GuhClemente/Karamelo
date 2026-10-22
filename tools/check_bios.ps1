# check_bios.ps1 - confere os arquivos de BIOS em app/bios/ contra os MD5 que
# o packaging/bios-guide/BIOS_NECESSARIOS.txt registra.
#
# O objetivo e trocar "o jogo nao abre, sera que e a BIOS?" por uma resposta:
# o arquivo esta la e correto, esta la e corrompido, ou nao esta. Um arquivo
# com o nome certo e o conteudo errado e o pior dos casos, porque o core
# carrega e falha depois, longe da causa.
#
# Uso:  powershell -NoProfile -File tools\check_bios.ps1

$ErrorActionPreference = "Stop"
$root  = Split-Path -Parent $PSScriptRoot
$bios  = Join-Path $root "app\bios"

if (-not (Test-Path $bios)) {
    Write-Host "[ERRO] Pasta nao encontrada: $bios" -ForegroundColor Red
    exit 1
}

# nome do arquivo -> @(md5 esperado, sistema, obrigatorio)
# MD5 vindos do BIOS_NECESSARIOS.txt. Onde o guia nao registra hash, o valor
# fica vazio e a checagem se limita a existencia.
$expected = @(
    @{ n = "kick40060.CD32.ext"; md5 = "";                                 sys = "Amiga CD32";        req = $true  },
    @{ n = "kick40060.CD32";     md5 = "";                                 sys = "Amiga CD32";        req = $true  },
    @{ n = "kick34005.A500";     md5 = "";                                 sys = "Amiga A500";        req = $true  },
    @{ n = "panafz1.bin";        md5 = "f47264dd47fe30f73ab3c010015c155b"; sys = "3DO";               req = $true  },
    @{ n = "5200.rom";           md5 = "281f20ea4320404ec820fb7ec0693b38"; sys = "Atari 5200";        req = $true  },
    @{ n = "ATARIXL.ROM";        md5 = "06daac977823773a3eea3422fd26a703"; sys = "Atari 5200";        req = $true  },
    @{ n = "ATARIBAS.ROM";       md5 = "0bac0c6a50104045d902df4503a4c30b"; sys = "Atari 5200";        req = $true  },
    @{ n = "ATARIOSA.ROM";       md5 = "eb1f32f5d9f382db1bbfb8d7f9cb343a"; sys = "Atari 5200";        req = $true  },
    @{ n = "ATARIOSB.ROM";       md5 = @("a3e8d617c95d08031fe1b20d541434b2", "4177f386a3bac989a981d3fe3388cb6c"); sys = "Atari 5200";        req = $true  },
    @{ n = "lynxboot.img";       md5 = "fcd403db69f54290b51035d82f835e7b"; sys = "Atari Lynx";        req = $true  },
    @{ n = "pcfx.rom";           md5 = "08e36edbea28a017f79f8d4f7ff9b6d7"; sys = "PC-FX";             req = $true  },
    @{ n = "scph5500.bin";       md5 = "8dd7d5296a650fac7319bce665a6a53c"; sys = "PlayStation NTSC-J";req = $true  },
    @{ n = "scph5501.bin";       md5 = "1e68c231d0896b7eadcad1d7d8e76129"; sys = "PlayStation NTSC-U";req = $true  },
    @{ n = "scph5502.bin";       md5 = "32736f17079d0b2b7024407c39bd3050"; sys = "PlayStation PAL";   req = $true  },
    @{ n = "7800 BIOS (U).rom";  md5 = "0763f1ffb006ddbe32e52d497ee848ae"; sys = "Atari 7800";        req = $false },
    @{ n = "bios_U.sms";         md5 = "";                                 sys = "Master System";     req = $false },
    @{ n = "bios_E.sms";         md5 = "";                                 sys = "Master System";     req = $false },
    @{ n = "bios_J.sms";         md5 = "";                                 sys = "Master System";     req = $false },
    @{ n = "bios.gg";            md5 = "";                                 sys = "Game Gear";         req = $false },
    @{ n = "kick40068.A1200";    md5 = "";                                 sys = "Amiga A1200 AGA";   req = $false },
    @{ n = "FMT_SYS.ROM";        md5 = @("3a4f31cbf48737db6f2df8ef036ab7d4", "86fb6f7280689259f0ca839dd3dd6cde"); sys = "FM Towns (System)"; req = $true  },
    @{ n = "FMT_DOS.ROM";        md5 = @("fa0c98f5a6b0c61ee34f9a72138fc6b5", "0585b19930d4a7f4c71bcc8a33746588"); sys = "FM Towns (DOS)";    req = $true  },
    @{ n = "FMT_FNT.ROM";        md5 = @("a35ee3364f26b5270be7f3f185ef3577", "b91300e55b70227ce98b59c5f02fa8dd"); sys = "FM Towns (Font)";   req = $true  },
    @{ n = "FMT_DIC.ROM";        md5 = @("e28e67838634891b8d6505417ec651db", "8fa4e553f28cfc0c30a0a1e589799942"); sys = "FM Towns (Dic)";    req = $true  },
    @{ n = "FMT_F20.ROM";        md5 = @("9559e2ab176041ec1eaae92795c6f671", "ac0c7021e9bf48ca84b51ab651169a88"); sys = "FM Towns (Font20)"; req = $false },
    @{ n = "np2kai\font.bmp";    md5 = "";                                 sys = "PC-98 Font Bitmap";  req = $true  },
    @{ n = "np2kai\bios.rom";    md5 = @("a4fc2355403063f9cfb403f56b50eec3", "cd237e16e7e77c06bb58540e9e9fca68"); sys = "PC-98 System BIOS"; req = $false },
    @{ n = "np2kai\itf.rom";     md5 = @("185078a63503aa3ca8bb64b38d3ab16e", "72ea51443070f0e9212bfc9b793ee28e"); sys = "PC-98 ITF";         req = $false },
    @{ n = "np2kai\sound.rom";   md5 = @("97800c7329524da5f6efb2e4d9c74567", "524473c1a5a03b17e21d86a0408ff827"); sys = "PC-98 Sound BIOS";  req = $false }
)

$ok = 0; $bad = 0; $missReq = 0; $missOpt = 0

Write-Host ""
Write-Host "Conferindo $bios" -ForegroundColor Cyan
Write-Host ("-" * 78)

foreach ($e in $expected) {
    $path = Join-Path $bios $e.n
    if (-not (Test-Path $path)) {
        if ($e.req) {
            Write-Host ("  FALTA     {0,-22} {1}" -f $e.n, $e.sys) -ForegroundColor Red
            $missReq++
        } else {
            Write-Host ("  opcional  {0,-22} {1}" -f $e.n, $e.sys) -ForegroundColor DarkGray
            $missOpt++
        }
        continue
    }

    # Um arquivo minusculo nunca e uma BIOS. O caso real que motivou isto: o
    # scph5500.bin e o panafz1.bin baixados tinham 131 e 132 bytes porque o
    # servidor entregou o ponteiro do Git LFS em vez do arquivo. Sem MD5 no
    # guia, a checagem de hash nao pega, e o arquivo passava como "presente".
    $size = (Get-Item $path).Length
    $head = ""
    try {
        $bytes = [System.IO.File]::ReadAllBytes($path)
        $head  = [System.Text.Encoding]::ASCII.GetString($bytes[0..([Math]::Min(63, $bytes.Length - 1))])
    } catch {}

    if ($head -like "version https://git-lfs*") {
        Write-Host ("  LFS STUB  {0,-22} {1}" -f $e.n, $e.sys) -ForegroundColor Red
        Write-Host ("            {0} bytes - e um ponteiro do Git LFS, nao a BIOS. Baixe de novo." -f $size) -ForegroundColor DarkRed
        $bad++
        continue
    }

    # O palpite por tamanho so vale quando nao ha hash. O MD5 e autoritativo:
    # a BIOS do Atari 5200 tem 2 KB legitimos, e uma primeira versao deste
    # script a reprovou por ser "pequena demais" mesmo com o hash batendo.
    if ([string]::IsNullOrEmpty($e.md5)) {
        if ($size -lt 512) {
            Write-Host ("  SUSPEITO  {0,-22} {1}" -f $e.n, $e.sys) -ForegroundColor Red
            Write-Host ("            so {0} bytes - pequeno demais para ser uma BIOS." -f $size) -ForegroundColor DarkRed
            $bad++
        } else {
            Write-Host ("  presente  {0,-22} {1} ({2:N0} bytes, sem MD5 no guia)" -f $e.n, $e.sys, $size) -ForegroundColor Yellow
            $ok++
        }
        continue
    }

    $validHashes = @($e.md5) | Where-Object { -not [string]::IsNullOrEmpty($_) } | ForEach-Object { $_.ToLower() }
    $h = (Get-FileHash $path -Algorithm MD5).Hash.ToLower()
    if ($validHashes -contains $h) {
        Write-Host ("  OK        {0,-22} {1}" -f $e.n, $e.sys) -ForegroundColor Green
        $ok++
    } else {
        Write-Host ("  ERRADO    {0,-22} {1}" -f $e.n, $e.sys) -ForegroundColor Red
        Write-Host ("            esperado {0}" -f ($validHashes -join " | ")) -ForegroundColor DarkRed
        Write-Host ("            obtido   {0}" -f $h)     -ForegroundColor DarkRed
        $bad++
    }
}

Write-Host ("-" * 78)
Write-Host ("  {0} conferidos, {1} com conteudo errado, {2} obrigatorios faltando, {3} opcionais faltando" -f $ok, $bad, $missReq, $missOpt)

# O log e a outra metade da resposta: o core diz o que ele proprio nao achou,
# com o nome exato do arquivo que espera.
$log = Join-Path $root "app\karamelo.log"
if (Test-Path $log) {
    $lines = Select-String -Path $log -Pattern "firmware is missing|not found!|ROM .* missing" -CaseSensitive:$false |
             Select-Object -Last 12 -ExpandProperty Line
    if ($lines) {
        Write-Host ""
        $age = [int]((Get-Date) - (Get-Item $log).LastWriteTime).TotalMinutes
        Write-Host ("  Reclamacoes dos cores no log (ultima escrita ha {0} min - pode ser de ANTES de voce copiar os arquivos):" -f $age) -ForegroundColor Cyan
        $lines | ForEach-Object { Write-Host ("    " + $_.Trim()) -ForegroundColor DarkYellow }
    }
}

Write-Host ""
if ($bad -gt 0 -or $missReq -gt 0) { exit 1 } else { exit 0 }
