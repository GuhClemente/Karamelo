# Servidor das salas de chat pela internet

O chat do Karamelo funciona na rede local sem servidor nenhum. Este servidor
(`server/chat/server.mjs`) só existe para o chat alcançar quem está fora dela.
Não tem dependências: Node 20 ou mais novo, um arquivo só.

## O que ele guarda

| Dado | Onde | Por quanto tempo |
|---|---|---|
| Últimas 100 mensagens de cada sala | memória | até sair da fila ou o servidor reiniciar |
| Quem está online e o que joga | memória | enquanto o jogador estiver conectado |
| Código de partida | memória | até ser usado, ou 10 minutos |
| Endereço de quem está conectado | memória | só durante a conexão, para limitar abuso; nunca vai a outro jogador |
| Denúncias, com as últimas mensagens do denunciado | memória | até 500 denúncias ou reiniciar |
| Lista de banidos, em hash com chave | disco (`bans.json`) | até remover |

Nome e e-mail nunca chegam: o login só pede o escopo `openid`. O ID anônimo do Google
também não é gravado nem enviado a ninguém. O login do jogador é um token assinado que
fica no PC dele, por isso reiniciar o servidor não desconecta ninguém.

## Como está publicado

Desde 31/10/2026 em `https://chat.karamelo-emu.com`, no mesmo servidor do site
(`187.127.59.127`), com registro `A` `chat` na Hostinger.

**É um container à parte, `karamelo-chat`, fora do painel do Coolify.** Ele usa o proxy
do Coolify (a rede `coolify` e os labels do Traefik), que também cuida do certificado.
Portanto ele não aparece na lista de aplicações do Coolify e não recebe deploy
automático a cada push.

| Em `/data/karamelo-chat/` | O que é |
|---|---|
| `src/` | `server.mjs`, `Dockerfile` e `deploy.sh`, copiados deste repositório |
| `client_ids` | os Client IDs aceitos; vários, separados por vírgula |
| `.env` | `CHAT_HASH_SECRET` e `CHAT_ADMIN_TOKEN`, gerados no servidor, `CHAT_GOOGLE_CLIENT_SECRET` e `TYPESAFE_API_KEY` (modo 600) |
| `data/bans.json` | a lista de banidos |

**Nunca apague nem troque o `CHAT_HASH_SECRET`:** todos os jogadores seriam desconectados,
os números das tags mudariam e os banidos voltariam.

## Atualizar

```bash
scp server/chat/server.mjs server/chat/moderation.mjs server/chat/blocklist.txt server/chat/Dockerfile server/chat/deploy.sh root@187.127.59.127:/data/karamelo-chat/src/
ssh root@187.127.59.127 "sed -i 's/\r$//' /data/karamelo-chat/src/deploy.sh; bash /data/karamelo-chat/src/deploy.sh tls"
```

Conferir: `https://chat.karamelo-emu.com/healthz` responde `{"ok":true}`.

O `CHAT_TRUST_PROXY=1` do Dockerfile só é seguro com um proxy na frente, que acrescenta
o endereço real ao `X-Forwarded-For`. Sem proxy, deixe em `0`, ou qualquer um escolhe o
IP que quiser.

## Antes de divulgar

1. **Lançar a versão nova do aplicativo:** é ela que busca o Client ID e o segredo neste
   servidor (`/v1/login-config`) na hora do login, tanto do chat quanto da nuvem. Sem este
   servidor no ar, ninguém faz login novo; quem já conectou a nuvem continua sincronizando.
   Eles não ficam no código, que é público,
   e podem ser trocados aqui sem novo lançamento. O Google não trata o segredo de um app
   instalado como confidencial, então entregá-lo assim não piora nada.
2. **Tela de consentimento do Google:** escopo `openid` registrado e app publicado, ou só
   as contas de teste conseguem entrar (`docs/CLOUD_GOOGLE.md`).
