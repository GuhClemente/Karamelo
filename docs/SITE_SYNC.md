# Briefing para quem mantém o site karamelo-emu.com

Este arquivo existe para manter o site e o aplicativo dizendo a mesma coisa.
Ele é versionado junto com o código, então **a versão sempre atual está em**:

```
https://raw.githubusercontent.com/GuhClemente/Karamelo/main/docs/SITE_SYNC.md
```

Se você é uma IA trabalhando no site: leia este arquivo inteiro antes de
escrever qualquer texto de página, e releia sempre que for publicar uma versão
nova do aplicativo. Onde este documento disser "não invente", significa que o
número ou a frase tem uma fonte de verdade em algum lugar e você deve buscá-la,
não estimá-la.

---

## 1. Identidade

| campo | valor |
|---|---|
| Nome completo | **Karamelo Emulador** |
| Nome curto | **Karamelo** |
| Domínio | **karamelo-emu.com** |
| Repositório | **https://github.com/GuhClemente/Karamelo** |
| Autor | Guh Clemente — YouTube [@GuhClemente](https://youtube.com/@GuhClemente) |
| Plataforma | **Windows x64, Linux x64 e macOS ARM64 (Apple Silicon) nativos** |

Use "Karamelo Emulador" na primeira menção de uma página, em títulos e no
`<title>`. Depois disso, "Karamelo" sozinho está correto e lê melhor.

### O nome antigo

O projeto se chamava **MiSTer 4 ALL** até 08/09/2026. Esse nome **não deve
aparecer em lugar nenhum do site**, em nenhuma variação (`MiSTer 4 All`,
`MiSTer4ALL`, `MiSTer_4_ALL`, `mister4all.com`). Se encontrar, remova.

Também não deve sobrar nada de nomes ainda mais antigos: **"MiSTer Flavor"**,
**"Sabor MiSTer"**, **"Sabor Mister Arcade Edition"**. Esses aparecem em nomes
de pacote de wallpaper e em texto de marketing antigo. Renomeie para algo sem
a marca antiga (ex.: "Pack de Wallpapers", "Wallpapers Arcade").

### Sobre citar "MiSTer FPGA"

Aqui é preciso cuidado, e a regra não é "apagar tudo":

- **Pode** mencionar o MiSTer FPGA como referência de inspiração visual, desde
  que o disclaimer da seção 5 esteja presente na mesma página.
- **Não pode** dizer que o Karamelo é um *port* do MiSTer, nem que o código é
  derivado do `Main_MiSTer`. **Isso é falso.** A interface e o OSD são
  implementação própria, escrita do zero — existe um documento detalhando
  arquivo a arquivo o que é e o que não é (`docs/FRONTEND.md` no repositório).
  Essa distinção deixou de ser cosmética quando o código foi aberto sob uma
  licença não-GPL: dizer "port" contradiz a licença.
- **Evite** usar o nome MiSTer como chamada principal da página. Frases como
  "A experiência do MiSTer FPGA no seu PC" apoiam a marca do projeto no nome
  de outro. Prefira descrever o que o Karamelo faz.

---

## 2. Números — não invente nenhum

Todos os números publicáveis vivem em **um único arquivo**, que é a fonte de
verdade:

```
https://raw.githubusercontent.com/GuhClemente/Karamelo/main/include/app_info.h
```

Leia de lá. Valores em 09/09/2026:

| macro | valor | como publicar |
|---|---|---|
| `APP_SYSTEM_COUNT` | 35 | "35 sistemas" |
| `APP_CORE_ENGINES` | 40 | "40 motores de emulação" — detalhamento em [MOTORES.md](MOTORES.md) |
| `APP_CORE_FILES` | 41 | **não publique** — é contagem de arquivos, não de emuladores |
| `APP_VERSION` | 0.9.4 | prefira ler do `version.json` (seção 3) |

**Publique sempre `APP_CORE_ENGINES`, nunca `APP_CORE_FILES`.** Os dois diferem
porque `n64_parallel.dll` e `n64.dll` são o mesmo arquivo sob dois nomes.
Anunciar 41 seria contar o mesmo emulador duas vezes.

A lista motor a motor, pronta para publicar, está em
[docs/MOTORES.md](MOTORES.md) — e ela avisa dos dois casos que nao se deve
inventar: o core de ColecoVision nao se identifica, e `arcade_fbneo.dll` e MAME
0.289, nao FinalBurn Neo.

> Esses números já ficaram errados em quatro lugares ao mesmo tempo — o site
> dizia 39, o README dizia 40, o `app_info.h` dizia 39 e o CREDITS dizia as
> duas coisas em parágrafos diferentes. Por isso: leia do `app_info.h`, não
> copie de outra página.

A quantidade de jogos em "Ports & Recomp" **não** está no `app_info.h` — o app
conta em tempo de execução. Se precisar publicar, conte as entradas de
`CREDITS.md` na seção de ports, ou omita o número.

---

## 3. Downloads e versão

### O manifesto é a fonte

```
https://karamelo-emu.com/downloads/version.json
```

Formato (exemplo real):

```json
{
    "version": "0.9.4",
    "title": "Karamelo v0.9.4",
    "release_date": "2026-09-11",
    "notes": "Lancamento oficial do Karamelo com 35 sistemas nativos e Auto-Update.",
    "force_full_package": false,
    "exe_url": "https://karamelo-emu.com/downloads/Karamelo.exe",
    "exe_size": 4038144,
    "exe_sha256": "4AA64512...",
    "zip_url": "https://karamelo-emu.com/downloads/Karamelo_v0.9.4_Win64.zip",
    "linux_bin_url": "https://karamelo-emu.com/downloads/Karamelo_linux",
    "linux_bin_size": 5380024,
    "linux_bin_sha256": "331E1BE1...",
    "linux_tar_url": "https://karamelo-emu.com/downloads/Karamelo_v0.9.4_Linux64.tar.gz",
    "macos_bin_url": "https://karamelo-emu.com/downloads/Karamelo_mac",
    "macos_bin_size": 1213512,
    "macos_bin_sha256": "ABC7BE57...",
    "macos_tar_url": "https://karamelo-emu.com/downloads/Karamelo_v0.9.4_macOS_arm64.tar.gz"
}
```

O site deve montar os botões de download **lendo esse arquivo**, nunca com o
número da versão escrito à mão no código da página. Assim uma release nova não
exige tocar no site.

⚠️ **Esse arquivo é lido pelo auto-update do aplicativo (Windows, Linux e macOS).**
Não altere nomes de campos nem quebre o JSON. Se ele quebrar, os usuários perdem
a atualização automática.

### O que oferecer para download

| arquivo | oferecer? |
|---|---|
| `Karamelo_v<versão>_Win64.zip` | **sim** — download principal para Windows (x64) |
| `Karamelo_v<versão>_Linux64.tar.gz` | **sim** — download principal para Linux (x64) |
| `Karamelo_v<versão>_macOS_arm64.tar.gz` | **sim** — download principal para macOS (Apple Silicon M1/M2/M3/M4) |
| `Karamelo.exe` | **não** ofereça avulso (usado pelo auto-update do Windows) |
| `Karamelo_linux` | **não** ofereça avulso (usado pelo auto-update do Linux) |
| `Karamelo_mac` | **não** ofereça avulso (usado pelo auto-update do macOS) |
| `Karamelo_Pack_BIOS.zip` | **não existe mais** |

O `Karamelo.exe`, `Karamelo_linux` e `Karamelo_mac` avulsos **não funcionam sozinhos**: eles
localizam `cores/`, `bios/`, `roms/` e `saves/` a partir da própria pasta. Quem
baixar só o executável fica sem os motores de emulação. Eles existem nas URLs
acima porque é onde o auto-update do próprio app busca atualizações.

### Como apresentar no site

Recomenda-se oferecer três botões ou abas claras:
- **Baixar para Windows (x64)** (apontando para `zip_url`)
- **Baixar para Linux (x64)** (apontando para `linux_tar_url`)
- **Baixar para macOS (Apple Silicon)** (apontando para `macos_tar_url`)

Para Linux, instrua o usuário a descompactar e executar:
```bash
tar -xzf Karamelo_v*_Linux64.tar.gz
cd Karamelo_v*_Linux64
./Karamelo
```

Para macOS (Apple Silicon M1/M2/M3/M4):
```bash
tar -xzf Karamelo_v*_macOS_arm64.tar.gz
cd Karamelo_v*_macOS_arm64
./Karamelo
```

O pack de BIOS **deixou de ser publicado em 09/09/2026**. Ele contém firmware
de console, material protegido por direito autoral, e publicá-lo é
redistribuição — a mesma coisa que o README proíbe para o repositório, só que
por outro canal. Foi retirado do servidor e dos scripts de upload.

Se o site ainda tiver link para ele, remova: a URL responde 404 e o arquivo não
volta.

### ROMs e BIOS

O site **não distribui** ROMs nem BIOS, e não deve linkar para lugares que
distribuam. Se houver uma página de ajuda de BIOS, ela explica quais arquivos
são necessários e onde eles ficam na pasta — não fornece os arquivos.

---

## 4. Licença

O código é livre sob a **GNU General Public License v3.0 (GPL-3.0)**:

```
https://github.com/GuhClemente/Karamelo/blob/main/LICENSE.md
```

⚠️ **A licença mudou em 09/09/2026.** O projeto passou um único dia sob a
PolyForm Noncommercial 1.0.0. **Todo texto do site que mencione PolyForm,
"não comercial", "uso comercial não é permitido" ou "código-fonte disponível"
está desatualizado e precisa sair** — inclusive nas metatags, nas keywords, no
`<title>` e no JSON-LD, onde a frase aparece várias vezes.

Como descrever, corretamente:

> Software livre sob GPL-3.0. Qualquer pessoa pode usar, estudar, modificar e
> redistribuir o código. Quem distribuir uma versão modificada é obrigado a
> publicar o código-fonte dela sob a mesma licença.

Agora **pode** escrever "open source" e "software livre" sem ressalva: a
GPL-3 é aprovada pela OSI e pela FSF, e o próprio GitHub passa a exibir o selo
da licença. A restrição comercial que existia deixou de existir — **não escreva
mais que o uso comercial é proibido**, porque virou falso.

O ponto que vale a pena explicar ao leitor, porque é o motivo da troca:

> A licença anterior proibia vender, mas permitia pegar o código, fechar e não
> devolver nada. A GPL-3 faz o contrário: vender é permitido, fechar não é.

Não descreva o app como "freeware": ele é **software livre**, que é outra
coisa. Freeware é grátis e fechado; isto é grátis, aberto, e obriga quem
modificar a continuar aberto.

### Terceiros

O app embute bibliotecas de terceiros com licenças próprias, listadas em
`THIRD-PARTY-NOTICES.md` no repositório. Se o site tiver página de créditos,
linke para lá em vez de reescrever a lista. Um ponto que costuma ser mal
entendido: o core de N64 `n64_gopher.dll` é **GPL-3.0** por conta própria. Isso
agora coincide com a licença do Karamelo, mas continua sendo decisão do projeto
dele — o core é um binário separado, carregado pela API libretro.

Os cores de emulação são projetos de terceiros, cada um com licença própria,
catalogados em `CREDITS.md`. O site não deve dar a entender que foram feitos
por este projeto.

---

## 5. Aviso legal obrigatório

Deve aparecer em qualquer página que mencione MiSTer FPGA, e no rodapé:

> O **Karamelo Emulador** é um projeto de software independente e **NÃO possui
> qualquer afiliação, vínculo ou endosso de Alexey Melnikov, do projeto oficial
> MiSTer FPGA ou de seus mantenedores**.

O aviso **não cita plataforma**, e isso é de propósito. A versão anterior dizia
"desenvolvido para o ecossistema Windows", o que amarrava um texto jurídico a
um detalhe de build: no dia em que sair um binário de Linux, o aviso passa a
ser impreciso justo onde ele precisa ser exato. O que ele afirma é a não
afiliação com o MiSTer FPGA, e isso independe de onde o app roda.

Cuidado para não confundir esta mudança com a seção 9.6: tirar a plataforma do
**aviso legal** é correto; anunciar o app como **multiplataforma** na chamada e
nas metatags continua sendo promessa até existir um binário de Linux ou macOS
para baixar. Uma coisa é não prometer nada; a outra é prometer o que ainda não
está pronto.

Complemento recomendado na mesma seção:

> Todos os motores de emulação utilizados são plugins externos independentes
> compatíveis com a especificação Libretro, desenvolvidos por suas respectivas
> comunidades e regidos por suas licenças originais.

---

## 6. Infraestrutura — leia antes de mexer no servidor

O site é um app **Next.js em modo `standalone`**, rodando em container pelo
Coolify. Duas coisas não óbvias, ambas descobertas do jeito difícil:

**A pasta de downloads é um bind mount.** `/data/downloads` no host está
montado em `/app/public/downloads` no container. Arquivo colocado em
`/data/downloads` já está dentro do container no mesmo instante — **não copie
nada com `docker cp`**, é redundante e vai parar no container errado.

**O Next.js standalone monta a lista de arquivos de `public/` no boot.** Isto é
a causa de 100% dos problemas de download que já apareceram:

| sintoma | causa |
|---|---|
| arquivo novo no disco dá **404** | apareceu depois do boot, não está na lista |
| arquivo apagado dá **500** | está na lista, sumiu do disco |

Em ambos os casos a correção é a mesma: **reiniciar o container do site**. Não
adianta mexer em permissão, cache ou proxy.

O script `fix_server.sh` (no repositório, e em `/root/fix_server.sh` no
servidor) faz isso: acha o container pelo domínio nos labels, reinicia, espera
voltar e testa cada arquivo por HTTP. **O nome do container muda a cada
restart** — nunca escreva o nome fixo em script nenhum.

---

## 7. Checklist a cada versão nova do aplicativo

1. Buscar `version.json` e confirmar que `version` subiu.
2. Confirmar que o `zip_url` responde **200** (`HEAD` basta, não baixe 300 MB).
3. Reler `app_info.h` e atualizar sistemas e motores **se mudaram**.
4. Conferir se a release correspondente existe em
   `https://api.github.com/repos/GuhClemente/Karamelo/releases/latest`.
5. Passar o olho no site procurando o nome antigo — MiSTer 4 ALL, Sabor MiSTer,
   mister4all.com.

---

## 8. Erros que já aconteceram, para não repetir

- **Publicar número de versão escrito à mão na página.** Ficou desatualizado no
  primeiro release. Leia do `version.json`.
- **Publicar 41 motores** porque foi copiado de outra página, quando a fonte
  dizia outra coisa. Leia do `app_info.h`. Já esteve 39 e já esteve 41; nunca
  esteve certo por cópia.
- **Oferecer o `.exe` avulso** como download principal. Não funciona sozinho.
- **Chamar de "port do MiSTer".** É falso e conflita com a licença.
- **Chamar de "open source"** sem ressalva. A restrição comercial impede.

---

## 9. Auditoria do site em 09/09/2026 — o que está errado hoje

Isto não é teoria: cada item abaixo foi lido na página publicada em
`https://karamelo-emu.com` nesta data, incluindo o HTML gerado (metatags e
JSON-LD), não só o texto visível. Ordem por gravidade.

### 9.1 O domínio antigo ainda está no código da página

`mister4all.com` aparece em, pelo menos:

| onde | valor atual |
|---|---|
| `<link rel="canonical">` | `https://mister4all.com` |
| `og:url` | `https://mister4all.com` |
| `og:image` / `twitter:image` | `https://mister4all.com/og-banner.jpg` |
| JSON-LD `WebSite.url` | `https://mister4all.com` |
| JSON-LD `SoftwareApplication.url` | `https://mister4all.com` |

Todos devem ser `https://karamelo-emu.com`.

Este é o item mais grave da lista, e não é cosmético: um `canonical` apontando
para outro domínio manda o Google indexar o domínio antigo e tratar o novo como
cópia. Todo o SEO do site está sendo creditado a um endereço que o projeto não
usa mais.

### 9.2 A versão está escrita à mão no JSON-LD

`SoftwareApplication.softwareVersion` diz `"0.9.3"`, e a metatag `keywords`
traz `Karamelo Emu v0.9.3`. O botão de download já lê o `version.json` e mostra
a versão certa — o JSON-LD não, e ficou uma versão atrás.

Ou o JSON-LD passa a ser gerado a partir do mesmo `version.json`, ou tire o
`softwareVersion` e o número das keywords. Versão escrita à mão sempre
atrasa; foi o primeiro erro registrado na seção 8.

### 9.3 O número de motores está errado em oito lugares

O site publica **41**. O correto é **40** (ver seção 2 — `APP_CORE_ENGINES`).
Lugares encontrados:

1. Card do topo: "35 Sistemas — Consoles, arcades e portáteis (41 motores)"
2. Comparativo: "35 sistemas nativos com 41 motores de emulação Libretro"
3. `<meta name="description">`
4. `og:description`
5. `twitter:description`
6. `keywords`: "41 motores Libretro" e "41 cores Libretro"
7. JSON-LD `SoftwareApplication.description`
8. FAQ "Todos os 35 sistemas funcionam?": "são 41 motores de emulação Libretro
   distintos no total"

Detalhe do 8: além do número, a frase "nenhuma entrada do menu aponta para um
emulador ausente" continua verdadeira e pode ficar.

**Atualizado em 11/09/2026:** são **42** jogos de "Ports & Recomp" — 39 até
09/09, +1 (Valkyrie Profile) e +1 (Wave Race 64) em 10-11/09, +1 (Pokemon
Snap) ainda em 11/09 (ver seções 10, 11 e 13). O número foi conferido contra
o `CREDITS.md`, que é sempre a fonte — essa tabela só cresce, não copie
nenhum número antigo (39, 40, 41) de outra página no futuro. É coincidência
infeliz que 39 já tenha sido, um dia, o número errado de motores — não
confunda os dois.

### 9.4 Motores creditados a quem não é

| card / seção | está publicado | correto |
|---|---|---|
| Capcom CPS 1/2/3 Arcade | FinalBurn Neo | **MAME 0.289** |
| Galeria, Arcade Classics | "MAME / FB Neo" | **MAME** |
| Galeria, SNK Neo Geo | "Geolith / FinalBurn Neo" | **Geolith** (AES/MVS), **NeoCD** (CD) |
| PlayStation 2 | Play! v0.77 | **LRPS2 (PCSX2)** é o padrão; Play! é o alternativo |
| ColecoVision | Gearcoleco | **não publique nome de motor** — o core não se identifica |
| Atari 5200 | a5200 | **Atari800** |
| Game Boy & Color | "Gambatte / SameBoy" | **Gambatte** — SameBoy não é distribuído |
| Galeria, NES | "Mesen / FCEUmm" | **Mesen** — FCEUmm não é distribuído |
| Galeria, Master System | "Gearsystem / PicoDrive" | **Gearsystem** — PicoDrive é o core de 32X |
| Nintendo 64 | ParaLLEl N64 / Mupen64Plus-Next / build genérico | falta o **Gopher64**, que é o terceiro selecionável |
| Neo Geo Pocket | Mednafen NGP | **Beetle NeoPop** |
| WonderSwan | Mednafen WonderSwan | **Beetle WonderSwan** |
| PC-FX | Mednafen PC-FX | **Beetle PC-FX** |

**O projeto não distribui FinalBurn Neo, em lugar nenhum.** O arquivo se chama
`arcade_fbneo.dll` por herança, mas é uma build do MAME 0.289 — confirmado por
três evidências independentes em [MOTORES.md](MOTORES.md). Toda menção a FBNeo
no site precisa sair.

### 9.5 Números de versão de core que ninguém verificou

Os cards publicam coisas como "bsnes v115", "Mesen v0.9.9", "Stella v8.0",
"Geolith v0.4.1", "melonDS v0.9.5", "PPSSPP v1.17", "Flycast v2.3",
"Opera v1.0.0", "PUAE v5.3.0", "Play! v0.77".

O [MOTORES.md](MOTORES.md) **não publica versão de core de propósito**: o único
número que o projeto consegue provar é o que o próprio core declara ao carregar,
e isso só foi levantado para o MAME. Publicar versão de core é assumir uma
dívida de manutenção que ninguém vai pagar — na primeira atualização de core o
site fica mentindo em vinte lugares.

Recomendação: tire as versões, deixe só o nome do motor. Se quiser manter
alguma, mantenha só as que puder conferir e assuma que vai ter que revisá-las
a cada release.

### 9.6 Promessas de plataforma

O `<title>`, a `description`, as `keywords` e o JSON-LD descrevem o app como
**multiplataforma**, com "Linux e macOS a caminho", e o `operatingSystem` do
JSON-LD lista os três sistemas.

Hoje existe **um** binário: Windows x64. A migração para SDL3 é real e torna
Linux e macOS possíveis, mas possível não é disponível. O risco aqui não é
técnico, é de confiança: quem chega pelo termo "emulador multiplataforma" e
encontra só um .zip de Windows sente que foi enganado.

Formulação segura, que diz a verdade sem perder o argumento:

> Nativo para Windows x64. A base é SDL3, a mesma camada multiplataforma usada
> por projetos como o RetroArch, o que abre caminho para Linux e macOS.

O FAQ "Vai ter versão para Linux e macOS?" já está bem escrito e pode ficar
como está — ele diz "hoje o build oficial é Windows", que é exatamente o tom
certo. O problema é o título e as metatags venderem o que o FAQ desmente.

### 9.7 GameCube — reverificar antes de publicar

O card diz **"EM DESENVOLVIMENTO — ainda não roda"**, com a explicação do
contexto OpenGL compartilhado entre threads.

Na bateria automatizada de 09/09/2026 o GameCube **carrega, descarrega e
recarrega três vezes sem falhar**. Isso não é o mesmo que jogar: a bateria não
renderiza quadros nem abre janela, então ela não refuta o texto do card.

**Não mude esse card com base neste parágrafo.** O que ele autoriza é uma
reverificação com janela aberta. Enquanto ninguém abrir um jogo e confirmar, o
texto pessimista é o correto — errar dizendo que não funciona é muito mais
barato que errar dizendo que funciona.

### 9.8 Menor

**Os três cards de arcade** — "Capcom CPS 1/2/3", "MAME 2003", "MAME 2010" —
são apresentados como sistemas separados. No aplicativo eles são **um** sistema
("Arcade") com troca automática de core. Se alguém contar os cards para chegar
ao número de sistemas, vai achar 37 e não 35. Conte pelo `app_info.h`.

**O pack de wallpapers** (`/wallpapers/Karamelo_Wallpapers_4K_Pack.zip`) tem
arte com **"SABOR MISTER" pintado na ilustração** — 16 wallpapers, mais duas
imagens do app. A marca antiga não deve circular; a arte precisa ser refeita ou
o pack repensado. É decisão do dono, registrada aqui para não parecer resolvido.

**O aviso legal da seção 5 existe** no FAQ e no JSON-LD, e está correto. Garanta
que ele também apareça **visível** na seção do comparativo com o MiSTer FPGA,
que é a página onde a marca de terceiro mais aparece.

### 9.9 O que está certo — não mexa

- Os **42** jogos de Ports & Recomp (atualizado em 11/09/2026 — ver seções
  10, 11 e 13),
  e o texto explicando que o app baixa só o binário de cada projeto direto do
  GitHub. Uma ressalva nova a partir de agora: nem todo projeto da lista é
  "código aberto" no sentido estrito — a maioria é, mas ao menos um
  (Valkyrie Profile) embute um motor sob licença não-comercial. Se o texto do
  site afirmar "código aberto" para a lista inteira, isso deixou de ser
  literalmente verdade; "de código aberto ou disponível gratuitamente" descreve
  melhor, sem prometer o que a licença de um projeto de terceiro não garante.
- A estrutura da seção de licença — **mas o conteúdo mudou**: o projeto agora
  é GPL-3.0, e toda menção a PolyForm ou a "uso comercial não permitido" tem
  que sair. Ver seção 4.
- O FAQ sobre não distribuir ROM/BIOS, e o de "vocês reescreveram os cores?".
- O botão de download lendo o `version.json`.
- A ausência de qualquer link para o pack de BIOS.
- O texto "recriados do zero em C++20" — é verdade, e é exatamente a distinção
  que a seção 1 exige que o site preserve.

---

## 10. Adição de 10/09/2026 — Valkyrie Profile em Ports & Recomp

Entrou uma linha nova em `CREDITS.md`: **Valkyrie Profile**, via
[Ed1z19/ValkyrieRecomp](https://github.com/Ed1z19/ValkyrieRecomp). É um PS1 de
dois discos, motor PSXRecomp — os outros 39 são quase todos N64Recomp.
Mecanicamente ele se comporta como qualquer outro da lista (baixa do GitHub,
sem nada empacotado no instalador do Karamelo), mas tem duas diferenças reais
que valem nota se o site descrever essa entrada especificamente, não só a
categoria como um todo:

1. **Não é "baixe e jogue" na primeira vez.** O zip não traz o jogo pronto —
   traz um assistente que exige Python 3 já instalado na máquina e baixa uma
   toolchain de compilação (cmake/clang) no primeiro uso, para gerar o
   executável do jogo a partir dos discos que o jogador fornecer. Todo o resto
   da categoria é executável pronto após a extração.
2. **Licença do motor:** o repositório do ValkyrieRecomp em si não declara
   licença própria, mas o motor que ele embute (PSXRecomp, de Matthew Stan) é
   **PolyForm Noncommercial 1.0.0** — a mesma família de licença que o
   Karamelo usou por um único dia antes de virar GPL-3.0 (seção 4). Isso não
   afeta a licença do Karamelo — é um binário de terceiro baixado sob demanda,
   nunca embutido —, mas significa que esse port específico não é livre para
   uso comercial, ao contrário do próprio Karamelo.

Não é motivo para tirar a entrada da lista nem para dar destaque negativo a
ela — é só para o texto do site não prometer, por causa desta uma linha, algo
que não é verdade para ela: nem "baixa e já roda", nem "todo mundo aqui é
código aberto sem ressalva". Se a página não descrever entradas individuais de
Ports & Recomp (a maioria não descreve), não há nada a mudar além da contagem
da seção 9.3/9.9.

---

## 11. Adição de 11/09/2026 — Wave Race 64 em Ports & Recomp

Mais uma linha em `CREDITS.md`: **Wave Race 64**, via
[elliotttate/wave-race-64-recomp](https://github.com/elliotttate/wave-race-64-recomp).
N64Recomp normal — baixa, extrai, roda, pede o `.z64` do jogador como qualquer
outro da categoria. Ao contrário do Valkyrie Profile (seção 10), este **não**
tem ressalva de licença: o `LICENSE` do repositório é MIT de verdade (o
GitHub mostra "Other" porque o arquivo tem um parágrafo extra de aviso antes
do texto padrão da MIT, o que confunde o detector automático — o texto em si
é MIT sem modificação).

Duas coisas que valem nota, se o site descrever essa entrada especificamente:

1. **Ainda é beta.** A descrição do próprio repositório diz isso, e as notas
   de release trazem ressalvas reais (ex.: "outras famílias de GPU no Windows
   permanecem não testadas"). Não é motivo para tirar da lista — vários outros
   projetos da categoria também são WIP —, só não descreva como "finalizado".
2. **Pacote bem maior que o normal.** O zip do Windows tem ~390 MB, contra
   dezenas de MB da maioria da tabela — ele embute um pacote de texturas HD e
   trilha sonora substituta junto com o jogo recompilado. Funciona igual,
   demora mais para baixar.

Se a página não descrever entradas individuais (o caso comum), a única
mudança necessária é a contagem da seção 9.3/9.9.

---

## 12. Filtro e ícone de SO em Ports & Recomp (11/09/2026)

Pedido direto do dono do projeto: a lista de "Ports & Recomp" no site deve
**filtrar por sistema operacional e mostrar um ícone de qual SO cada jogo
suporta** — 🪟 para Windows, 🐧 para Linux. A fonte de verdade agora é a
própria tabela em [CREDITS.md](../CREDITS.md), que ganhou uma coluna **SO**
com exatamente esses dois ícones, verificada em 11/09/2026 contra a release
mais recente de cada um dos 42 repositórios. Leia de lá — não estime, não
copie a lista abaixo sem checá-la primeiro (ela existe aqui só pra você não
se perder revisando, ver a ressalva no fim desta seção).

### O que "SO" significa aqui

**Não é sobre o motor recompilado em si — é sobre o que o Karamelo consegue
baixar e abrir automaticamente.** Alguns desses projetos publicam mais
plataformas do que as que contam aqui (ex.: o Valkyrie Profile também tem
build de macOS, o Wave Race 64 também) — isso não vira ícone, porque **o
Karamelo não roda em macOS**, então não existe um app Karamelo-para-Mac que
pudesse baixar aquele arquivo. 🐧 só aparece quando existe um `.zip` com
marcador explícito de Linux no nome da release (o app não arrisca baixar um
zip sem marcação, para não entregar um binário Windows que não abre) **e**
esse zip realmente contém um executável, não um pacote Flatpak (que não é
algo que o app consiga abrir sozinho — `Banjo 64` e `Sonic Unleashed
Recompiled` caem nesse caso: têm "linux" no nome do arquivo mas não contam),
**e** precisa ser especificamente um `.zip` - o extrator deste app não abre
`.tar.gz`/`.tar.xz`/AppImage, então `Pokemon Snap` também fica sem 🐧 apesar
de ter um build Linux de verdade, só que empacotado como `.tar.gz`.

### ⚠️ Antes de publicar o ícone 🐧 em qualquer lugar do site

A build Linux do Karamelo **ainda não é a versão publicada** em
`karamelo-emu.com` — confira isso primeiro, direto na fonte, não assuma:

```
https://karamelo-emu.com/downloads/version.json
```

Se esse arquivo **não tiver** os campos `linux_bin_url`/`linux_tar_url`
(formato descrito na seção 3), o Linux ainda não foi lançado publicamente e
a coluna 🐧 não deve aparecer em lugar nenhum do site ainda — nem no ícone
por jogo, nem prometendo "também disponível pra Linux". Assim que esse
arquivo passar a ter os campos de Linux, a filtragem abaixo já pode entrar.

### O que implementar, quando o Linux já estiver publicado

1. **Filtro por SO.** Se a página tiver alguma forma de o visitante escolher
   a plataforma (aba, toggle, ou detecção automática do SO de quem visita),
   a lista de Ports & Recomp exibida deve mostrar só as entradas com o ícone
   daquele SO — hoje isso é 42 no filtro Windows e 16 no filtro Linux.
2. **Ícone por jogo.** Onde quer que a lista apareça (card, tabela, grid),
   cada entrada mostra 🪟 e/ou 🐧 conforme a coluna SO do `CREDITS.md`. Se só
   tiver 🪟, não precisa dizer nada extra — é o padrão da categoria.
3. **Contagem.** Se o site publicar "N jogos suportam Linux" ou algo parecido,
   o número vem de contar as linhas com 🐧 em `CREDITS.md` no momento da
   publicação, nunca do número fixo "16" — essa tabela muda toda vez que um
   projeto de terceiro adiciona ou remove uma plataforma.

### Lista completa de 11/09/2026, para não se perder revisando

Confira sempre contra o `CREDITS.md` antes de publicar — isto aqui é uma
cópia de leitura rápida, não a fonte:

**🪟🐧 (16, funcionam nos dois SOs):** Zelda 64: Recompiled (OoT/MM), Goemon
64, Harvest Moon 64, Bomberman 64, Mega Man 64, Bomberman Hero, Zelda OoT
(Ship of Harkinian), Zelda MM (2 Ship 2 Harkinian), Star Fox 64 (Starship),
Star Fox (SNES, Enhanced), Mario Kart 64 (SpaghettiKart), Super Mario 64
(Ghostship), Super Mario 64 Coop Deluxe, Infinite Mario 64, Super Mario Bros.
Remastered, Valkyrie Profile.

**🪟 apenas (26, só Windows):** Dr. Mario 64, Dinosaur Planet, Snowboard
Kids 2, Pokemon Stadium, Banjo 64, Chameleon Twist, Quest 64, Perfect Dark,
Animal Crossing (GameCube), Banjo-Kazooie: Nuts & Bolts, Dragon Ball Z
Budokai, Jak & Daxter (OpenGOAL), LoD: Severed Chains, REDRIVER 2,
Castlevania: Symphony of the Night, Sonic 1 Forever, Sonic 3 A.I.R., Sonic
Unleashed Recompiled, Space Station Silicon Valley, Super Mario World, Super
Metroid, Viva Pinata: Trouble in Paradise, WipEout Phantom Edition, OutRun
(CannonBall DX), Wave Race 64, Pokemon Snap.

Essa divisão 16/26 vai ficar desatualizada com o tempo — cada projeto de
terceiro pode adicionar ou tirar uma plataforma a qualquer release deles, sem
avisar o Karamelo. Reverifique contra `CREDITS.md` (que por sua vez precisa
ser reverificado contra a API do GitHub periodicamente - não é uma tarefa
única).

---

## 13. Adição de 11/09/2026 — Pokemon Snap em Ports & Recomp

Mais uma linha em `CREDITS.md`: **Pokemon Snap**, via
[JackandBeans/Snap64Recomp](https://github.com/JackandBeans/Snap64Recomp).
N64Recomp normal, sem ressalva de licença (GPL-3.0, a mesma do Karamelo) e
sem exigir nada além do `.z64` do jogador.

Duas coisas que valem nota, se o site descrever essa entrada
especificamente:

1. **Precisa de Direct3D 12 no Windows** - mesmo requisito que o Wave Race
   64 (seção 11) já tem. Qualquer PC com Windows 10/11 e GPU dedicada dos
   últimos anos atende, mas não é universal como um core libretro em OpenGL.
2. **Tem build Linux de verdade, mas não conta pra este app** (ver a
   ressalva da seção 12) - o release publica `.tar.gz`, não `.zip`, e o
   próprio projeto chama esse build de "experimental... rodado num Steam
   Deck em modo Desktop e em lugar nenhum mais até agora". Não é motivo pra
   tratar a entrada como incompleta no Windows, só não prometa Linux pra
   ela.

Se a página não descrever entradas individuais (o caso comum), a única
mudança necessária é a contagem da seção 9.3/9.9 e a lista da seção 12.
