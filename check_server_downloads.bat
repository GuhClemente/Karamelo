@echo off
setlocal
cls

echo =======================================================
echo   MiSTer 4 ALL - Verificador de Arquivos no Servidor
echo =======================================================
echo.
echo Conectando em root@187.127.59.127:/data/downloads/...
echo (Digite sua senha/passphrase da chave SSH caso seja solicitada)
echo.

ssh -o StrictHostKeyChecking=no root@187.127.59.127 "echo '--- CONTEUDO E PERMISSOES DE /data/downloads/ ---' && ls -lah /data/downloads/ && echo '' && echo '--- CORRIGINDO PERMISSOES PARA LEITURA PUBLICA (chmod 755) ---' && chmod -R 755 /data/downloads/ && echo 'Permissoes atualizadas:' && ls -lah /data/downloads/"

echo.
echo =======================================================
echo   Consulta e correcao de permissoes finalizada!
echo =======================================================
echo.
pause
