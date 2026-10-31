# Servidor das salas de chat pela internet

O chat do Karamelo funciona na rede local sem servidor nenhum. Este servidor
(`server/chat/server.mjs`) só existe para o chat alcançar quem está fora dela.
Não tem dependências: Node 20 ou mais novo, um arquivo só.

## O que ele guarda

| Dado | Onde | Por quanto tempo |
|---|---|---|
| Últimas 100 mensagens de cada sala | memória | até sair da fila ou o servidor reiniciar |
| Quem está online e o que joga | memória | enquanto o jogador estiver conectado |
| IP de quem convidou para uma partida | memória | 10 minutos; só vai para quem aceitar |
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
| `.env` | `CHAT_HASH_SECRET` e `CHAT_ADMIN_TOKEN`, gerados no servidor (modo 600) |
| `data/bans.json` | a lista de banidos |

**Nunca apague nem troque o `CHAT_HASH_SECRET`:** todos os jogadores seriam desconectados,
os números das tags mudariam e os banidos voltariam.

## Atualizar

```bash
scp server/chat/server.mjs server/chat/Dockerfile server/chat/deploy.sh root@187.127.59.127:/data/karamelo-chat/src/
ssh root@187.127.59.127 "sed -i 's/\r$//' /data/karamelo-chat/src/deploy.sh; bash /data/karamelo-chat/src/deploy.sh tls"
```

Conferir: `https://chat.karamelo-emu.com/healthz` responde `{"ok":true}`.

O `CHAT_TRUST_PROXY=1` do Dockerfile só é seguro com um proxy na frente, que acrescenta
o endereço real ao `X-Forwarded-For`. Sem proxy, deixe em `0`, ou qualquer um escolhe o
IP que quiser.

## Antes de divulgar

1. **Client ID no aplicativo:** hoje ele só existe em `Config/google_credentials.json` do
   PC do projeto. Sem ele no aplicativo distribuído, o jogador vê que o login não está ativo.
2. **Tela de consentimento do Google:** escopo `openid` registrado e app publicado, ou só
   as contas de teste conseguem entrar (`docs/CLOUD_GOOGLE.md`).
3. **Site:** privacidade, termos, idade mínima e regras das salas (`docs/SITE_SYNC.md`, seção 53).

## Moderar

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

O primeiro cobre tokens forjados, limites, sessão, convite e banimento. O segundo sobe o
servidor neste PC e coloca dois Karamelo de verdade para conversar, convidar e abrir a
partida. Os dois fazem parte do `compile.bat`, do `tools/test_netplay_unix.sh` e do CI.
Eles assinam os logins com uma chave de teste; o Google de verdade só se testa com o
servidor no ar e uma conta real.

Para um aplicativo usar outro servidor, escreva o endereço em `Config/chat_server.txt`.
Só vale `https://`, ou `http://127.0.0.1` para testes neste PC.

## O que ele não faz

A partida não passa por aqui. O convite atravessa a internet, mas quem convida ainda
precisa da porta 55435 (UDP) aberta no roteador, ou de VPN. Retransmitir a partida é
outro serviço, com outro custo de banda.
