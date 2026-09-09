# Pendências e diagnósticos em aberto

Escrito em 09/09/2026, no fim de uma sessão longa, para que o trabalho em curso
não se perca. Cada item traz **a evidência que sustenta o diagnóstico**, não só
a conclusão — se alguém discordar, dá para refutar olhando a mesma coisa que eu
olhei. Os números de linha valem para o commit `95fc167`; confira o contexto ao
redor antes de editar, porque eles andam.

**Atualizado em 09/09/2026.** Três dos quatro itens foram corrigidos no mesmo
dia. O diagnóstico de cada um fica registrado abaixo do respectivo cabeçalho —
não porque ainda seja pendência, mas porque saber *por que* algo quebrou vale
mais do que saber que foi consertado, e esses três voltam se alguém mexer na
cadeia de cores ou no `CB_InputState` sem saber disto.

---

## 1. ~~Entrada duplicada: um botão faz duas coisas~~ — CORRIGIDO (`c665689`)

> A ponte agora só vale para MSX, C64, ZX Spectrum, DOS e Amiga, decidida uma
> vez no load. **Confirmado em jogo:** joystick no arcade voltou ao normal.
> Falta confirmar o outro lado — que o controle ainda digita num jogo de MSX.

### Diagnóstico original

**Sintoma relatado, e reproduzido pelo dono do projeto no Altered Beast (MAME):**
no gameplay, um botão do controle soca *e* pula ao mesmo tempo. No menu interno
do MAME (tecla TAB), a seta do teclado anda duas casas e parece "voltar"; pelo
controle o mesmo menu funciona normal.

**Causa.** `src/core_runner.cpp:2591`, dentro de `CB_InputState`:

```cpp
// Also bridge Joypad buttons to common keyboard keys so gamepad players can
// drive keyboard-controlled computer games (MSX, C64, ZX Spectrum, etc.):
if (id == 273 && joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_UP]) return 1;
if (id == 32  && (joypad_buttons[0][B] || joypad_buttons[0][A])) return 1; // Espaço
```

São 8 regras dessas. Elas fazem o frontend responder **o mesmo input físico em
dois dispositivos libretro diferentes**: o botão B aparece como
`RETRO_DEVICE_JOYPAD` botão B **e** como `RETRO_DEVICE_KEYBOARD` tecla espaço.

Isso é inofensivo num core que consulta só um dos dois. O MAME consulta **os
dois** — no log da sessão ele registra `retro joystickprovider` e
`retro keyboardprovider` lado a lado. Resultado: cada botão chega duas vezes,
por caminhos diferentes, e o MAME executa as duas ações que tiver mapeadas.

O caso da seta é o mesmo problema espelhado: a tecla ↑ do teclado está ligada a
`JOYPAD_UP` pelo bind do usuário (`key_up=38` no `Config/karamelo.cfg`) **e** é
lida direto por `GetAsyncKeyState` algumas linhas acima. O menu recebe os dois e
anda duas casas.

**Por que a ponte existe.** Foi feita de propósito para MSX, C64 e ZX Spectrum,
onde o jogo só entende teclado e quem tem só controle ficaria sem jogar. A
intenção é boa; o erro é ela valer para **todos** os cores.

**Correção proposta.** Restringir a ponte aos sistemas que precisam dela. O
`core_runner.cpp` já sabe qual é o sistema carregado — `loaded_system_dir`
(declarado na linha 190, preenchido na 2796) guarda o nome da pasta em
`roms/`, então dá para ligar a ponte só para `MSX`, `C64`, `ZXSpectrum`, `DOS` e
o que mais fizer sentido, e desligá-la para arcade e consoles.

**Como verificar que foi corrigido.** Não basta compilar. Abrir o Altered Beast
pelo Arcade, apertar um botão só e confirmar que o personagem faz **uma** ação;
depois abrir um jogo de MSX e confirmar que o controle ainda digita. Os dois,
porque a correção pode consertar um e quebrar o outro.

---

## 2. Crash de arcade — CORRIGIDO EM PARTE (`7580e6d`, `02bee22`)

> Dois consertos fecharam os dois caminhos que levavam ao crash: a cadeia agora
> lembra qual core rodou cada romset (o reload virou uma tentativa em vez de
> quinze), e o Flycast saiu da fila geral — ele só emula Naomi/Atomiswave, e era
> nele que toda busca fracassada terminava.
>
> **Continua em aberto:** uma romset que não casa com nenhum core ainda percorre
> a cadeia inteira. Nada valida a romset antes de entregá-la, e nada impede um
> core de derrubar o processo. Resolver de verdade exige validação prévia ou
> rodar cores fora do processo.

### Diagnóstico original

**Evidência.** No `app/karamelo.log`:

