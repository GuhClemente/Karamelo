# Registrar o Client ID do Google (Karamelo Cloud)

A sincronização de saves usa a conta Google do próprio jogador, mas o Google exige
que o aplicativo seja registrado uma vez, pelo dono do projeto. Só ele pode fazer
isso, porque depende da conta dele e do aceite dos termos do Google.

## Passo a passo

1. Abra <https://console.cloud.google.com/> com a conta do projeto e crie um
   projeto chamado `Karamelo`.
2. Em **APIs e serviços > Biblioteca**, ative a **Google Drive API**.
3. Em **APIs e serviços > Tela de permissão OAuth**, escolha **Externo** e preencha
   nome, e-mail de suporte e o site `https://karamelo-emu.com`.
4. Em **Escopos**, adicione só `https://www.googleapis.com/auth/drive.appdata`. É a
   pasta oculta do aplicativo: o Karamelo não enxerga os outros arquivos do Drive.
5. Em **Credenciais > Criar credenciais > ID do cliente OAuth**, escolha o tipo
   **App para computador** e copie o ID. Ele termina em `.apps.googleusercontent.com`.
6. Coloque o ID em `kDefaultGoogleClientId`, em `src/cloud_sync.cpp`, e compile.
7. Enquanto o app estiver em modo de teste, só as contas adicionadas em **Usuários de
   teste** conseguem entrar, e o login expira em 7 dias. Para liberar para todos, use
   **Publicar app**. O Google pode pedir verificação, e a tela mostra um aviso até lá.

Para testar sem compilar, coloque em `app/Config/cloud.cfg` a linha:

```
google_client_id=SEU_ID.apps.googleusercontent.com
```

## Conferir

No Karamelo: **Configurações > Karamelo Cloud > Conectar Google Drive**. Depois de
entrar no navegador, a tela do Karamelo mostra `Conectado`. Salve em um jogo, use
**Sincronizar Agora** e confira o mesmo save em outro PC com a mesma conta.

O teste automático (`CloudSync_RealSyncAgainstDriveApi`) cobre o envio, o download, o
conflito e o token contra um servidor simulado. O login real do Google só se testa
com o ID registrado.
