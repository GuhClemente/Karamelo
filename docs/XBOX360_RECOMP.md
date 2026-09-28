# Recompilados de Xbox 360 no Karamelo

Primeiro caso: **The Darkness** — [portingpete/The-Darkness-Recomp](https://github.com/portingpete/The-Darkness-Recomp)
(GPL-3.0, feito sobre o XenonRecomp). Id no Karamelo: `TheDarknessRecomp`.
Só Windows x64 (é o único build que o projeto publica).

## Para o jogador

1. Coloque o `.iso` do **seu** disco em `roms/Xbox360/`. O nome do arquivo
   precisa conter `darkness` (ex.: `The Darkness (USA).iso`).
2. No menu **Ports & Recomp**, abra **The Darkness**.
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
   memória, arquivo = RVA) e `_uncrypted.xex` (equivalente a
   `xextool -e u -c u`: os mesmos cabeçalhos com o payload em claro).
4. **Portão de hash.** O runtime do port recusa qualquer `basefile.exe` ou
   `_uncrypted.xex` cujo SHA-256 não seja o da geração AOT. Em vez de uma
   tabela fixa no Karamelo (que ficaria velha na próxima release), os hashes
   aceitos são lidos **do próprio `DarkRecomp.exe` instalado**, que os embute
   como texto hexadecimal (confirmado na v0.1.2: `a7ccd878…d535e` para o
   `.xex` e `180b7fc8…2d049` para o `basefile.exe`). Como o corte de zeros no
   fim e a regravação do cabeçalho do XexTool não são documentados, o
   Karamelo gera as poucas variantes plausíveis e fica com a que bate. Nada
   que não bata é gravado.
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
