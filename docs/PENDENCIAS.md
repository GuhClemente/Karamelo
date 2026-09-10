# Pendências

Estado em 09/09/2026. Cada item traz **a evidência que sustenta o
diagnóstico**, não só a conclusão, para que possa ser refutado a partir da
mesma medição.

O que já foi corrigido está no fim, curto, para ninguém refazer.

---

## Só o dono pode fechar

### Testes no app com janela

A bateria (`tools/stress_cores.ps1`) roda sem janela e não substitui estes:

- **PSP e PlayStation 2** — abrir um jogo, sair, abrir de novo. Foi assim que o
  crash original apareceu, e é assim que se confirma que sumiu.
- **Nintendo 3DS** — o único que a bateria não consegue avaliar: o Citra não
  cria renderizador sem janela. As chaves foram corrigidas em 09/09/2026 (ver
  no fim).
- **MSX com controle** — a ponte controle→teclado foi restringida a MSX, C64,
  ZX Spectrum, DOS e Amiga. O arcade voltou ao normal; falta confirmar o outro
  lado, que o controle ainda digita no MSX.
- **Arcade** — um botão do controle deve fazer **uma** ação.

### ROMs de 6 sistemas

Atari 5200, Atari Lynx, WonderSwan, Neo Geo Pocket, PC-FX e 3DO seguem sem
cobertura da bateria. **Quatro deles ganharam BIOS válida em 09/09/2026** e
nunca foram exercitados.