```
[CRASH-WER] codigo=c0000374 modulo=ntdll.dll offset=...112165
            (relatorio do Windows - o processo morreu antes do nosso handler rodar)
```

`0xC0000374` é `STATUS_HEAP_CORRUPTION`. O Windows detecta e mata o processo por
*fast-fail*, que **não levanta exceção** — o `SetUnhandledExceptionFilter`
instalado em `src/main_win32.cpp:1359` nunca roda. Não é falha do handler; é
assim por design, e não existe handler em processo que pegue isso. A única razão
de aparecer no log é o leitor de Windows Error Reporting que roda no início da
execução seguinte.

**O que dispara.** Entregar ao core de MAME atual uma romset feita para outra
revisão de MAME. Ele não encontra os arquivos, entra no caminho de erro e
estoura o heap. Reproduzido com `altbeast.zip`.

**O agravante.** Em `src/core_runner.cpp:2823` a cadeia de tentativas é:

```cpp
const std::string default_arcade_cores[] = {
    "cores/arcade_fbneo.dll",   // <- este e o MAME 0.289, e o que corrompe
    "cores/mame2003.dll",
    "cores/mame2010.dll",
    "cores/dreamcast.dll"
};
```

E o roteamento em `src/menu.cpp:509` também devolve `arcade_fbneo.dll` como
padrão. Ou seja, **o core perigoso é sempre o primeiro a ser tentado**. Os
outros três falham de forma limpa e educada — `readroms failed`,
`MAME returned an error!`, `Unknown game` — e o app segue para o próximo. Só o
primeiro mata o processo. Toda romset que não casar com ele derruba o app antes
de chegar nos cores que talvez a rodassem.

**Correção proposta.** Inverter a ordem: tentar os que falham limpo primeiro e
deixar o MAME atual por último. Custo: um jogo que só ele emula demora mais para
abrir, porque carrega três DLLs antes. Benefício: para de derrubar o app.

**O que essa correção *não* resolve.** Se nenhum core aceitar a romset, a última
tentativa ainda é a perigosa e o app ainda morre. Resolver de verdade exigiria
validar a romset antes de entregar, ou rodar cores em outro processo — mudança
de arquitetura, não conserto pontual.

---

## 3. `arcade_fbneo.dll` não é FinalBurn Neo — EM ABERTO

**Evidência, três independentes:**

1. O core se identifica no log como `[CORE] 'MAME' v0.289 (24cffe76)`.
2. Todas as opções que ele declara começam com `mame_` (`mame_thread_mode`,
   `mame_cheats_enable`, `mame_softlists_enable`…).
3. O arquivo tem **372 MB**. O MAME atual tem esse porte; o FinalBurn Neo tem
   cerca de 60 MB. Para comparação, `mame2003.dll` tem 28 MB e `mame2010.dll`
   50 MB. Busca por `FinalBurn`, `FB Neo` e `FBNeo` no binário: nenhuma
   ocorrência.

**O que está errado por causa disso:**

- `CREDITS.md` lista `arcade_fbneo.dll` como "FinalBurn Neo". É falso, e o
  arquivo se diz "verificado pelo que o próprio core declara" — essa
  verificação não pegou este caso.
- Os comentários de roteamento em `menu.cpp` raciocinam sobre o que "o FBNeo não
  suporta" (ex.: "Midway Y/T-Unit & Williams games not supported in FBNeo") —
  premissa falsa, então a lógica precisa ser reavaliada, não só o texto.
- **O projeto não tem nenhum core FinalBurn Neo.** As quatro opções de arcade
  eram, na prática, três versões de MAME e o Flycast.

**Cuidado ao corrigir.** Renomear o arquivo para `mame.dll` quebraria instalações
existentes e o `download_cores.ps1`. Decidir antes se vale.

---

## 4. ~~Crash ao recarregar o mesmo jogo~~ — CORRIGIDO (`7580e6d`)

> Era o item 2. O log de uma sessão real mostrou: o jogo rodava no MAME 2010, o
> reload recomeçava do fbneo, falhava nos quatro candidatos e repetia — quatro
> rodadas, quinze cargas de core. Na quarta o Flycast bateu na própria asserção
> (`state == Init`) e matou o processo com `0xC000001D`. Não era suposição minha
> sobre o MAME; era outro core.

### Diagnóstico original

Relatado: abrir o jogo, sair, abrir de novo → trava. Não investigado. Suspeita
de ser o mesmo item 2 (o core de 372 MB sendo descarregado e recarregado), mas
**isso é suposição, não diagnóstico** — precisa de um log de uma sessão em que
o jogo tenha aberto com sucesso na primeira vez.

---

## Feito nesta sessão, para ninguém refazer

