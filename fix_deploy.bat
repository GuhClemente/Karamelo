@echo off
setlocal
cls

echo =======================================================
echo   Karamelo - Correcao Definitiva de Download
echo =======================================================
rem Endereco, usuario e diretorio remoto do servidor ficam em deploy_env.bat,
rem que esta fora do controle de versao. Sem ele o deploy nao roda: copie o
rem deploy_env.example.bat e preencha. Quem preferir pode so exportar
rem SERVER_IP/SERVER_USER/REMOTE_DIR no ambiente - deploy_env.bat nao
rem sobrescreve o que ja estiver definido.
if not exist "%~dp0deploy_env.bat" (
    echo [ERRO] deploy_env.bat nao encontrado.
    echo Copie deploy_env.example.bat para deploy_env.bat e preencha o servidor.
    pause
    exit /b 1
)
call "%~dp0deploy_env.bat"

echo.
echo [1/2] Enviando script de correcao para o servidor...
scp -o StrictHostKeyChecking=no "%~dp0fix_server.sh" %SERVER_USER%@%SERVER_IP%:/root/fix_server.sh
if errorlevel 1 (
    echo [ERRO] Falha ao enviar o script via SCP.
    pause
    exit /b 1
)

echo.
echo [2/2] Executando correcao no servidor...
ssh -o StrictHostKeyChecking=no %SERVER_USER%@%SERVER_IP% "chmod +x /root/fix_server.sh && /root/fix_server.sh"

echo.
echo =======================================================
echo   Processo finalizado!
echo =======================================================
echo.
pause
