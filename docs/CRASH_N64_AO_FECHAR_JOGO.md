# O crash do N64 ao fechar o jogo — o que era e como foi resolvido

**Resolvido em 07/09/2026.** Commit `d4ab32c`.

Este documento existe porque o bug era difícil de entender e ficou meses em
aberto. Se ele voltar, ou se aparecer algo parecido em outro core, comece aqui.

---

## O sintoma

Abrir qualquer jogo de N64, jogar um pouco, e escolher **Close Game** no menu.
O aplicativo inteiro morria na hora. Sem mensagem, sem erro na tela, e — o que
mais confundia — **sem nada no log**. O `karamelo.log` simplesmente parava no
meio, na linha `Chamando RetroUnloadGameGuarded...`, e acabava ali.

Esse silêncio era ele próprio uma pista. O aplicativo tem um capturador de
crashes que registra qualquer pancada. Ele não registrou nada porque o Windows
não estava mandando um erro comum: ele estava **matando o processo de propósito**,
por ter detectado corrupção de memória. Esse tipo de morte não passa por nenhum
capturador — nem o nosso, nem o do próprio core.

O Visualizador de Eventos do Windows confirmava: `STATUS_HEAP_CORRUPTION`,
código `0xC0000374`.

---

## O que era, em português

O bug estava dentro do **próprio código do gopher64** (o emulador de N64), em
`third_party/gopher64/src/device/rdram.rs`. Cinco linhas.

O N64 tem uma memória chamada RDRAM. O gopher64 reserva essa memória com uma
exigência especial: o endereço onde ela começa precisa ser múltiplo de 64 KB.
Isso não é frescura — a parte do emulador que desenha os gráficos entrega essa
memória direto para a placa de vídeo, e a placa exige esse alinhamento.

Só que, depois de reservar com essa exigência, o código guardava essa memória
numa estrutura comum, que **não sabe da exigência**. Quando chegava a hora de
devolver a memória, essa estrutura devolvia como se fosse uma reserva comum.

### A analogia

Imagine alugar uma vaga de garagem que precisa começar exatamente numa linha
pintada no chão.

O manobrista não consegue começar exatamente na linha, então ele estaciona um
pouco à frente — e deixa **um bilhete no chão, logo antes do carro**, anotando
onde a vaga de verdade começa.

Na hora de devolver, se você disser *"era uma vaga comum"*, ele não procura o
bilhete. Ele avisa a garagem: *"pode liberar a vaga que começa aqui, onde está o
carro"*. Mas não é ali que a vaga começa. O controle da garagem fica
inconsistente — e quebra na próxima pessoa que for estacionar.

É exatamente isso. O Windows guarda um "bilhete" com o endereço real logo antes
da memória alinhada, e só vai procurar esse bilhete se você devolver dizendo que
a reserva era alinhada. Devolvendo como comum, ele manda liberar um endereço que
**não é o começo do bloco**, e o controle de memória do processo quebra.

### Por que era tão difícil de achar

Três motivos, e vale registrar os três:

1. **A pancada acontecia longe da causa.** A memória era corrompida na hora de
   devolver, mas o Windows só percebe isso depois, numa operação seguinte. Então
   o erro aparecia num lugar que não tinha nada a ver.
2. **Parecia problema de placa de vídeo.** Como a RDRAM é entregue para a parte
   gráfica, a investigação começou por lá.
3. **O compilador estava apagando as pistas.** Uma otimização chamada LTO junta
   várias partes do código numa só e apaga as fronteiras entre as funções — o
   depurador não conseguia dizer onde a pancada estava. Desligar isso nas
   compilações de diagnóstico foi o que destravou tudo (commit `60ef5fe`).

---

## A correção

Um tipo novo, `AlignedBytes`, que **lembra o próprio alinhamento** e devolve a
memória do jeito certo. O resto do código não precisou mudar: ele continua
enxergando a RDRAM exatamente como antes.

Detalhes que importam se alguém for mexer nisso:

- Savestates antigos continuam carregando — o formato gravado não mudou.
- O alinhamento de 64 KB foi mantido, porque a parte gráfica precisa dele.

---

## O que **não** era (não refaça esses testes)

Antes de achar o problema de verdade, quatro hipóteses foram testadas de ponta a
ponta — cada uma recompilada e medida. **Nenhuma mudou nada:**

