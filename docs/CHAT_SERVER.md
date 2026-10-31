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

## Publicar no Coolify

1. **DNS:** crie o registro `A` de `chat.karamelo-emu.com` para o IP do servidor.
2. **Client ID:** registre o escopo `openid` na tela de consentimento
   (`docs/CLOUD_GOOGLE.md`). Sem o Client ID embutido no aplicativo ou em
   `Config/cloud.cfg`, o botão de entrar avisa que falta.
3. **Novo recurso no Coolify:** tipo Dockerfile, repositório deste projeto, pasta
   `server/chat`, domínio `https://chat.karamelo-emu.com`, porta `8787`.
4. **Volume:** monte um volume em `/data`, ou os banimentos se perdem a cada deploy.
5. **Variáveis de ambiente** (nunca no git):

| Variável | Valor |
|---|---|
| `CHAT_GOOGLE_CLIENT_IDS` | o Client ID do aplicativo; vários, separados por vírgula |
| `CHAT_HASH_SECRET` | segredo aleatório (`openssl rand -hex 32`) |
| `CHAT_ADMIN_TOKEN` | outro segredo aleatório, para moderar |

**Nunca troque o `CHAT_HASH_SECRET`:** todos os jogadores seriam desconectados, os
números das tags mudariam e os banidos voltariam.

6. **Conferir:** `https://chat.karamelo-emu.com/healthz` responde `{"ok":true}`.
7. **Antes de divulgar:** atualize a privacidade e os termos do site (`docs/SITE_SYNC.md`,
   seção 53).

O Dockerfile já liga `CHAT_TRUST_PROXY=1`. Ele só é seguro com um proxy na frente, como
o do Coolify, que acrescenta o endereço real ao `X-Forwarded-For`. Sem proxy, deixe em
`0`, ou qualquer um escolhe o IP que quiser.

## Moderar

Ver denúncias:

```bash
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