Procurei no `D:\` por nome de pasta, por extensão (`.a52`, `.lnx`, `.ws`,
`.ngp`) até quatro níveis, e dentro das coleções soltas: não estão nessa
máquina. Basta uma ROM em cada pasta de `app/roms/` e a bateria cobre.

### O site publica 41 motores

O correto é **40**. O site anuncia 41, que é a contagem de arquivos — e conta
`n64_parallel.dll` e `n64.dll`, que são o mesmo binário, como dois motores.

O número aparece em **oito lugares** da página, metatags e JSON-LD incluídos.
A lista motor a motor, pronta para publicar, está em [MOTORES.md](MOTORES.md);
a auditoria completa do site, com todos os pontos a corrigir, está na seção 9
de [SITE_SYNC.md](SITE_SYNC.md).

---

## Arquitetural — sem conserto pontual

### A thread do core ainda é morta à força

`CoreShutdown` espera 3 segundos e chama `TerminateThread`. Foi essa a mecânica
que congelou a interface em `LOADING...` no PlayStation 2: matar a thread não
solta os locks que ela segurava, e quem pegar o mesmo lock depois espera para
sempre. É comportamento documentado da API, não uso errado.

As threads de áudio e do teardown do D3D11 passaram a ser **abandonadas** em vez
de mortas, e o mesmo princípio provavelmente vale aqui. **Mas não é verificável
no estado atual:** nenhum core chega mais nesse timeout, então a bateria não exercita o
caminho, e uma thread de core abandonada seguiria chamando os callbacks do
frontend enquanto o próximo core carrega — pior que o congelamento. Trocar uma
falha conhecida por uma não medida não é melhoria.

As duas saídas reais: não matar thread nenhuma (aceitando que core travado trava
o app, e dizendo isso ao jogador), ou rodar cores em processo separado.

### O `context_destroy` do PCSX2 nunca retorna

Medido: **20 estouros em 20 descarregamentos**, 100%. O frontend contorna —
chama uma vez por sessão, com prazo de 5 segundos, e pula daí em diante — mas o
contexto D3D11 vaza até o app fechar. Um core que não libera o que alocou não é
algo que o frontend conserte de fora.

---

## Menor

**`ATARIOSB.ROM` tem MD5 de outra revisão** do OS-B. Tamanho certo (10 KB), e o
Atari 5200 tem as outras quatro ROMs corretas. Provavelmente funciona; só não é
a revisão que o guia registrou. Confira com `tools/check_bios.ps1`.

**14 ocorrências de `Unable to open file.` no log**, sem contexto que diga de
qual core vêm. Não afetam nenhum sistema da bateria. Se algo quebrar sem
explicação, começar por aqui.

**O nome `arcade_fbneo.dll` continua mentindo** — é MAME 0.289. O `CREDITS.md`
e o `MOTORES.md` já dizem a verdade; renomear o arquivo quebraria instalações
existentes e o `download_cores.ps1`. Sem urgência.

**A arte ainda carrega a marca antiga** — 16 wallpapers mais o `karamelo_chef` e
o `karamelo_arcade` têm "SABOR MISTER" pintado na ilustração, e o repositório é
público. Decisão do dono, registrada aqui para não parecer resolvido.

---

## Corrigido em 09/09/2026 — não refazer

Todos com bateria verde depois: 28 sistemas, 3 ciclos de carrega/desliga/
recarrega, zero crash e zero trava.

| o que quebrava | causa real | commit |
|---|---|---|
| Um botão fazia duas ações | a ponte controle→teclado valia para todos os cores; o MAME lê teclado **e** joypad, e recebia cada botão duas vezes | `c665689` |
| Arcade falhava na 2ª carga, crashava na 3ª | a DLL era mantida mapeada e os globais do core sobreviviam ao unload | `7580e6d` |
| Toda busca fracassada terminava no Flycast | ele estava na fila geral de arcade, sendo core de Dreamcast/Naomi | `02bee22` |
| PS2 travava para sempre ao fechar | `context_destroy` do PCSX2 não retorna; agora tem prazo e é pulado depois | `1694b3d`, `7e304e3` |
| PSP crashava ao carregar | pedia `SET_HW_RENDER` **antes** de pedir negociação Vulkan; quando descobríamos, já tinha se comprometido | `125c088` |
| Processo morria ao fechar (`0xC0000409`) | destrutores estáticos do PPSSPP; o app encerra sem rodá-los | `6db600a` |
| Falhas engolidas sem registro | sete `__except` vazios e uma morte de thread silenciosa | `ff391d4` |
| Índices de menu fixos | 28 sites agora acham a linha pelo `action_id` | `03eaa97` |
| Thread escrevia em pilha liberada | captura por referência numa thread destacada, na verificação de lock | `41e0bc3` |
| 25 MB de cores mortos no release | 3 cópias e 1 core abandonado que nada carregava | `1974d8e` |
| Chaves do 3DS rejeitadas | `aes_keys.txt` com CRLF: toda chave de 32 caracteres chegava com 33 | — |
| Amiga CD32 parava na tela de disquete | faltava `kick40060.CD32.ext`; o guia afirmava que CD32 estava coberto | `02bee22` |

Ferramentas que sobraram do caminho:

- **`tools/stress_cores.ps1`** — carrega, desliga e recarrega cada sistema N
  vezes. Separa "o core recusou" de "o processo morreu", que são coisas
  diferentes: a primeira pode ser romset errada, a segunda nunca é aceitável.
- **`tools/check_bios.ps1`** — confere cada BIOS contra o MD5 do guia e
  reconhece ponteiro do Git LFS, que já passou por BIOS duas vezes.
- **`tools/make_icon.py`** — gera o ícone do app por código.

### Uma lição que vale mais que os consertos

Em três dos quatro sistemas quebrados, **a primeira hipótese estava errada** —
`TerminateThread` foi culpado pelo que era spin, `retro_unload_game` pelo que
era outra coisa, e a DLL mapeada pelo que não explicava o PS2. O que resolveu
foi medir: CPU por amostragem para separar trava de laço, log por etapa para achar
qual das quatro chamadas prendia, contagem de disparos para descobrir que o
watchdog era 100% e não ocasional.

E a bateria mentiu três vezes antes de ser confiável — argumentos sem aspas
reprovaram 16 sistemas bons, leitura de log por offset acusou dois crashes que
nunca houve, e um guarda contra execução dupla se enxergava a si mesmo. **Teste
que mente é pior que teste nenhum.** Quando um teste reprova quase tudo, a
primeira suspeita é o teste.
