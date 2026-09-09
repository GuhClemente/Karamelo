@echo off
rem ---------------------------------------------------------------------------
rem  deploy_env.example.bat - modelo das credenciais de deploy
rem
rem  Copie este arquivo para "deploy_env.bat" e preencha com os valores reais:
rem
rem      copy deploy_env.example.bat deploy_env.bat
rem
rem  O deploy_env.bat esta no .gitignore e nunca vai para o repositorio. Este
rem  aqui, com valores de exemplo, vai - e serve so para documentar quais
rem  variaveis os scripts esperam.
rem
rem  Quem preferir nao ter arquivo nenhum pode exportar as variaveis no
rem  ambiente antes de rodar os scripts; os "if not defined" abaixo respeitam
rem  o que ja estiver definido e nao sobrescrevem nada:
rem
rem      set SERVER_IP=203.0.113.10
rem      set SERVER_USER=deploy
rem      deploy_all.bat
rem
rem  Usado por: deploy_all.bat, upload_to_server.bat, fix_deploy.bat e
rem  check_server_downloads.bat.
rem ---------------------------------------------------------------------------

if not defined SERVER_IP   set SERVER_IP=203.0.113.10
if not defined SERVER_USER set SERVER_USER=root
if not defined REMOTE_DIR  set REMOTE_DIR=/data/downloads/
