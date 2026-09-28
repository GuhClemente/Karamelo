# Recompilados de Xbox 360 no Karamelo

Primeiro caso: **The Darkness** — [portingpete/The-Darkness-Recomp](https://github.com/portingpete/The-Darkness-Recomp)
(GPL-3.0, feito sobre o XenonRecomp). Id no Karamelo: `TheDarknessRecomp`.
Só Windows x64 (é o único build que o projeto publica).

> **Status em 27/09/2026: oculto.** O pipeline foi validado com um disco real
> (Title ID `545407EE`, Media ID `0F213645`, versão 1) e o jogo abre, mas o
> runtime do projeto ainda está instável demais para oferecer. A entrada
> continua em `src/port_runner.cpp` com `hidden = true`: **não aparece no menu
> Ports & Recomp, não entra em nenhuma contagem e não vai para o site**
> (`docs/SITE_SYNC.md`, seção 19). Para testar, pela linha de comando, dentro de `app/`:
>
> ```
> Karamelo.exe --prepare-port TheDarknessRecomp
> ```
>
> Isso baixa o port, extrai o ISO e gera os arquivos. Depois, rode
> `ports\TheDarknessRecomp\Launch.cmd`. Para liberar no menu:
>
> 1. tirar o `.hidden = true` da entrada em `src/port_runner.cpp`;
> 2. somar 1 às contagens (Windows) em `CREDITS.md`, `CLAUDE.md`, `AGENTS.md`,
>    `README.md` e `SITE_SYNC.md` (nova seção dizendo que voltou), e devolver
>    a linha à tabela principal do `CREDITS.md`;
> 3. voltar o guia `packaging/hidden-ports/Xbox360/LEIA-ME.txt` e a criação de
>    `roms\Xbox360` no `package_release.bat` (no Linux/macOS o port não existe:
>    se o guia voltar para `packaging/rom-folder-guides/`, os scripts de lá
>    precisam pular essa pasta).
>
> As instruções abaixo valem para quando ele voltar ao menu.

## Para o jogador

1. Coloque o `.iso` do **seu** disco em `roms/Xbox360/`. O nome do arquivo
   precisa conter `darkness` (ex.: `The Darkness (USA).iso`).
2. No menu **Ports & Recomp**, abra **The Darkness** (enquanto estiver oculto,
   use o `--prepare-port` acima).
3. Na primeira vez o Karamelo baixa o port (~43 MB), extrai o ISO (vários GB,
   alguns minutos, com porcentagem na tela) e gera os arquivos do jogo. Das
   próximas vezes abre direto.

Não é preciso Xbox 360 Image Browser, XexTool, PowerShell nem conferir hash à
mão — é exatamente o que o Karamelo substitui.

Controles (do projeto original): clique na janela para capturar o mouse,
**F1** mostra os controles, **F2** alterna a captura, **Esc** pausa/solta o
mouse, **Alt+Enter** tela cheia. Controle de Xbox funciona. Gráficos em
*Options > Video Settings* (salvos em `ports/TheDarknessRecomp/DarkRecomp.settings.ini`).

Espaço em disco: até o tamanho do ISO (os arquivos extraídos) mais ~12 MB. Depois de preparado, o ISO não
é mais lido e pode ser apagado ou movido.

### Mensagens possíveis

| Mensagem | O que fazer |
|---|---|
| `COLOQUE SEU ISO DE The Darkness EM roms/Xbox360` | Nenhum `.iso` com "darkness" no nome foi achado. |
| `EXTRAINDO ISO... NN%` | Normal na primeira abertura. |
| `FALHA AO EXTRAIR O ISO` | Imagem que não é XDVDFS (GOD, XBLA, zip) ou disco cheio. Detalhe no `karamelo.log`. |
| `DUMP NAO CONFERE COM O PORT (REVISAO DIFERENTE?)` | Seu disco é outra revisão. O port só roda a revisão com que foi gerado. |

O `karamelo.log` guarda tudo em linhas `[PORT-X360]`: Title ID, Media ID,
versão, tipo de compressão e os hashes gerados — o que o autor do port pede
num relato de problema. A saída do jogo em si vai para
`ports/TheDarknessRecomp/build_native/run/karamelo_runtime.log`.

Quem já tem a pasta do jogo extraída à mão pode copiá-la para
`ports/TheDarknessRecomp/Darkness/` (com `default.xex` na raiz): o Karamelo
pula a extração e só gera os dois arquivos. Quem já tem `basefile.exe` e
`_uncrypted.xex` gerados pelo XexTool também pode deixá-los lá — o Karamelo
usa os que encontrar.

## Como funciona

Tudo em `src/x360_dump.cpp` (formato) e `PrepareX360Game()` em
`src/port_runner.cpp` (fluxo). Roda numa thread de fundo, na mesma vaga do
download de ports, antes do launch.

1. **Extração do ISO** (substitui o *Xbox 360 Image Browser*). Leitor próprio
   de XDVDFS/GDFX: procura o descritor `MICROSOFT*XBOX*MEDIA` no setor 32 da
   partição do jogo, testando os deslocamentos conhecidos (0 para imagem só da
   partição, `0xFD90000` XGD2, `0x2080000` XGD3, `0x18300000` XGD1), e percorre
   a árvore binária de cada diretório. Nomes com `..`, barras ou caracteres
   inválidos no Windows abortam a extração. Cada arquivo é gravado como
   `.part` e renomeado no fim; um arquivo já presente com o mesmo tamanho é
   pulado, então uma extração interrompida continua de onde parou.
2. **Decodificação do `default.xex`** (substitui o XexTool). XEX2: a chave do
   arquivo (security info `+0x150`) é decifrada com AES-128-ECB pela chave
   retail pública (e pela devkit, se a retail não gerar um PE válido); o
   payload inteiro é decifrado com AES-128-CBC, IV zero. Compressão *basic*
   (blocos dados + zeros) é remontada direto; *normal* é a cadeia de blocos
   com SHA-1, cujos chunks são concatenados e passados ao decodificador LZX do
   libmspack (`third_party/libmspack/`, o mesmo que o Xenia usa). AES e SHA-256
   vêm do BCrypt do Windows.
3. **Saídas** — `basefile.exe` (equivalente a `xextool -b`: a imagem de
   memória inteira, arquivo = RVA) e `_uncrypted.xex` (equivalente a
   `xextool -e u -c u`). O `_uncrypted.xex` não é "o mesmo cabeçalho com o
   payload em claro": o XexTool **reconstrói o cabeçalho inteiro** a cada
   gravação — entradas opcionais em ordem de chave, security info movida para
   `0x18 + n*8 + 0x80`, blobs empacotados logo depois dela, import libraries
   encostadas no início do payload (alinhado em 4 KiB), *header digest*
   (SHA-1) recalculado e assinatura RSA zerada (não há chave privada retail).
   `-c u` é compressão *basic* (sem os trechos zerados): um XEX que já é
   *basic*, como o do The Darkness, mantém os próprios blocos; um XEX LZX é
   recortado em grânulos de 32 KiB. Esse layout foi portado do
   [XexTool-RE](https://github.com/RexxColder/XexTool-RE) (MIT), uma
   reimplementação *clean-room* do XexTool 6.3 verificada byte a byte contra o
   original. Conferido em 27/09/2026 com um dump real (Title ID `545407EE`,
   Media ID `0F213645`, versão 1): os dois hashes batem.
4. **Portão de hash.** O runtime do port recusa qualquer `basefile.exe` ou
   `_uncrypted.xex` cujo SHA-256 não seja o da geração AOT. Em vez de uma
   tabela fixa no Karamelo (que ficaria velha na próxima release), os hashes
   aceitos são lidos **do próprio `DarkRecomp.exe` instalado**, que os embute
   como texto hexadecimal (confirmado na v0.1.2: `a7ccd878…d535e` para o
   `.xex` e `180b7fc8…2d049` para o `basefile.exe`). As poucas escolhas que
   o layout deixa em aberto (blocos originais ou recortados, payload com ou
   sem preenchimento) são todas geradas e fica a que bate. Nada que não bata
   é gravado; o log mostra o hash gerado para comparação.
5. **Fallback opcional.** Se a geração nativa não bater e houver um
   `xextool.exe` do próprio usuário (em `Darkness/`, na pasta do port ou em
   `tools/`), ele é executado com os mesmos argumentos do README do projeto e
   passa pelo mesmo portão. O Karamelo não distribui o XexTool.

### Launch

A release traz `Launch.cmd` e um `DarkRecompPreview.exe` que só dispara o
jogo e sai na hora — o monitor de processo do Karamelo devolveria o menu com o
jogo ainda aberto. Por isso a entrada fixa o executável
(`exe_relpath = build_native/Release/DarkRecomp.exe`), roda a partir da pasta
do port (o `--game-dir` padrão `Darkness` é relativo a ela) e passa os mesmos
argumentos que o modo *play* do projeto: `--timeout-ms 0 --engine-preview`.
Sem `--timeout-ms 0` o runtime se fecha sozinho após 30 s. Som ligado é o
padrão. O `DarkRecomp.exe` é um programa de console: o Karamelo o abre sem
janela de console, com stdout/stderr no `karamelo_runtime.log`.

### Linha de comando

```
Karamelo.exe --install-port TheDarknessRecomp   # só baixa o port
Karamelo.exe --prepare-port TheDarknessRecomp   # baixa + extrai o ISO + gera os arquivos
Karamelo.exe --launch-port  TheDarknessRecomp   # tudo acima + abre o jogo
```

### Adicionar o próximo recompilado de Xbox 360

Uma entrada em `KnownPortDefs()` com `x360_game_dir` (pasta onde o runtime
espera o dump), `iso_keywords` (palavras do nome do `.iso`), `iso_dirs`
(subpastas de `roms/` onde procurar) e, se o
release não abrir "com o .exe e mais nada", `exe_relpath`, `launch_args` e
`console_log_relpath`. O portão de hash funciona para qualquer recomp que
embuta os SHA-256 em texto no executável, como o XenonRecomp faz.

## Testes

`tests/test_x360_dump.cpp` (roda no `compile_port.bat`/`run_tests.bat`), com
fixtures sintéticos — nenhum dado de jogo entra no repositório: árvore
XDVDFS com subpasta, recusa de nomes `..`, XEX *basic* cifrado com a chave
retail, XEX *normal* com um bloco LZX, o portão de hash (aceita a variante
certa, recusa outra revisão) e a leitura dos hashes embutidos num binário.

Validado de ponta a ponta com a release real v0.1.2: download pelo
`--install-port`, detecção do executável, mensagem de ISO ausente, e launch com
argumentos/pasta de trabalho/log corretos (o jogo carrega e recusa um
`basefile.exe` falso pelo hash, que é o comportamento esperado).
