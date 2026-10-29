# Catálogo de Ports & Recomp

Desde a v0.9.78 a lista de ports nativos não está mais no código C++. Ela é um
arquivo de dados:

| Arquivo | Papel |
|---|---|
| `data/ports.json` | **Fonte da verdade.** Um objeto por port. |
| `include/ports_catalog_json.h` | Cópia embutida no executável, gerada por `tools/embed_ports.py`. Não editar à mão. |
| `https://karamelo-emu.com/downloads/ports.json` | Cópia publicada. O app baixa em segundo plano e usa a partir do próximo início. |
| `Config/ports_cache.json` (na pasta do app) | Onde o app guarda o catálogo baixado. |

Por que embutido **e** online: o auto-update troca só o executável. Um
`ports.json` solto ao lado dele nunca chegaria a quem atualizou pelo app, por
isso o binário sempre carrega uma cópia própria. O catálogo online só vale se
for **mais novo** (`catalog_version` maior) e passar na validação; um arquivo
corrompido ou antigo é ignorado e o app segue com o embutido.

## Adicionar ou alterar um port

1. Edite `data/ports.json`. Os campos estão documentados em
   `include/port_catalog.h` (`struct PortDefinition`). Os obrigatórios são `id`
   (vira a pasta `ports/<id>/`; só letras, números, `_` e `-`) e
   `display_name`. Os demais são opcionais e só aparecem quando diferem do
   padrão.
2. **Aumente `catalog_version`** em 1. Sem isso, quem já tem o app não troca
   para o catálogo novo.
3. Atualize as contagens em `CREDITS.md` (tabela e a linha
   "**X de Y têm build Linux** e **Z de Y têm build macOS**"), `docs/SITE_SYNC.md`,
   `CLAUDE.md` e `AGENTS.md`.
4. Rode `python tools/embed_ports.py` e depois `compile.bat`.
   Os testes (`tests/test_port_catalog.cpp`) reprovam o build se:
   - o JSON for inválido, tiver `id` repetido ou caminho que saia da pasta do port;
   - o cabeçalho embutido estiver diferente de `data/ports.json`;
   - as contagens do `CREDITS.md` não baterem com o catálogo.
5. Commite `data/ports.json` e `include/ports_catalog_json.h` juntos.
6. Publique:
   - **só o catálogo**, sem release do app: `deploy_ports_catalog.bat`;
   - ou junto de uma release: os scripts `deploy_all.bat`, `upload_to_server.bat`,
     `deploy_linux.sh` e `deploy_macos.sh` já enviam `data/ports.json`.

## Conferir o que o app carregou

- `karamelo.log` registra uma linha `[PORT-CATALOG]` por início: qual catálogo
  (embutido ou baixado), a versão e quantos ports.
- `Karamelo.exe --dump-port-catalog arquivo.json` grava o catálogo em uso no
  mesmo formato do `data/ports.json`.
- `Karamelo.exe --list-ports` lista o que cada port resolveu no disco.

## Campo `notes`

Texto livre para quem mantém o catálogo (por que um flag está assim, quando a
release foi conferida). O app nunca lê. As notas que existiam como comentário
na antiga tabela C++ foram trazidas para este campo.

## Versão e SHA-256 fixos

Cada port com repositório no GitHub tem, no catálogo:

- `release_tag`: a release exata que o app instala. Uma release nova do projeto
  não muda nada para o jogador até o catálogo ser atualizado.
- `asset_sha256`: o SHA-256 de cada arquivo daquela release que o app pode
  baixar. Arquivo fora da lista ou com hash diferente é recusado e apagado -
  um repositório invadido ou um arquivo trocado na release não chega ao PC
  do jogador.

Port sem esses campos (hoje: os que não publicam release com arquivos e o
Foxhollow, que usa feed próprio) continua instalando a release mais nova, como
antes.

`tools/pin_ports.py` preenche e atualiza os dois campos (usa o `digest` que o
GitHub publica; quando falta, baixa o arquivo e calcula):

```
python tools/pin_ports.py               # fixa os ports ainda sem versão
python tools/pin_ports.py --update       # move todos para a release mais nova
python tools/pin_ports.py --port ID --update
python tools/pin_ports.py --check        # só lista quem tem versão nova
```

Ele sobe `catalog_version` e regera o cabeçalho embutido quando algo muda.

## Automação (GitHub Actions)

- `.github/workflows/build.yml`: build e testes no Windows, Linux e macOS em
  todo push e pull request. Os testes incluem as checagens do catálogo.
- `.github/workflows/ports.yml`:
  - **PR que mexe no catálogo**: compila e instala de verdade cada port
    novo ou alterado (`tools/validate_ports.ps1`, que roda
    `Karamelo.exe --install-port`). Port que não instala reprova o PR.
  - **Toda segunda-feira**: roda `pin_ports.py --update`, instala os ports
    com versão nova e abre um PR com a tabela do resultado. Nada chega ao
    jogador sem o merge e o `deploy_ports_catalog.bat`.

Descoberta de ports novos (busca no GitHub, Discord, RSS) fica fora daqui: é o
papel sugerido para o n8n - ele só precisa abrir uma issue ou um PR com a
entrada nova, e o mesmo check acima valida.
