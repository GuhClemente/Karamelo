# Catálogo de Ports & Recomp

Desde a v0.9.77 a lista de ports nativos não está mais no código C++. Ela é um
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
4. Rode `python tools/embed_ports.py` e depois `compile_port.bat`.
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
