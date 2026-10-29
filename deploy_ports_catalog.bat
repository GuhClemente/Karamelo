@echo off
setlocal

rem Publica so o catalogo de Ports & Recomp (data\ports.json) em
rem https://karamelo-emu.com/downloads/ports.json - sem gerar release do app.
rem Quem ja tem o Karamelo baixa o catalogo novo sozinho e passa a ver o port
rem no proximo inicio. Antes de publicar:
rem   1. aumente "catalog_version" em data\ports.json (o app so troca para um
rem      catalogo de versao MAIOR que a que ele tem);
rem   2. rode compile.bat - os testes conferem o JSON, o cabecalho embutido
rem      e as contagens do CREDITS.md.
rem Detalhes em docs\PORTS_CATALOG.md.

cd /d "%~dp0"

if not exist "deploy_env.bat" (
    echo [ERRO] deploy_env.bat nao encontrado - copie deploy_env.example.bat e preencha.
    exit /b 1
)
call "%~dp0deploy_env.bat"

echo [1/3] Validando data\ports.json...
python tools\embed_ports.py
if errorlevel 1 (
    echo [ERRO] data\ports.json invalido. Nada foi publicado.
    exit /b 1
)
git diff --quiet -- include\ports_catalog_json.h
if errorlevel 1 (
    echo [AVISO] include\ports_catalog_json.h mudou ao regerar: rode compile.bat e
    echo         commite o JSON e o cabecalho juntos antes de publicar.
    exit /b 1
)

rem O que sobe tem que ser o que esta no GitHub: o catalogo publicado e o do
rem repositorio, e o cabecalho embutido na proxima release vem dele.
git diff --quiet HEAD -- data/ports.json include/ports_catalog_json.h
if errorlevel 1 (
    echo [ERRO] data\ports.json ou o cabecalho tem alteracao nao commitada. Nada foi publicado.
    exit /b 1
)
git fetch -q origin
set PUSHED=
for /f %%b in ('git branch -r --contains HEAD') do set PUSHED=1
if not defined PUSHED (
    echo [ERRO] O commit atual ainda nao esta no GitHub - rode git push antes.
    exit /b 1
)
python tools\release_manifest.py catalog-check
if errorlevel 1 exit /b 1

echo [2/3] Enviando ports.json para %SERVER_USER%@%SERVER_IP%:%REMOTE_DIR%...
scp -o StrictHostKeyChecking=no "%~dp0data\ports.json" %SERVER_USER%@%SERVER_IP%:%REMOTE_DIR%
if errorlevel 1 (
    echo [ERRO] Falha no upload via SCP.
    exit /b 1
)

echo [3/3] Publicando no site (reinicio do app no Coolify)...
scp -o StrictHostKeyChecking=no "%~dp0fix_server.sh" %SERVER_USER%@%SERVER_IP%:/root/fix_server.sh
ssh -o StrictHostKeyChecking=no %SERVER_USER%@%SERVER_IP% "sed -i 's/\r$//' /root/fix_server.sh && chmod +x /root/fix_server.sh && /root/fix_server.sh"

echo.
echo   Catalogo publicado: https://karamelo-emu.com/downloads/ports.json
exit /b 0