| Hipótese | Resultado |
|---|---|
| Liberar cedo demais o buffer da ROM que passamos ao core | Continuou crashando |
| Travamento entre threads na hora de fechar | Continuou crashando |
| Reservar o buffer da ROM no heap do processo em vez do heap do C++ | Continuou crashando |
| Compilar o aplicativo inteiro em `/MD` em vez de `/MT` | Continuou crashando |

A última é a mais importante de registrar: **compilar em `/MD` não resolveria
nada**. Isso significa que a escolha de manter o `/MT` — que é o que faz o
aplicativo ser um `.exe` único, sem depender de DLLs do Windows instaladas — está
certa e não precisa ser revista por causa disso.

A primeira hipótese, mesmo não sendo a causa, apontou um **bug de verdade** que
foi corrigido junto (commit `0f09904`): o buffer da ROM era liberado antes da
hora, o que a especificação do libretro proíbe. Nenhum core reclamava, mas o
próximo que guardasse o ponteiro em vez de copiar iria quebrar.

---

## Como testar se voltou

O aplicativo tem um modo sem interface gráfica que carrega um core, roda alguns
quadros e descarrega. Rode de dentro da pasta `app`:

```bash
Karamelo.exe --core-selftest cores\n64_gopher.dll "roms\Nintendo64\SEU_JOGO.n64"
```

Também vale testar com outro core antes, que é quando o bug era mais violento:

```bash
Karamelo.exe --core-selftest cores\gba.dll "roms\GBA\SEU_JOGO.gba" cores\n64_gopher.dll "roms\Nintendo64\SEU_JOGO.n64"
```

**Como ler o resultado:**

- Código de saída `0` = passou.
- Código `-1073740940` (`0xC0000374`) = o bug voltou.
- No `karamelo.log`, todo `Chamando RetroUnloadGameGuarded...` tem que ser
  seguido de `RetroUnloadGameGuarded concluido.`

Esse último ponto é o que realmente importa. Já aconteceu de um teste "passar"
só porque o processo saiu rápido demais, antes do Windows perceber a bagunça.
Se aparecer `Matando core thread via TerminateThread`, **não passou** — o core
travou e foi morto à força, mesmo que o código de saída seja `0`.

---

## Como o core corrigido chega no usuário

Este é um ponto que já causou confusão e vale deixar claro.

Todos os outros cores (GBA, SNES, Mega Drive...) vêm prontos da libretro pelo
`download_cores.ps1`. **O gopher64 não.** Ele é compilado aqui, do código-fonte
em `third_party/gopher64`, porque carrega correções nossas — inclusive esta.

A pasta `cores/` é ignorada pelo git, ou seja: **a DLL corrigida não viaja no
repositório, só o código-fonte viaja**. Quem clonar o projeto precisa compilar
o core, senão empacota uma versão antiga com o bug.

Para isso existe o `build_gopher64.bat` (commit `a4c59ba`). A ordem é:

```bash
build_gopher64.bat
```
```bash
package_release.bat
```

O primeiro compila o core e coloca em `cores/` e `app/cores/`. O segundo copia
`app/cores/*.dll` para dentro do pacote. Pulando o primeiro, o pacote sai com o
core velho.

---

## Uma armadilha da máquina de desenvolvimento

O **Application Verifier** pode estar ligado para o `Karamelo.exe` no
registro do Windows. Ele é uma ferramenta de caça-bugs que **mata o processo de
propósito** em qualquer deslize, e deixa os códigos de erro diferentes
(`0xC0000409` em vez de `0xC0000374`).

Se estiver investigando um crash e os números não baterem com este documento,
confira:

```bash
reg query "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\Karamelo.exe"
```

Se aparecer `GlobalFlag`, ele está ligado. Para testar como o usuário final vê,
basta copiar o exe com outro nome — a configuração é por nome de arquivo.

---

## Isso também afeta o gopher64 original

O arquivo `rdram.rs` veio do projeto original do gopher64, então **o bug existe
lá também**. Provavelmente ninguém viu porque no Linux e no macOS os
gerenciadores de memória não usam o truque do "bilhete", e a mesma falha passa
despercebida.

O patch pronto para enviar ao projeto original está guardado, mas **não foi
enviado** — decisão do dono do projeto.