3. **Site:** privacidade, termos, idade mínima e regras das salas (`docs/SITE_SYNC.md`, seção 53).

## Moderação

Duas camadas, ajustáveis no admin do site (`/admin`, painel "Chat ao vivo"):

- **Lista de palavras e links:** a palavra vira um doce sorteado (`caramelinho`, `pudim`...).
  Roda sempre no servidor, com o JEV aprovando ou não, e mesmo com ele fora do ar. A lista é
  a LDNOOBW (`blocklist.txt`, inglês e português). `moderation.mjs` já libera palavras comuns
  dela, como `comer` e `mama`; no painel dá para bloquear e liberar sem deploy.
- **JEV (TypeSafe AI):** avalia a mensagem inteira e barra ameaça, golpe e assédio. Quem
  escreveu recebe um aviso. A chave fica só no servidor (`TYPESAFE_API_KEY`), e ele recebe só
  o texto, sem tag, conta ou IP. **Se cair, ficar sem crédito ou demorar mais de 1,5 s, a
  mensagem segue só com a lista.** Depois de chave recusada ou falta de crédito, ele não é
  consultado por 10 minutos; depois de 5 falhas seguidas, por 1 minuto. O painel mostra isso.

Na rede local as mensagens não passam pelo servidor, então nada disso se aplica lá.

No servidor, o painel do site precisa de `CHAT_ADMIN_TOKEN` igual ao do chat, e de
`CHAT_SERVER_URL` se o endereço mudar. Os ajustes ficam em `/data/settings.json`.

## Moderar por comando

Ver denúncias:

```bash
source /data/karamelo-chat/.env   # no servidor
curl -H "Authorization: Bearer $CHAT_ADMIN_TOKEN" https://chat.karamelo-emu.com/v1/admin/reports
```

Banir a conta por trás de uma tag, o que também apaga as mensagens dela das salas:

```bash
curl -X POST -H "Authorization: Bearer $CHAT_ADMIN_TOKEN" -d '{"tag":"Nome#ABC123"}' https://chat.karamelo-emu.com/v1/admin/ban
```

Só dá para banir tag vista recentemente: online, nas mensagens ou em uma denúncia. Para
desfazer, tire a linha de `/data/bans.json` e reinicie.

Limites já ativos: 5 mensagens por 10 segundos por conta, 10 logins por minuto e 500
pedidos por 10 segundos por endereço.

## Testar

```bash
node --test server/chat/server.test.mjs
node tools/test_chat_internet.mjs
```

O primeiro cobre tokens forjados, limites, sessão, partida, ponte e banimento. O segundo sobe o
servidor neste PC e coloca dois Karamelo de verdade para conversar, convidar e abrir a
partida. Os dois fazem parte do `compile.bat`, do `tools/test_netplay_unix.sh` e do CI.
Eles assinam os logins com uma chave de teste; o Google de verdade só se testa com o
servidor no ar e uma conta real.

Para um aplicativo usar outro servidor, escreva o endereço em `Config/chat_server.txt`.
Só vale `https://`, ou `http://127.0.0.1` para testes neste PC.

## O que ele não faz

Não grava conversa nem partida, e não entrega o IP de um jogador a outro. A voz não
passa por aqui: por ser direta entre os PCs, ela fica desligada nas partidas pela internet.

## A ponte de partida

Porta **55440 (TCP)**, aberta direto no servidor, fora do proxy. Os dois jogadores se
conectam a ela com um bilhete de uso único, e ela repassa os dados entre eles. O bilhete
sai de `/v1/match` (quem cria) e `/v1/match/join` (quem digita o código).

Limites: 100 partidas ao mesmo tempo, 8 conexões por endereço, 4 GB e 12 horas por
partida, 5 minutos parada. **Toda a banda das partidas passa por este servidor:** se o uso
crescer, acompanhe o consumo. Os dados não são criptografados nesse trecho; são botões
e estado do jogo, sem nada da conta do jogador.
