@echo off
setlocal
cls

echo =======================================================
echo   Karamelo - Verificador de Arquivos no Servidor
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
echo Conectando em %SERVER_USER%@%SERVER_IP%:%REMOTE_DIR%...
echo (Digite sua senha/passphrase da chave SSH caso seja solicitada)
echo.

ssh -o StrictHostKeyChecking=no %SERVER_USER%@%SERVER_IP% "echo '--- CONTEUDO E PERMISSOES DE %REMOTE_DIR% ---' && ls -lah %REMOTE_DIR% && echo '' && echo '--- CORRIGINDO PERMISSOES PARA LEITURA PUBLICA (chmod 755) ---' && chmod -R 755 %REMOTE_DIR% && echo 'Permissoes atualizadas:' && ls -lah %REMOTE_DIR%"

echo.
echo =======================================================
echo   Consulta e correcao de permissoes finalizada!
echo =======================================================
echo.
pause