- Setting "Arcade Core" removido de Settings > Video (commit `95fc167`). O
  roteamento automático continua e é agora o único caminho. Os índices fixos de
  linha do menu abaixo dele foram ajustados.
- Contagens de cores corrigidas em `app_info.h`, `README.md` e `CREDITS.md`
  (commit `f219a54`): **45 arquivos, 41 motores**, medido por hash e não à mão.
- Ícone do app refeito por código em `tools/make_icon.py` (commit `c116f7e`).
- Briefing do site em `docs/SITE_SYNC.md`.

## 5. BIOS: dois arquivos ainda inválidos

`app/bios/panafz1.bin` tem 132 bytes — é um ponteiro do Git LFS, não a BIOS do
3DO, então o 3DO continua sem abrir nada. `ATARIOSB.ROM` tem o tamanho certo mas
MD5 de outra revisão do OS-B; o Atari 5200 tem as outras quatro ROMs corretas e
provavelmente roda mesmo assim.

Rode `tools/check_bios.ps1` antes de investigar qualquer "jogo não abre": ele
compara MD5, reconhece ponteiro de LFS pelo cabeçalho e mostra o que os cores
reclamaram no log. Foi ele que achou os dois stubs de 131 bytes que passavam
como arquivos presentes.

Resolvidos hoje por esse caminho: Amiga CD32 (faltava `kick40060.CD32.ext`, o
sintoma era ficar na tela pedindo disquete para sempre), Atari 5200, Lynx, PC-FX
e a BIOS japonesa de PlayStation.

## Decisões que dependem do dono

- **25 MB de cores mortos** em todo release: `pcsx2.dll`, `pcsx2_libretro.dll`,
  `play_libretro.dll` (cópias byte a byte de cores que continuam no pacote com
  outro nome) e `bluemsx.dll`. Nada no código os referencia. Detalhes em
  `CREDITS.md`.
- **O site publica 39 motores**, o correto é 41.
- **A marca antiga continua na arte** de 16 wallpapers mais o `karamelo_chef` e
  o `karamelo_arcade`. O `wallpaper_08` foi removido do projeto a pedido; a
  purga dele do histórico do git ficou por fazer.


---

## 5. PlayStation 2: crash na terceira carga, e a tela congela depois — EM ABERTO

Reportado com o app rodando de verdade, não pelo teste headless. O jogador
abriu um jogo de PS2, saiu, abriu outro, e na **terceira** carga do core na
mesma sessão:

```
Resetting host memory for virtual systems...
[CRASH] codigo=0xC0000005 modulo=ps2.dll offset=0xA94F4
[CRASH] escrita no endereco 0x7FFEA0000000
[CoreShutdown] Timeout aguardando core thread. Verificando module_op=0
[CoreShutdown] Matando core thread via TerminateThread...
[CoreRunner] o core nao encerrou sozinho; estado recuperado a forca
[CoreShutdown] Finalizado apos recuperacao forcada.
```

E o log **para aí**. A tela fica em `LOADING...` para sempre.

**Duas coisas separadas, e a segunda é a pior.**

**O crash.** Terceira inicialização do PCSX2 no mesmo processo. O padrão é o
mesmo que quebrou MAME e Flycast — DLL mantida mapeada, globais sobrevivem, a
inicialização seguinte reencontra estado morto. A diferença é que ali havia
prova e aqui não: tirar `ps2.dll` da lista de "não liberar" **não mudou nada**
que se pudesse medir, e a mudança foi desfeita. Ver o comentário no
`skip_free` para o raciocínio e para o teste que falta.

**O congelamento.** Não é falta de tratamento: `main_win32.cpp:1345` já reabre
o menu quando uma carga termina sem core rodando. A tela congela porque **a
thread principal travou**. `TerminateThread` mata a thread do core sem liberar
nada que ela segurava, e quem tentar o mesmo lock depois para para sempre. É
documentado da API, não é bug de uso.

Isso significa que **a recuperação forçada não é recuperável** no caso geral.
Enquanto o core rodar dentro do processo do app, todo crash de core que não
encerre sozinho pode levar a UI junto. As saídas de verdade são duas, e as
duas são mudança de arquitetura: não matar thread nenhuma (aceitar que um core
travado trava o app, e dizer isso ao jogador), ou rodar cores em processo
separado, onde a morte de um não alcança a interface.

**Por que a bateria headless não ajuda aqui.** PS2 e PSP falham nela antes de
chegar ao ponto que interessa - o PCSX2 pede contexto D3D12, cai para D3D11 e
trava; o PPSSPP exige negociação de contexto Vulkan que o app não implementa,
é recusado e crasha assim mesmo. Os dois precisam de janela. O que confirma ou
derruba a hipótese da DLL é carregar um jogo de PS2 três vezes seguidas no app
de verdade, com e sem a DLL liberada.
