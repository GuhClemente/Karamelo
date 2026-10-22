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
| `APP_SYSTEM_COUNT` | 37 | "37 sistemas" |
| `APP_CORE_ENGINES` | 42 | "42 motores de emulação" — detalhamento em [MOTORES.md](MOTORES.md) |
| `APP_CORE_FILES` | 43 | **não publique** — é contagem de arquivos, não de emuladores |
| `APP_VERSION` | 0.9.77 | prefira ler do `version.json` (seção 3) |

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
    "version": "0.9.7",
    "title": "Karamelo v0.9.7",
    "release_date": "2026-10-04",
    "notes": "Lancamento oficial do Karamelo com 35 sistemas nativos e Auto-Update.",
    "force_full_package": false,
    "exe_url": "https://karamelo-emu.com/downloads/Karamelo.exe",
    "exe_size": 4283904,
    "exe_sha256": "97BB368909C280CE89C0E4B3704E4BE8A6ACA0CAFC801ED5B3D3735A81ACC47D",
    "zip_url": "https://karamelo-emu.com/downloads/Karamelo_v0.9.7_Win64.zip",
    "linux_bin_url": "https://karamelo-emu.com/downloads/Karamelo_linux",
    "linux_bin_size": 5495904,
    "linux_bin_sha256": "DDF53B8C27F0A8EA033CFA789DE72EDAD8E88791EF8A546ECBAADE6EAE6CD38B",
    "linux_tar_url": "https://karamelo-emu.com/downloads/Karamelo_v0.9.7_Linux64.tar.gz",
    "macos_bin_url": "https://karamelo-emu.com/downloads/Karamelo_mac",
    "macos_bin_size": 4245576,
    "macos_bin_sha256": "884A3F7E5852FBF486007DCC9029534DE6AE044BE96E783ED15DF12DD2C78B21",
    "macos_tar_url": "https://karamelo-emu.com/downloads/Karamelo_v0.9.7_macOS_arm64.tar.gz"
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

**Atualizado em 18/10/2026:** são **70** jogos de "Ports & Recomp" — 42 até
11/09, +5 em 20/09 (ver seção 15), +8 em 21/09 (Pokemon Red/Blue, Spider-Man,
F-Zero X, AeroGauge, Crash Bandicoot, Super Mario Strikers, Pikmin / Open Nectar,
Soulcalibur II / Ring Out — ver seção 16), +1 em 27/09 (Super Smash Bros. Melee /
Melee Unlocked — ver seção 18), +1 em 27/09 (Monster Hunter Portable 3rd /
Yakumo — ver seção 20), +1 em 29/09 (Star Fox Adventures / Foxhollow — ver
seção 23), +1 em 12/10 (Conker's Bad Fur Day / CBFD-Recompiled — ver seção 25),
+1 em 12/10 (Sly Cooper / ProjectCane — ver seção 26), +1 em 12/10 (Castlevania: Legacy of Darkness / cvlod_recomp — ver seção 27), +1 em 17/10 (BLACK / BLACK-XBERecomp — ver seção 30), e +8 em 18/10 (CPS-2 Recomp, Wayne Gretzky's 3D Hockey, Rush 2, Automobili Lamborghini, Road Rash 64, Jungle Strike, ICO, Star Wars Battlefront: Renegade Squadron — ver seção 33). The Darkness (Xbox 360) chegou a ser anunciado
em 27/09 e foi **retirado no mesmo dia** — não conta e não aparece (seção 19).
O número foi conferido contra o `CREDITS.md`, que é sempre a fonte, e não copie
nenhum número antigo (39, 40, 41, 42, 47, 55, 56, 57, 58, 59, 60, 61, 62) de outra página no futuro. É coincidência
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

- Os **70** jogos de Ports & Recomp (atualizado em 18/10/2026 — ver seções
  10, 11, 13, 15, 16, 18, 19, 20, 23, 25, 26, 27, 30 e 33),
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

## 12. Filtro e ícone de SO em Ports & Recomp (Atualizado em 21/09/2026)

Pedido direto do dono do projeto: a lista de "Ports & Recomp" no site deve
**filtrar por sistema operacional e mostrar um ícone de qual SO cada jogo
suporta** — 🪟 para Windows, 🐧 para Linux, 🍎 para macOS (Apple Silicon &
Universal). A fonte de verdade agora é a própria tabela em
[CREDITS.md](../CREDITS.md), que traz a coluna **SO** com esses três ícones,
verificada em 21/09/2026 contra a release mais recente de cada um dos 55
repositórios daquela data (+ Melee Unlocked e Yakumo, verificados em 27/09/2026). Leia de lá — não estime, não invente.

### O que "SO" significa aqui

**É sobre o que o Karamelo consegue baixar, extrair e abrir automaticamente em
cada plataforma.** O Karamelo roda nativamente em Windows (x64), Linux (x64) e
macOS (ARM64 Apple Silicon). O subsistema de ports (`src/port_runner.cpp`)
filtra dinamicamente os jogos disponíveis para a plataforma em execução:
- No **Windows**: todos os 70 ports possuem builds executáveis (.exe).
- No **Linux**: requer um `.zip`, `.tar.xz`/`.tar.gz` ou pacote `.AppImage` com binário Linux executável nativo direto ou binário avulso (`CrashBandicoot-Linux`). Total: 42 ports.
- No **macOS**: o app detecta e prioriza binários Mach-O nativos (ARM64 >
  Universal > Intel), extrai pacotes `.app` descompactados, zips, tarballs e imagens de disco `.dmg`
  (montadas e extraídas transparentemente via `hdiutil`). Hoje 31 ports possuem
  releases comprovadas e utilizáveis para macOS.

### O que implementar no site

1. **Filtro por SO.** Se a página tiver seletor de plataforma (aba, toggle, ou
   detecção do SO do visitante via `navigator.userAgent`), a lista de Ports &
   Recomp exibida deve filtrar conforme o SO selecionado:
   - **Filtro Windows (🪟)**: 70 jogos.
   - **Filtro Linux (🐧)**: 42 jogos.
   - **Filtro macOS (🍎)**: 31 jogos.
2. **Ícones por jogo.** No card, tabela ou modal de cada jogo, exibir os ícones
   dos SOs suportados (🪟, 🐧, 🍎) conforme a tabela de `CREDITS.md`.
3. **Contagem dinâmica.** Não fixe números mágicos no HTML; se exibir contadores,
   derive-os da contagem dos ícones em `CREDITS.md`.

### Resumo das compatibilidades (revisado em 18/10/2026 — ver seções 20, 21, 22, 27, 30 e 33)

Confira sempre no [CREDITS.md](../CREDITS.md) antes de publicar:

- **🪟🐧🍎 (27 jogos, rodam em todos):** Zelda 64: Recompiled (OoT/MM), Goemon 64,
  Harvest Moon 64, Banjo 64, Bomberman 64, Mega Man 64, Bomberman Hero, Zelda OoT (Ship of
  Harkinian), Zelda MM (2 Ship 2 Harkinian), Star Fox (SNES, Enhanced), Mario
  Kart 64 (SpaghettiKart), Super Mario 64 (Ghostship), Super Mario 64 Coop Deluxe,
  Infinite Mario 64, Sonic 3 A.I.R., Valkyrie Profile, Pokemon Snap,
  Super Mario Strikers, Diablo (DevilutionX), Fallout (Community Edition),
  F-Zero X (ganhou build Linux em 18/10),
  Monster Hunter Portable 3rd (Yakumo) — adicionado em 27/09/2026, ver seção 20,
  Star Fox Adventures (Foxhollow) — adicionado em 29/09/2026, ver seção 23,
  Conker's Bad Fur Day (CBFD-Recompiled) — adicionado em 12/10/2026, ver seção 25,
  Castlevania: Legacy of Darkness (cvlod_recomp) — adicionado em 12/10/2026, ver seção 27,
  CPS-2 Recomp (Capcom Arcade) — adicionado em 18/10/2026, ver seção 33, e
  ICO (PS2 PC Port) — adicionado em 18/10/2026, ver seção 33.
- **🪟🍎 (4 jogos, Windows e macOS):** Perfect Dark, Snowboard Kids 2,
  Space Station Silicon Valley, Wave Race 64.
- **🪟🐧 (15 jogos, Windows e Linux):** Star Fox 64 (Starship), REDRIVER 2,
  Super Mario Bros. Remastered, Super Mario World, Body Harvest,
  Diddy Kong Racing, Pokemon Red and Blue (reblue), AeroGauge, Crash Bandicoot,
  Pikmin (Open Nectar), Soulcalibur II (Ring Out),
  Sly Cooper (ProjectCane) — adicionado em 12/10/2026, ver seção 26,
  Rush 2: Extreme Racing USA — adicionado em 18/10/2026, ver seção 33,
  Automobili Lamborghini — adicionado em 18/10/2026, ver seção 33, e
  Road Rash 64 — adicionado em 18/10/2026, ver seção 33.
- **🪟 apenas (24 jogos, somente Windows):** Dr. Mario 64, Dinosaur Planet,
  Pokemon Stadium, Chameleon Twist, Quest 64, OutRun (CannonBall DX),
  Animal Crossing (GC), Banjo-Kazooie: Nuts & Bolts, DBZ Budokai,
  Jak & Daxter, LoD: Severed Chains, Castlevania: SotN, Sonic 1 Forever,
  Sonic Unleashed, Super Metroid, Viva Pinata, WipEout Phantom Edition,
  Star Wars: Dark Forces (TFE), Spider-Man (OpenSpidey),
  Super Smash Bros. Melee (Melee Unlocked) — adicionado em 27/09/2026, ver seção 18,
  BLACK (BLACK-XBERecomp) — adicionado em 17/10/2026, ver seção 30,
  Wayne Gretzky's 3D Hockey — adicionado em 18/10/2026, ver seção 33,
  Jungle Strike — adicionado em 18/10/2026, ver seção 33, e
  Star Wars Battlefront: Renegade Squadron — adicionado em 18/10/2026, ver seção 33. (The Darkness **não** entra — ver seção 19.)

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
2. **Atualização de 21/09/2026: agora também roda no Linux (🐧).** Quando
   esta entrada foi adicionada em 11/09, o instalador só sabia abrir `.zip`
   no Linux, e o release desse jogo só publica `.tar.gz` - por isso ficou
   Windows-only por dez dias. O suporte a `.tar.gz`/`.tar.xz` direto (seção
   16) resolveu isso; testado de ponta a ponta com `--install-port`, não só
   lendo os assets da release. O projeto ainda chama esse build de
   "experimental... rodado num Steam Deck em modo Desktop e em lugar nenhum
   mais até agora", o que vale como nota se o site descrever a entrada, mas
   não é mais motivo pra omitir o ícone 🐧.

Se a página não descrever entradas individuais (o caso comum), a única
mudança necessária é a contagem da seção 9.3/9.9 e a lista da seção 12.

---

## 14. Suporte Nativo a macOS (Apple Silicon ARM64) — 11/09/2026

O Karamelo agora possui compilação e suporte nativo completo para **macOS (Apple Silicon M1/M2/M3/M4)**.

### Resumo Técnico para o Site

1. **Arquitetura**: Nativo ARM64 (Apple Silicon). Não necessita de emulação x86 nem Rosetta 2 para o app principal.
2. **Motores de Emulação**: 39 cores Libretro compilados nativamente em `.dylib` ARM64. Testados e validados em bateria de stress (240 ciclos contínuos de abertura, carga de jogo, execução de quadros e encerramento com 0 falhas).
3. **Download Principal**: Pacote `Karamelo_v<versão>_macOS_arm64.tar.gz`, apontado pela chave `macos_tar_url` no `version.json`.
4. **Auto-Update macOS**: O aplicativo no macOS se auto-atualiza consultando `macos_bin_url`, `macos_bin_size` e `macos_bin_sha256` no `version.json`.
5. **Ports Recompilados no Mac**: O subsistema de PC Ports (`port_runner.cpp`) possui suporte a macOS completo:
   - Seletor com prioridade: **ARM64 nativo > Universal Binary (`x86_64 + arm64`) > x86_64**.
   - Extração inteligente de pacotes de ports no formato `.app` (`Contents/MacOS/`) e zips aninhados.
   - 25 ports compatíveis catalogados com o selo 🍎 (ver Seção 12 e `CREDITS.md`).
   - Suporte nativo a imagens `.dmg` através de montagem/desmontagem automática e segura via `hdiutil`, permitindo instalar jogos distribuídos em `.dmg` no macOS (como Diablo/DevilutionX e Fallout 1 CE).
6. **Detecção no Site**:
   - Recomenda-se detectar o SO do usuário (`MacIntel` com `navigator.maxTouchPoints > 0` ou `navigator.userAgent` contendo `Macintosh`) para destacar o botão **Baixar para macOS (Apple Silicon)** por padrão para usuários Apple.

---

## 15. Adição de 20/09/2026 — Expansão de Ports & Recompilados (47 Ports) e Validação dos 35 Sistemas

Em 20/09/2026, o catálogo de "Ports & Recomp" foi expandido de 42 para **47 títulos** com a adição de 5 projetos amplamente testados e validados:

1. **Body Harvest** (`danielgomesvieira2000/body-harvest-recomp` — 🪟🐧):
   - Motor N64Recomp nativo de Body Harvest (USA).
   - Suporte a widescreen com HUD ancorado, interpolação de alta taxa de quadros e suporte total a gamepads.
   - Suporta cópia automática de ROM (`body-harvest-recomp.exe` no Windows, `body-harvest-recomp.sh` no Linux).
2. **Diddy Kong Racing** (`ThatGuyMcd/DKR-R` — 🪟):
   - Motor N64Recomp / RT64 de DKR.
   - Suporte a widescreen, 60fps+, multiplayer online e suporte nativo a controles.
3. **Diablo (DevilutionX)** (`diasurgical/devilutionX` — 🪟🐧🍎):
   - Source port moderno e aprimorado de Diablo 1 e da expansão Hellfire.
   - Suporte nativo para **Windows**, **Linux** (`.tar.xz`) e **macOS** (imagem `.dmg` com binário Universal ARM64/x86_64 montada e extraída automaticamente via `hdiutil`).
   - Suporta gamepads modernos, resolução escalável e multiplayer.
4. **Star Wars: Dark Forces (The Force Engine)** (`TheForceEngine/TheForceEngine` — 🪟):
   - Engine moderna substituta para o clássico Star Wars: Dark Forces da LucasArts.
   - Suporte a renderização em alta resolução, mouse look e áudio aprimorado.
5. **Fallout (Community Edition)** (`alexbatalov/fallout1-ce` — 🪟🐧🍎):
   - Reimplementação open-source e nativa da engine do Fallout 1 original.
   - Roda perfeitamente em **Windows**, **Linux** e **macOS** (imagem `.dmg` Universal ARM64/x86_64) com suporte a resoluções modernas e controles.

### Aprimoramento no Seletor de Assets (`src/port_runner.cpp`)
- O seletor de assets do Karamelo foi atualizado para descartar automaticamente pacotes de símbolos de depuração (`debug-symbols`, `debug_symbols`, `-pdb`, `_pdb`, `symbols`). Isso garante que o instalador baixe sempre o arquivo de jogo executável em vez de pacotes complementares de PDB.

### Validação dos 35 Sistemas com ROMs Sintéticas
- Todos os 35 sistemas agora possuem cobertura de testes automatizada através de geradores de ROMs sintéticas válidas (`tools/generate_test_roms.py`), permitindo autotestes de ponta a ponta (`--core-selftest`) para Atari 5200, Lynx, WonderSwan, Neo Geo Pocket, PC-FX e 3DO com 100% de estabilidade e fechamento gracioso.

---

## 16. Adição de 21/09/2026 — Expansão de Ports & Recompilados (55 Ports) e Suporte a Executáveis Avulsos

Em 21/09/2026, o catálogo de "Ports & Recomp" foi expandido de 47 para **55 títulos** com a homologação de 8 novos ports provenientes de projetos da comunidade:

1. **Pokémon Red / Blue (reblue)** (`zolaware/reblue` — 🪟):
   - Recompilação nativa em C do clássico de Game Boy.
   - Release v1.2.1 com executável nativo Windows x64.
2. **Spider-Man (OpenSpidey)** (`GTTeancum/OpenSpideyPS1` — 🪟):
   - Recompilação nativa de Spider-Man 1 & 2 (PS1).
   - Suporte a alta taxa de quadros e controles modernos no Windows.
3. **F-Zero X** (`craigshaw/FZeroRecomp` — 🪟🍎):
   - Motor N64Recomp de F-Zero X.
   - Suporte nativo completo a **Windows x64** e **macOS Apple Silicon ARM64**.
   - Timing NTSC aprimorado independente de taxa de atualização de tela.
4. **AeroGauge** (`alondero/aerogauge-recomp` — 🪟🐧):
   - Motor N64Recomp de AeroGauge (N64).
   - Suporte nativo a **Windows** e **Linux** (Vulkan, 60fps+, Easy Turbo assist).
5. **Crash Bandicoot** (`Matteo842/CrashBandicoot-Launcher` — 🪟🐧):
   - Port nativo para PC de Crash Bandicoot 1 (PS1).
   - Widescreen 16:9 real nativo (sem esticar ou cortar geometria), suporte a 60/120/240/uncapped FPS e suporte a imagens de disco `.chd` e `.cue+.bin`.
   - Distribui binários executáveis diretos (`CrashBandicoot.exe` no Windows e `CrashBandicoot-Linux` no Linux).
6. **Super Mario Strikers** (`new-coke/strikers` — 🪟🐧🍎):
   - Port nativo completo de Super Mario Strikers / Mario Smash Football (GameCube).
   - Compatibilidade Triple Crown: **Windows** (.zip), **Linux** (.tar.gz) e **macOS** (.tar.gz ARM64).
   - Suporte a 60fps+, widescreen e compilação de shaders em segundo plano.
7. **Pikmin (Open Nectar)** (`SSunnKing/Open-Nectar---Pikmin-Native-PC-Mobile-Port` — 🪟🐧):
   - Port nativo de Pikmin 1 (GameCube).
   - Suporte nativo para **Windows** e **Linux**.
   - Suporte a texturas HD e modelos aprimorados, controles modernos via mouse e gamepad.
8. **Soulcalibur II (Ring Out)** (`jackpoison-prog/RingOut` — 🪟🐧):
   - Port nativo / recompilador de Soulcalibur II (GameCube).
   - Suporte nativo a **Windows** e **Linux**.
   - Suporte nativo a widescreen 16:9 real, netplay P2P e 60 FPS cravados.

### Suporte a Executáveis Avulsos e Tarballs Linux no Downloader (`src/port_runner.cpp`)
- O instalador automático agora reconhece assets que sejam executáveis diretos (como `CrashBandicoot.exe` ou binários ELF do Linux como `CrashBandicoot-Linux`), gravando-os diretamente em `ports/<id>/` com permissões adequadas de execução (`chmod 0755`), sem tentar forçar descompactação via `ArchiveExtractAll`.
- O matcher de assets do Linux (`PickLinuxAsset`) agora suporta `.tar.gz` e `.tar.xz`, além do tradicional `.zip`.

### Atualização de Compatibilidade: Super Metroid Recomp (`mstan/SuperMetroidRecomp` — 🪟🐧🍎)
- O port de **Super Metroid** agora conta com suporte nativo homologado e comprovado para **Linux** (`supermetroid-0.3.8-linux-x64.zip`) e **macOS** (`supermetroid-0.3.8-macos-arm64.zip` / `supermetroid-0.3.8-macos-x64.zip`).
- O executável nativo Mach-O ARM64 (`SuperMetroidSNESRecomp`) foi validado diretamente no macOS Apple Silicon.
- Com isso e o suporte a imagens `.dmg`, o total de ports sobe para **26 no Linux (🐧)** e **24 no macOS (🍎)**.

### Suporte a Imagens de Disco `.dmg` no macOS (`src/archive_helper.cpp`)
- Adicionado suporte nativo à montagem e extração transparente de imagens `.dmg` através do utilitário padrão do macOS `hdiutil attach/detach`.
- Os jogos distribuídos pela comunidade exclusivamente em `.dmg` para macOS (como **Diablo / DevilutionX** e **Fallout 1 Community Edition**) agora são baixados, montados, extraídos para `ports/<id>/` e desmontados automaticamente, com detecção do bundle `.app` interno e executáveis Mach-O universais (ARM64 + x86_64).

### Projetos Analisados e Não Adicionados nesta Rodada
- `GenryTheFox/Spider-Man-Edge-of-Time-PC-Edition`: O repositório retornou HTTP 404 (privado ou deletado). Não foi adicionado.

---

## 17. Adição de 21/09/2026 — Expansão Linux (32 Ports), Save States com Timestamps, Shaders CRT e Wizard de Controles

Nesta atualização, quatro melhorias significativas de experiência do usuário (UX) e catálogo foram implementadas diretamente no Karamelo:

### 1. Expansão de Ports no Linux (32 Ports) e Suporte a `.AppImage`
- O seletor de assets Linux (`PickLinuxAsset`) passou a aceitar pacotes executáveis `.AppImage` além de `.tar.gz`, `.tar.xz` e `.zip`.
- Com isso e a homologação dos releases oficiais com binários nativos de Linux, mais 6 títulos ganharam suporte oficial a Linux (🐧):
  - **Banjo 64** (`BanjoRecomp/BanjoRecomp` — 🪟🐧🍎): tarball nativo com binário ELF x86_64.
  - **Super Mario World** (`mstan/SuperMarioWorldRecomp` — 🪟🐧): pacote AppImage oficial.
  - **Diddy Kong Racing** (`ThatGuyMcd/DKR-R` — 🪟🐧): pacote AppImage oficial.
  - **Pokemon Red and Blue (reblue)** (`zolaware/reblue` — 🪟🐧): pacote AppImage oficial.
  - **Sonic 3 A.I.R.** (`Eukaryot/sonic3air` — 🪟🐧): tarball nativo com executável Linux.
  - **REDRIVER 2** (`OpenDriver2/REDRIVER2` — 🪟🐧): tarball nativo com executável Linux.
- Total no Linux salta de **26 para 32 ports compatíveis**.

### 2. Save States com Slots Visuais e Timestamps em Tempo Real
- O menu in-game agora conta com o seletor **"Slot de Save"**:
  - Exibe dinamicamente `< Slot N: DD/MM HH:MM >` (ou `Vazio`).
  - Setas Esquerda/Direita alternam os 10 slots (0 a 9) em tempo real, lendo e exibindo a data/hora exata em que o estado foi gravado.
  - O atalho **F2 / Salvar Estado** atualiza o rótulo do timestamp imediatamente na tela.
  - Adicionado o **"Gerenciador Slots >"** (`STATE_SAVESTATES`), listando todos os 10 slots com tamanho em KB/MB, data/hora e permitindo salvar, carregar ou alternar slots diretamente.

### 3. Menu Dedicado de Shaders CRT com Lista e Preview em Tempo Real
- Ao selecionar "CRT Shader" com Enter/A, abre-se a tela **"Shaders CRT"** (`STATE_SHADER_LIST`) listando todos os 10 presets visuais com descrições objetivas:
  1. `Off (Pixel Raw)`
  2. `Scanlines Leve (Scanlines)`
  3. `CRT Suave (TV Tubo)`
  4. `PVM Pro 600TVL (PVM 600TVL)`
  5. `Sony Trinitron (Trinitron Aperture)`
  6. `Arcade Shadow Mask (Shadow Mask)`
  7. `Scanlines 50% (Grade 50%)`
  8. `NTSC Composite (Composite)`
  9. `LCD Matrix (LCD Matrix)`
  10. `Tubo Curvado 3D (Tubo Curvo)`
- O shader ativo exibe um marcador `*` e navegar/selecionar qualquer preset aplica o efeito instantaneamente na tela com live preview e toast de confirmação.

### 4. Assistente de Mapeamento de Controles (Wizard) no OSD
- Na tela de controles (`Controller`), foi adicionada a opção **"Mapear Tudo (Passo a Passo) >"**.
- Ao ser ativado, o assistente guia o jogador automaticamente botão por botão (D-Pad Cima, Baixo, Esquerda, Direita, B, A, Y, X, L1, R1, Start, Select, L2, R2, L3, R3), exibindo a contagem progressiva `[N/16]` e o botão esperado em notificações na tela.
- Ao pressionar um botão, o binding é atribuído e o assistente avança automaticamente para o próximo. Ao concluir, salva a configuração e exibe mensagem de sucesso. Tecla ESC cancela a qualquer instante.

---

## 18. Adição de 27/09/2026 — The Darkness (primeiro recompilado de Xbox 360), Melee Unlocked e ISO do jogador lido automaticamente

> ⚠ **Corrigido no mesmo dia — leia a seção 19 antes desta.** The Darkness
> foi retirado do menu e **não deve ir para o site**. Os números atuais
> estão na seção 20 (**57**: Windows 57, Linux 33, macOS 25, já com o Yakumo),
> não os desta seção. A "Entrada 1" e o
> item 3 de "O que mudar no site" ficam só como registro — não publicar.

O catálogo de "Ports & Recomp" ia de 55 para **57 títulos** (57 Windows 🪟,
32 Linux 🐧, 24 macOS 🍎). Com a retirada do The Darkness, o total ficou em
**56** — ver seção 19.

### Entrada 1 — The Darkness (NÃO PUBLICAR — retirado, ver seção 19)

- **The Darkness (Xbox 360)** — [portingpete/The-Darkness-Recomp](https://github.com/portingpete/The-Darkness-Recomp) — 🪟 apenas.
- Recompilação estática do executável de Xbox 360 para C++ (XenonRecomp),
  renderizador Direct3D 11 próprio, áudio XMA, mouse + controle, saves.
  **Não é emulação.** Licença GPL-3.0.
- É o **primeiro jogo de Xbox 360** da lista — vale destaque se o site
  agrupar por console de origem (os outros vêm de N64, SNES, PS1, GameCube,
  Game Boy, Mega Drive, PC).
- Estado: o projeto só publica *prereleases* (v0.1.2 em 27/09/2026) e o
  próprio runtime se descreve como "development build, gameplay incomplete".
  Descreva como **experimental / em desenvolvimento**, nunca como "completo".
- Requisitos: Windows 10/11 64-bit, GPU com Direct3D 11.

### O diferencial para o site: "coloque o ISO e jogue"

O projeto original exige três ferramentas e passos manuais: extrair o disco
com o *Xbox 360 Image Browser*, rodar o *XexTool* do xorloser duas vezes em
PowerShell para gerar `basefile.exe` e `_uncrypted.xex`, e conferir SHA-256 à
mão. **No Karamelo nada disso é necessário.** O jogador coloca o `.iso` do
próprio disco em `roms/Xbox360/` e abre o jogo pelo menu; o Karamelo:

1. baixa o port do GitHub;
2. extrai o ISO sozinho (leitor XDVDFS/GDFX nativo, com % na tela);
3. descriptografa e descomprime o `default.xex` sozinho (AES + LZX nativos);
4. confere os hashes contra os que o próprio port exige antes de abrir —
   se o disco for de outra revisão, avisa em vez de abrir e fechar.

Texto curto sugerido para o card/modal (pt-BR):

> **The Darkness** *(Xbox 360 · experimental)* — recompilação nativa para PC.
> Coloque o ISO do seu disco em `roms/Xbox360` e abra pelo menu: o Karamelo
> extrai o jogo e prepara os arquivos sozinho — sem XexTool, sem Image
> Browser, sem linha de comando. Só Windows.

Pontos obrigatórios em qualquer texto do site sobre essa entrada:

- **O jogo não vem com o Karamelo.** O jogador precisa do dump do **próprio**
  disco (`.iso`). Não sugira nem linke fontes de ISO.
- Só uma revisão do disco é suportada pelo port (Title ID `545407EE`, Media ID
  `0F213645`); o Karamelo diz claramente quando o disco não é essa.
- A primeira abertura demora alguns minutos (cópia de vários GB); as
  seguintes abrem direto.
- Formatos: `.iso` (imagem completa XGD2/XGD3 ou só a partição do jogo).
  Pastas GOD/XBLA e `.zip` não.

### Entrada 2 — Super Smash Bros. Melee (Melee Unlocked)

- [Hero88go/melee-unlocked](https://github.com/Hero88go/melee-unlocked) — 🪟 apenas. Licença GPL-2.0.
- Recompilação estática do executável de GameCube do **Melee NTSC 1.02**
  (mais os códigos Gecko do Slippi) com renderizador Direct3D 12 (ou 11).
  A lógica roda a 60 Hz como no console; a imagem roda a qualquer taxa
  (FPS destravado com quadros intermediários reais, não interpolação de
  imagem). DLSS/DLAA, resolução interna até 8x, widescreen 16:9, adaptador
  de controle GameCube (WUP-028) e **Slippi online** contra jogadores do
  Slippi Dolphin.
- Estado: **beta** (release v0.7.1 em 27/09/2026). Descreva como beta.
- Não é afiliado ao Slippi, à Nintendo nem à HAL — se o site citar Slippi,
  deixe claro que é compatibilidade com o netplay do Slippi, não um produto
  deles.

Como o jogador usa no Karamelo: coloca o `.iso` do **próprio** disco Melee
NTSC 1.02 em `roms/GameCube/` (nome contendo "melee") e abre pelo menu. O
Karamelo baixa o port (~158 MB), lê o cabeçalho de cada ISO com "melee" no
nome e usa só o que for `GALE01` revisão 2 — ISOs 1.00, Training Mode ou 20XX
na mesma pasta são ignorados, e se só houver esses o aviso diz que o disco é
o errado. O ISO é lido no lugar (não é copiado). Primeira abertura: 15-30 s
compilando shaders. **F1** abre as configurações de PC do jogo.

Online exige uma conta Slippi, criada e logada uma vez pelo Slippi Launcher
(que também instala o driver do adaptador GameCube). Offline não precisa de
nada disso.

Texto curto sugerido para o card/modal (pt-BR):

> **Super Smash Bros. Melee — Melee Unlocked** *(GameCube · beta)* —
> recompilação nativa com FPS destravado, DLSS e Slippi online. Coloque o
> ISO NTSC 1.02 do seu disco em `roms/GameCube` e abra pelo menu. Só Windows.

### O que mudar no site

1. Contagens: ~~56 / Windows 56, Linux 32, macOS 24~~ — superado: use os
   números da seção 20.
2. Lista "🪟 apenas" da seção 12: agora **19 jogos**, com
   Super Smash Bros. Melee (Melee Unlocked). Sem The Darkness.
3. ~~Se houver página~~ **Não publicar enquanto The Darkness estiver fora
   (seção 19).** Registro: se houver página de "como adicionar jogos" ou "pastas de ROM": acrescentar
   `roms/Xbox360/` com a explicação acima — **não é um sistema emulado** (a
   contagem de **35 sistemas** e **40 motores** não muda), é só onde fica o
   ISO dos recompilados de Xbox 360.
4. Nada muda em BIOS: não há BIOS nem chave para o jogador fornecer.

Detalhe técnico completo (para FAQ ou página de desenvolvedor):
[docs/XBOX360_RECOMP.md](XBOX360_RECOMP.md).

---

## 19. Correção de 27/09/2026 — The Darkness retirado do menu e do site

> ⚠ **Números de Linux/macOS atualizados na seção 21** (Linux **32**, macOS
> **26**). O resto desta seção continua valendo.

**Remova do site tudo o que a seção 18 mandou publicar sobre o The Darkness.**
O port funciona (o Karamelo preparou o dump de um disco real e o jogo abriu),
mas o projeto original ainda está instável demais para oferecer. Ele saiu do
menu do aplicativo; o site acompanha.

O que fazer no site:

1. **Tirar o card/modal/linha do The Darkness** da lista de Ports & Recomp,
   inclusive qualquer destaque de "primeiro jogo de Xbox 360".
2. **Contagens:** sem o The Darkness, **56** jogos (Windows 56, Linux 32,
   macOS 24) — e, com o Yakumo da seção 20, **57** (Windows **57**, Linux
   **33**, macOS **25**). Lista "🪟 apenas": **19 jogos**.
3. **Não mencionar** `roms/Xbox360/`, ISO de Xbox 360, XexTool nem Xbox 360
   Image Browser em nenhuma página (FAQ, "como adicionar jogos", pastas de ROM).
4. Melee Unlocked continua — só o The Darkness sai.
5. Sistemas (**35**) e motores (**40**) não mudam.

O código e a documentação técnica continuam no repositório
([docs/XBOX360_RECOMP.md](XBOX360_RECOMP.md)); quando ele voltar, uma nova
seção aqui vai dizer. Até lá, a regra é simples: **The Darkness não existe no
site.**

---

## 20. Adição de 27/09/2026 — Monster Hunter Portable 3rd (Yakumo), primeiro recompilado de PSP

> ⚠ **Números de Linux/macOS atualizados na seção 21** (Linux **32**, macOS
> **26**). O resto desta seção continua valendo.

O catálogo de "Ports & Recomp" vai de 56 para **57 títulos**. Novos totais,
conferidos contra o `CREDITS.md`: **57 Windows 🪟, 33 Linux 🐧, 25 macOS 🍎**
— o Yakumo publica build para os três. Na lista "rodam em todos" (seção 12)
são agora **20 jogos**.

### A entrada

- **Monster Hunter Portable 3rd (Yakumo)** — [TeamGDB/Yakumo](https://github.com/TeamGDB/Yakumo) — 🪟🐧🍎.
- Recompilação estática do Monster Hunter Portable 3rd HD Ver. (PSP) para
  C++, rodando sobre uma reimplementação do sistema do PSP; gráficos Vulkan,
  SDL3. **Não é emulação.** Licença MIT.
- É o **primeiro jogo de PSP** da lista de Ports & Recomp — vale destaque se o
  site agrupar por console de origem.
- Estado: o próprio projeto diz "jogável" (vila, caçadas, saves, música,
  vídeos, multiplayer ad hoc), mas as versões são *alpha* e saem quase todo
  dia. Descreva como **alpha / em desenvolvimento**, nunca como "completo".
- Download de ~220 MB, feito pelo Karamelo na primeira abertura.

### O que o jogador precisa saber (use no card/modal)

- **Oficialmente em japonês** — o MHP3rd nunca saiu fora do Japão. Existe
  tradução para inglês feita por fãs, que funciona no Yakumo (o README do
  projeto cita o jogo com esse patch). O site pode mencionar que ela existe,
  mas **não deve linkar nem distribuir** o patch ou ISOs.
- **Precisa do ISO `NPJB-40001`** — o ISO de PSP que vem dentro da versão HD
  do PS3. O Yakumo confere o código do disco e o executável: ISO de UMD de PSP
  comum (ULJM), `.cso` ou executável alterado são recusados, com uma tela
  explicando o motivo; um patch que só troca textos (como a tradução) passa.
- Na primeira abertura, o **próprio Yakumo** mostra uma tela para escolher o
  ISO (funciona com controle), confere e prepara o jogo. Depois abre direto.
- Pacotes de textura HD no formato do PPSSPP funcionam.

Texto curto sugerido para o card/modal (pt-BR):

> **Monster Hunter Portable 3rd — Yakumo** *(PSP · alpha)* — recompilação
> nativa para PC com Vulkan, taxa de quadros destravada e multiplayer. Em
> japonês (há tradução em inglês feita por fãs). Na primeira abertura,
> escolha o ISO `NPJB-40001` do seu jogo.
> Windows, Linux e macOS.

### O que mudar no site

1. Contagens: **57** jogos em Ports & Recomp; filtro **Windows = 57**,
   **Linux = 33**, **macOS = 25** (seção 12).
2. Lista "rodam em todos" (🪟🐧🍎) da seção 12: **20 jogos**, com o Yakumo.
3. Sistemas (**35**) e motores (**40**) não mudam — o Yakumo não é um core.
4. **The Darkness continua fora** (seção 19). Não confundir: o número subiu
   por causa do Yakumo, não pela volta do The Darkness.

---

## 21. Atualização de 27/09/2026 — compatibilidade revisada: macOS 26, Linux 32

Uma revisão das releases atuais de todos os ports mudou os ícones de três
jogos. O total de **57** jogos (já com o Yakumo, seção 20) não muda; só os
filtros por SO. O 🍎 do Yakumo também foi confirmado num Mac: o `.dmg`
instala e o jogo abre nativo em Apple Silicon, na tela de setup.

| Jogo | Antes | Agora | Por quê |
|---|---|---|---|
| Pokemon Snap | 🪟🐧 | 🪟🐧🍎 | a v1.1.0 (27/09) é a primeira release com build macOS (Universal) — instalado e aberto num Mac Apple Silicon |
| Sonic 3 A.I.R. | 🪟🐧 | 🪟🐧🍎 | a release estável sempre teve `.dmg`; o Karamelo falhava ao extrair esse `.dmg` e agora não falha mais — instalado e aberto num Mac Apple Silicon |
| Super Metroid | 🪟🐧🍎 | 🪟 | a v0.3.9 (22/09) só publica Windows; as versões até a v0.3.8 tinham Linux e macOS. Volta a ter os ícones se o projeto voltar a publicar |

O que mudar no site:

1. **Filtros:** Windows = **57**, Linux = **32**, macOS = **26**.
2. **Ícones** dos três jogos acima conforme a coluna "Agora" (a mesma da
   tabela do [CREDITS.md](../CREDITS.md)).
3. **Listas da seção 12:** 🪟🐧🍎 = 21 jogos, 🪟🍎 = 5, 🪟🐧 = 11,
   🪟 apenas = 20 (já atualizadas lá).
4. Sistemas (**35**), motores (**40**) e o resto das seções 18, 19 e 20
   não mudam.

---

## 22. Atualização de 28/09/2026 — macOS sem Homebrew, auto-update no Mac e no Linux, ícone

Os pacotes de macOS e Linux foram gerados com as correções abaixo:

O que o site deve dizer (e corrigir, se disser outra coisa):

1. **Requisito do macOS:** Mac com **Apple Silicon** (M1 ou mais novo) e
   **macOS 11 (Big Sur) ou mais novo**. **Não precisa de Homebrew** nem de
   instalar nada: o executável já traz tudo o que usa. (Até a primeira
   publicação da 0.9.6 o binário exigia o SDL3 do Homebrew —
   se o site citava `brew install sdl3`, remover.)
2. **Atualização automática nos três sistemas.** O menu *Update* do
   Karamelo agora baixa, confere (SHA-256) e instala a versão nova também no
   macOS e no Linux, e reabre o programa — antes só funcionava no Windows.
   No macOS e no Linux também existe `./Karamelo --update` no terminal.
3. **Ícone:** o mesmo ícone do Karamelo do Windows aparece no macOS (Dock e
   no arquivo `Karamelo` no Finder) e no Linux (janela, e um atalho
   "Karamelo Emulador" no menu de aplicativos, criado na primeira vez que o
   programa abre).

---

## 23. Adição de 29/09/2026 — Star Fox Adventures (Foxhollow)

O catálogo de "Ports & Recomp" vai de 57 para **58 títulos** (base: a revisão
da seção 21). Novos totais,
conferidos contra o `CREDITS.md` e o `src/port_runner.cpp`: **58 Windows 🪟,
33 Linux 🐧, 27 macOS 🍎** — o Foxhollow publica build para os três. Na lista
"rodam em todos" (seção 12) são agora **22 jogos**.

### A entrada

- **Star Fox Adventures (Foxhollow)** — site oficial [foxhollow.dev](https://foxhollow.dev/),
  código em [JackPriceBurns/foxhollow](https://github.com/JackPriceBurns/foxhollow) — 🪟🐧🍎.
- Port nativo do Star Fox Adventures (GameCube, 2002) feito a partir da
  decompilação [SFA-Decomp](https://github.com/zcanann/SFA-Decomp) e do
  renderizador [Aurora](https://github.com/encounter/aurora). **Não é
  emulação.** Licença CC0-1.0.
- O projeto diz que foi testado e é **totalmente jogável** em Windows, Linux e
  macOS, e que tenta ser fiel ao original "com bugs e tudo". As versões saem
  com frequência (v1.0.7 em 29/09/2026). Pode ser descrito como jogável;
  não chame de "remaster" nem de "versão melhorada" — a proposta é fidelidade.
- No macOS é build **nativo Apple Silicon (arm64)**.
- Download pequeno (~8 MB no macOS, ~14 MB no Linux, ~19 MB no Windows).
- Suporte a mods existe (troca de assets tipo pacote de textura, patch de
  funções), documentado em foxhollow.dev/modding — o Karamelo não gerencia
  mods; quem quiser usa a pasta de mods do próprio jogo.

### O que o jogador precisa saber (use no card/modal)

- **Precisa do dump do próprio disco** de Star Fox Adventures, em `.iso` ou
  `.rvz`, na pasta `roms/GameCube/`, com "fox" e "adventures" no nome do
  arquivo (ex.: `Star Fox Adventures (USA).rvz`). O site não deve linkar nem
  distribuir ISOs.
- Versões aceitas: **USA 1.0 e 1.1, Europa 1.0 e 1.1, Japão 1.0** — o
  projeto diz "qualquer cópia serve".
- O Karamelo lê o disco no lugar, sem copiar nada, e baixa o jogo na primeira
  abertura. Não precisa do launcher do Foxhollow.

Texto curto sugerido para o card/modal (pt-BR):

> **Star Fox Adventures — Foxhollow** *(GameCube)* — port nativo para PC
> feito a partir da decompilação do jogo, fiel ao original. Coloque o ISO ou
> RVZ do seu disco em `roms/GameCube` e abra pelo menu.
> Windows, Linux e macOS (Apple Silicon).

### O que mudar no site

1. Contagens: **58** jogos em Ports & Recomp; filtro **Windows = 58**,
   **Linux = 33**, **macOS = 27** (seção 12).
2. Lista "rodam em todos" (🪟🐧🍎) da seção 12: **22 jogos**, com o Foxhollow.
3. Sistemas (**35**) e motores (**40**) não mudam — o Foxhollow não é um core.
4. Não confundir com a linha **Star Fox (SNES, Enhanced)** nem com
   **Star Fox 64 (Starship)**: são três jogos diferentes.
5. The Darkness continua fora (seção 19).

---

## 24. Lançamento da Versão 0.9.7 (04/10/2026)

Lançamento da versão **0.9.7** para **Windows (x64), Linux (x64) e macOS (Apple Silicon ARM64)**.

### Resumo das Novidades e Sincronização

1. **Catálogo de Ports Atualizado (58 Títulos):**
   - Entrada oficial de **Star Fox Adventures (Foxhollow)** para Windows, Linux e macOS (Apple Silicon arm64 nativo).
   - Contagens consolidadas: **58 Windows 🪟, 33 Linux 🐧, 27 macOS 🍎**.
   - Lista "rodam em todos" (🪟🐧🍎): **22 jogos**.
2. **Correção de ROMs Mega Drive (.md):**
   - O navegador de arquivos do menu agora detecta e lista ROMs de Mega Drive com extensão `.md` (identificadas pelo cabeçalho SEGA a 0x100/0x101), em vez de ocultá-las como arquivos Markdown.
3. **Build e Auto-Update Nativo no macOS e Linux:**
   - No macOS: executável 100% nativo ARM64 (Apple Silicon M1/M2/M3/M4) com SDL3 estático embutido, sem dependência de Homebrew. Requisito mínimo: macOS 11.0 (Big Sur).
   - Auto-atualizador funcional com checagem de hash SHA-256 no Windows, Linux e macOS (via menu *Update* ou `./Karamelo --update`).
   - Ícone oficial do Karamelo configurado no Dock e Finder do macOS, e no desktop/menu do Linux.
4. **Manifesto de Download (`version.json`):**
   - Atualizado para a versão `0.9.7` com URLs, tamanhos e hashes SHA-256 de todas as plataformas.
5. **Favoritos, Recentes, Presets de Controles e Localização (i18n):**
   - Menus de acesso rápido de **Favoritos** e **Recentes** (últimos 15 jogos jogados) no menu principal.
   - Marcação rápida de favoritos com tecla `F` / `I` ou botão `Y` no controle, exibindo indicador `* ` nos jogos favoritados.
   - Detecção e exibição detalhada de modelos de controles Bluetooth (DualSense PS5, DualShock 4/3, Switch Pro, Joy-Cons, Xbox One/Series, Xbox 360).
   - Presets rápidos de botões (MiSTer / Nintendo, Xbox Nativo, PlayStation, Arcade 6-Botões).
   - Perfis de remap de botões persistidos por sistema (`Config/remaps/<Sistema>.cfg`).
   - Opção de idioma alternável (Português / English) nas Configurações, persistido em `Config/karamelo.cfg`.

---

## 25. Adição de 12/10/2026 — Conker's Bad Fur Day (CBFD-Recompiled)

O catálogo de "Ports & Recomp" vai de 58 para **59 títulos** (base: a revisão da seção 23 e release 0.9.7). Novos totais conferidos contra o `CREDITS.md` e o `src/port_runner.cpp`: **59 Windows 🪟, 34 Linux 🐧, 28 macOS 🍎** — o CBFD-Recompiled publica builds oficiais para os três sistemas. Na lista "rodam em todos" (seção 12) são agora **23 jogos**.

### A entrada

- **Conker's Bad Fur Day (CBFD-Recompiled)** — código em [sciaschi/CBFD-Recompiled](https://github.com/sciaschi/CBFD-Recompiled) — 🪟🐧🍎.
- Recompilação estática nativa de Conker's Bad Fur Day (Nintendo 64, 2001) baseada no renderizador **RT64**. **Não é emulação.** Licença **GNU General Public License v3.0 (GPL-3.0)** (a mesma licença do Karamelo).
- Criado por **sciaschi**, com porte nativo para macOS (Apple Silicon ARM64) por **nitrostemp**.
- O projeto publica executáveis nativos pré-compilados em seus GitHub Releases oficiais:
  - **Windows (🪟)**: zip x64 com binário `ConkerRecomp.exe`, suportando Direct3D 12 e Vulkan.
  - **Linux (🐧)**: tarball `.tar.gz` x86_64 nativo com Vulkan, SDL2, GTK 3 e FreeType.
  - **macOS (🍎)**: zip contendo bundle `.app` com binário Mach-O ARM64 (Apple Silicon nativo). Requer macOS 15 (Sequoia) ou mais recente.
- **Filtragem de assets**: os lançamentos do upstream incluem um arquivo companheiro `ConkerRecompiled-V*-Mods.zip` com modificações opcionais (Skip Intro, Cheats, etc.). O subsistema do Karamelo filtra e ignora esse pacote companheiro automaticamente, garantindo que o download baixe o runtime completo do jogo.

### O que o jogador precisa saber (use no card/modal)

- **Precisa da ROM original americana (.z64)** de Conker's Bad Fur Day. O Karamelo e o projeto não distribuem nenhum arquivo proprietário de jogo.
- O jogador só precisa colocar o arquivo `.z64` (ex.: `Conker's Bad Fur Day (USA).z64`) na pasta `roms/Nintendo64/`.
- O Karamelo detecta a ROM automaticamente, passa o parâmetro `--rom` na inicialização e o port importa e salva os dados na pasta do usuário na primeira vez, permitindo jogar direto com controle ou teclado sem precisar de caixas de diálogo do sistema operacional.
- Hacks de ROM que apenas alteram assets (como a versão sem censura / Uncensored) também são compatíveis.
- **Gráficos e Controles**:
  - Câmera livre no analógico direito (Free Camera) ou no mouse.
  - FOV (campo de visão) ajustável em graus.
  - Mira com suporte a mouse e giroscópio.
  - Suporte a monitores widescreen (16:9) e ultrawide (21:9 e 32:9) com opção de barras pretas em cutscenes para preservar enquadramentos originais.
  - Suporte a pacotes de textura RT64 (`.rtz`) e GLideN64 HD (`.htc`).

Texto curto sugerido para o card/modal (pt-BR):

> **Conker's Bad Fur Day — Recompiled** *(Nintendo 64)* — port nativo para PC
> via recompilação estática com RT64, suporte a widescreen, câmera livre e 60 FPS.
> Coloque sua ROM americana (.z64) em `roms/Nintendo64` e abra pelo menu.
> Windows, Linux e macOS (Apple Silicon).

### O que mudar no site

1. Contagens: **60** jogos em Ports & Recomp; filtro **Windows = 60**, **Linux = 35**, **macOS = 28** (seção 12).
2. Lista "rodam em todos" (🪟🐧🍎) da seção 12: **23 jogos**, adicionando Conker's Bad Fur Day.
3. Sistemas (**35**) e motores (**40**) não mudam — CBFD-Recompiled é um port nativo, não um core.
4. The Darkness continua fora (seção 19).

---

## 26. Adição de 12/10/2026 — Sly Cooper and the Thievius Raccoonus (ProjectCane)

O catálogo de "Ports & Recomp" vai de 59 para **60 títulos** (base: release 0.9.7). Novos totais conferidos contra o `CREDITS.md` e o `src/port_runner.cpp`: **60 Windows 🪟, 35 Linux 🐧, 28 macOS 🍎**.

### A entrada

- **Sly Cooper (ProjectCane)** — código em [theclub654/ProjectCane](https://github.com/theclub654/ProjectCane) — 🪟🐧.
- Port nativo para PC de *Sly Cooper and the Thievius Raccoonus* (PlayStation 2, NTSC `SCUS-97198`), totalmente reconstruído em C++ através de engenharia reversa. **Não é emulação.**
- Autor: **theclub654**.
- Compatibilidade por plataforma nos releases oficiais do GitHub:
  - **Windows (🪟)**: pacote RAR com `Sly Cooper and the Thievius Raccoonus.exe` e o `File Extractor GUI` (.NET 9).
  - **Linux (🐧)**: pacote RAR com `ProjectCane-x86_64.AppImage` (construído contra Ubuntu 22.04 / glibc 2.35, compatível com distribuições modernas e Steam Deck) e scripts `Run File Extractor.sh` e `Run ProjectCane.sh`.
  - **macOS (🍎)**: **Não possui suporte upstream no momento.** O código de plataforma depende de chamadas Linux como `/proc/self/exe` e subsistema ALSA, sem backend Metal/macOS e sem presets CMake para Darwin. Por isso, a badge macOS **não** é exibida para este port.
- **Suporte a arquivos RAR no Karamelo**: o subsistema de ports foi atualizado para reconhecer e descompactar arquivos `.rar` e `.7z` nativamente (usando `tar.exe` / libarchive no Windows e `7z` no Linux), garantindo download e extração 100% automáticos.

### Extração de assets do jogo (File Extractor GUI)

- **Requer a ISO original de PS2** do jogo (*Sly Cooper and the Thievius Raccoonus* NTSC, `SCUS-97198`). O Karamelo e o projeto não distribuem nenhum asset proprietário da Sony.
- O jogo necessita das pastas de assets `Worlds` e `Cutscenes` extraídas ao lado do executável.
- **Fluxo automatizado no Karamelo**:
  - Quando o jogador seleciona o jogo pela primeira vez no menu e os assets ainda não foram extraídos, o Karamelo detecta a ausência da pasta `Worlds` e abre automaticamente a janela do **File Extractor GUI** (.NET 9), exibindo uma notificação amigável na tela: `"EXTRAIA SUA ISO NO FILE EXTRACTOR"`.
  - O jogador simplesmente aponta para sua imagem ISO de PS2 no Extrator e confirma a extração na pasta do port.
  - A partir da segunda inicialização (com `Worlds/` presente), o Karamelo abre diretamente o jogo pelo menu, sem nenhuma janela intermediária.

### Gráficos e Jogabilidade

- Renderizador OpenGL moderno com shaders embutidos no binário.
- Suporte a monitores Widescreen 16:9 sem distorção na interface 2D.
- Taxa de quadros alta / destravada (com física e tempos de mira de torretas corrigidos para taxas altas de FPS).
- Detecção aprimorada de controles (DualShock, DualSense, Xbox, etc.).
- Sincronização rítmica precisa no chefe Mz. Ruby e colisões fiéis à física original do PlayStation 2.

Texto curto sugerido para o card/modal (pt-BR):

> **Sly Cooper and the Thievius Raccoonus (ProjectCane)** *(PlayStation 2)* — port
> nativo para PC reconstruído em C++ via engenharia reversa. Suporte a widescreen,
> 60+ FPS, OpenGL e controles modernos. Requer extração de assets da ISO original
> de PS2 (NTSC SCUS-97198) via File Extractor GUI embutido.
> Windows e Linux (Steam Deck).

### O que mudar no site

1. Contagens: **61** jogos em Ports & Recomp; filtro **Windows = 61**, **Linux = 36**, **macOS = 29** (seção 12).
2. Badge de SO no card: **🪟 Windows** e **🐧 Linux** (não adicionar 🍎 para este port).
3. Na lista da seção 12: adicionado a **🪟🐧 (12 jogos, Windows e Linux)**.
4. Sistemas (**35**) e motores (**40**) permanecem inalterados — ProjectCane é um port nativo, não um core.

---

## 27. Adição de 12/10/2026 — Castlevania: Legacy of Darkness (cvlod_recomp)

O catálogo de "Ports & Recomp" vai de 60 para **61 títulos** (base: release 0.9.7). Novos totais conferidos contra o `CREDITS.md` e o `src/port_runner.cpp`: **61 Windows 🪟, 36 Linux 🐧, 29 macOS 🍎**. Na lista "rodam em todos" (seção 12) são agora **24 jogos**.

### A entrada

- **Castlevania: Legacy of Darkness (cvlod_recomp)** — código em [fliperama86/cvlod_recomp](https://github.com/fliperama86/cvlod_recomp) — 🪟🐧🍎.
- Recompilação estática nativa de *Castlevania: Legacy of Darkness* (Nintendo 64, 1999) utilizando a tecnologia **N64Recomp** e renderizador **RT64**. **Não é emulação.**
- Licença: **GNU General Public License v3.0 (GPL-3.0)** — exatamente a mesma licença do Karamelo.
- Autor: **fliperama86**.
- Suporte oficial aos 3 sistemas operacionais com binários pré-compilados em cada release do GitHub:
  - **Windows (🪟)**: zip x64 com `LodRecomp.exe` e DLLs de runtime (DirectX 12 / Vulkan, SDL2).
  - **Linux (🐧)**: tarball `.tar.gz` x86_64 nativo com executável `LodRecomp`.
  - **macOS (🍎)**: zip contendo pacote `.app` com binário Mach-O ARM64 nativo para Apple Silicon (M1/M2/M3/M4).
- O projeto conta com pipeline ativa e dezenas de releases de melhoria contínua de estabilidade e áudio.

### O que o jogador precisa saber (use no card/modal)

- **Requer a ROM original americana (.z64)** do jogo (*Castlevania: Legacy of Darkness* NTSC, código interno `CASTLEVANIA2`, ID `ND4E`).
- O jogador só precisa colocar o arquivo da ROM em `roms/Nintendo64/` (ex.: `Castlevania - Legacy of Darkness (USA).z64`).
- O Karamelo localiza a ROM automaticamente, copia e garante a presença de `rom.z64` na pasta do port e repassa o caminho como argumento posicional na inicialização do jogo, garantindo boot direto sem janelas modais de seleção.
- **Gráficos, Áudio e Controles**:
  - Renderizador RT64 com suporte a Widescreen 16:9 sem distorção.
  - Taxa de quadros alta e fluida (60 FPS).
  - Suporte completo a gamepads modernos via SDL GameController (Xbox, DualShock 4, DualSense, Switch Pro) e teclado.
  - Áudio RSP nativo recompilado ativado por padrão (`LOD_USE_RECOMPILED_AUDIO_RSP=ON`).
  - Menu de configurações acessível no jogo para ajustes de vídeo, áudio e controles.

Texto curto sugerido para o card/modal (pt-BR):

> **Castlevania: Legacy of Darkness (LodRecomp)** *(Nintendo 64)* — port nativo
> para PC via recompilação estática com N64Recomp e RT64. 60 FPS, widescreen,
> controles modernos e áudio RSP recompilado. Coloque sua ROM americana (.z64)
> em `roms/Nintendo64` e jogue diretamente pelo menu.
> Windows, Linux e macOS (Apple Silicon).

### O que mudar no site

1. Contagens: **61** jogos em Ports & Recomp; filtro **Windows = 61**, **Linux = 36**, **macOS = 29** (seção 12).
2. Badge de SO no card: **🪟 Windows**, **🐧 Linux** e **🍎 macOS** (Apple Silicon).
3. Na lista da seção 12: adicionado a **🪟🐧🍎 (24 jogos, rodam em todos)**.
4. Sistemas (**35**) e motores (**40**) permanecem inalterados — LodRecomp é um port nativo, não um core.

---

## 28. Lançamento da Versão 0.9.71 (12/10/2026)

Lançamento da versão **0.9.71** com foco na expansão expressiva do ecossistema de **PC Ports & Recompilados nativos** e aprimoramento no instalador e configurador automático de ROMs.

### Principais Novidades da v0.9.71

1. **Expansão do Catálogo de Ports & Recomp (61 Títulos Totais)**:
   - **Castlevania: Legacy of Darkness (LodRecomp)** — N64 recompilado nativo a 60 FPS, Widescreen e áudio RSP nativo (🪟🐧🍎).
   - **Sly Cooper and the Thievius Raccoonus (ProjectCane)** — PS2 recompilado nativo com extração automática da ISO via `File Extractor` (🪟🐧).
   - **Conker's Bad Fur Day (CBFD-Recompiled)** — N64 recompilado com ray tracing / RT64, suporte a mouse & teclado e gamepads modernos (🪟🐧🍎).
2. **Setup Transparente de ROMs e ISOs**:
   - O instalador headless (`--install-port <id>`) e o launcher da interface agora localizam automaticamente as ROMs/ISOs na biblioteca do jogador (`roms/Nintendo64/`, `roms/PlayStation2/`) e preparam os arquivos necessários sem intervenção manual.
3. **Pacotes e Manifesto**:
   - Pacote de Windows atualizado para `Karamelo_v0.9.71_Win64.zip`.
   - Manifesto `version.json` atualizado para a versão `0.9.71`.

---

## 29. Lançamento da Versão 0.9.72 (15/10/2026)

Lançamento da versão **0.9.72** com foco em estabilidade profunda, correção de bugs de concorrência e eliminação de falhas sutis em todas as plataformas (Windows, Linux e macOS).

### Principais Correções e Melhorias da v0.9.72

1. **Ciclo de Vida de Threads POSIX & Eliminação de Use-After-Free**:
   - Adicionada contagem de referências atômica (`refcount`) nos handles de thread da camada de compatibilidade Win32/POSIX (`compat_win32.h`).
   - Elimina crash/SIGSEGV em encerramentos de cores com timeout ou chamadas a `TerminateThread` no macOS e Linux.
2. **Aplicação Completa de Opções de Cores pelo Navegador**:
   - Correção no fluxo de seleção de ROMs do menu (`menu.cpp`): a função `ApplyPersistedCoreOptions()` agora é chamada também ao iniciar jogos pelo navegador de arquivos (`STATE_BROWSE`), garantindo que opções de stylus/touch e telas do Nintendo DS (melonDS), layout do Nintendo 3DS (Citra), áudio FM do Master System, precache do Sega CD e dip-switches/regiões do NeoGeo sejam aplicadas corretamente.
3. **Robustez no Monitoramento de PC Ports**:
   - Proteção contra interrupções por sinais (`EINTR`) no loop de `waitpid` (`port_runner.cpp`), evitando que o Karamelo seja restaurado prematuramente sobre um jogo ainda em execução no Linux e macOS.
4. **Correção no Cliente RetroAchievements (Linux/macOS)**:
   - Eliminação de falso código HTTP 200 quando requisições `curl` falham sem saída.
   - Envio correto do cabeçalho `User-Agent` oficial (`Karamelo/2.0 rcheevos/12.4.0`) em todas as requisições.
5. **Prevenção de Timeout Excessivo no Descompactador**:
   - Saída imediata de loops de espera do extrator transparente (`archive_helper.cpp`) em caso de erros não recuperáveis (`ECHILD`), prevenindo bloqueios de até 10 minutos.
6. **Limpeza e Compilação Limpa**:
   - Resolução de advertências do compilador com `-Wall -Wextra` e expansão da suíte de testes unitários para 37 testes.

---

## 30. Adição de 17/10/2026 — BLACK (Xbox Recomp) em Ports & Recomp

Mais uma linha em `CREDITS.md`: **BLACK** (original Xbox), via [TGP482/BLACK-XBERecomp](https://github.com/TGP482/BLACK-XBERecomp).

> **Aviso para a IA do site:** o catálogo de "Ports & Recomp" vai de 61 para **62 títulos**. Novos totais conferidos contra o `CREDITS.md` e o `src/port_runner.cpp`: **62 Windows 🪟, 36 Linux 🐧, 29 macOS 🍎**. Na lista "rodam em todos" permanecem **24 jogos**.

### O que o BLACK é

É a primeira recompilação estática de um jogo do **Xbox Original** a entrar oficialmente no menu do Karamelo! Desenvolvido por **TGP482** utilizando o SDK `XboxXBE-Recomp`, o port roda o clássico FPS militar da Criterion Games nativamente em Direct3D 12 no Windows, com resolução 4K, taxa de quadros destravada (ultrapassando os 30 FPS do console original), suporte a HDR10, MSAA/SMAA, SSAO/GTAO, Widescreen e controles/mouse com raw input.

### Como funciona para o jogador

O executável oficial aceita diretamente o arquivo de imagem `.iso` do Xbox original (versões USA e Europe) dentro da pasta `assets/`, montando o sistema de arquivos FATX virtualmente sem necessidade de programas externos para extração.
No Karamelo, o jogador coloca sua ISO de BLACK em `roms/Xbox/` (ou `roms/`) e o frontend cria automaticamente a ligação (hardlink/cópia) para `ports/BlackRecomp/assets/Black.iso` antes de iniciar.

### Card para o site

> **BLACK**
> FPS frenético da Criterion Games (Xbox original) reconstruído para PC via recompilação estática (XboxXBE-Recomp). Taxa de quadros destravada (60+ FPS), 4K, HDR10, Widescreen e suporte a mouse e controles modernos. Coloque sua ISO do Xbox em `roms/Xbox` e jogue diretamente.
> Windows (Direct3D 12).

### O que mudar no site

1. Contagens: **62** jogos em Ports & Recomp; filtro **Windows = 62**, **Linux = 36**, **macOS = 29** (seção 12).
2. Badge de SO no card: **🪟 Windows** (Direct3D 12).
3. Na lista da seção 12: adicionado a **🪟 apenas (21 jogos, somente Windows)**.
4. Sistemas (**35**) e motores (**40**) permanecem inalterados — BLACK é um port nativo, não um core.

---

## 31. Lançamento da Versão 0.9.73 (17/10/2026)

Lançamento da versão **0.9.73** trazendo suporte oficial ao PC port e recompilação estática de **BLACK** (original Xbox via `XboxXBE-Recomp`), elevando o catálogo de Ports & Recomp para **62 títulos**.

### Principais Novidades da v0.9.73

1. **Suporte Nativo a BLACK (Xbox Recomp)**:
   - Resolução e download automático via GitHub Releases (`TGP482/BLACK-XBERecomp`).
   - Leitor e vinculador transparente de ISO do Xbox: localiza o arquivo `.iso` em `roms/Xbox/` e cria hardlink instantâneo para `ports/BlackRecomp/assets/` sem duplicar espaço em disco.
   - Mensagens claras de toast no OSD quando a imagem do jogo não for localizada.
2. **Atualização de Versão e Manifestos**:
   - `include/app_info.h` atualizado para `APP_VERSION "0.9.73"` e `Karamelo_v0.9.73.exe`.
   - Manifesto `dist/version.json` atualizado para a versão `0.9.73`.
   - Suíte de testes unitários do auto-atualizador expandida para validação da `0.9.73`.

---

## 32. Lançamento da Versão 0.9.74 (18/10/2026)

Lançamento da versão **0.9.74** com correções críticas no fluxo de navegação do menu OSD, retorno suave ao Menu do Core durante o jogo e correção definitiva no reinício de jogos de Nintendo 64.

### Principais Novidades da v0.9.74

1. **Correção no Toggle do Menu OSD (F12)**:
   - Pressionar F12 com um jogo em execução agora sempre abre o **Menu In-Game do Core** (com *Carregar*, *Reiniciar*, *Proporção*, *Shader CRT*, *Opções Core*, etc.), corrigindo o comportamento onde o menu de sistemas era exibido incorretamente sobre o jogo.
   - Adicionada a opção **"Navegar Sistemas"** no Menu do Core para permitir explorar outros consoles sem fechar o jogo atual.
   - Navegar até o menu de sistemas enquanto um jogo está aberto agora exibe explicitamente a opção `< Voltar ao Core ([Nome do Core])` no topo da lista.
   - Pressionar `Esc` ou selecionar `<..>` na raiz dos sistemas retorna diretamente para o Menu do Core.

2. **Reboot Limpo no "Reiniciar" (N64 e demais plataformas)**:
   - Corrigido travamento e congelamento de tela ao selecionar "Reiniciar" em jogos de Nintendo 64 (`mupen64plus-next`) e outros cores com pipeline gráfico avançado.
   - O reinício agora realiza recarga limpa da ROM e dos contextos de hardware da GPU de forma transparente, garantindo boot limpo sem falhas.

3. **Correção Visual no Menu de Atualização**:
   - Corrigida a renderização truncada de texto que exibia `"Voc versao mais recente"` no status do OSD.

4. **Atualização de Versão e Manifestos**:
   - `include/app_info.h` atualizado para `APP_VERSION "0.9.74"` e `Karamelo_v0.9.74.exe`.
   - Manifesto `dist/version.json` atualizado para a versão `0.9.74`.
   - Suíte de testes unitários expandida para validar a versão `0.9.74`.

5. **Artefatos e Hashes da Release macOS v0.9.74**:
   - Binário Auto-Update (`dist/Karamelo_mac`): 4.316.392 bytes, SHA-256: `02ED9C89183E52C0A0284FAB802BA6DFC8E09F75039A0EB589894DF5555E915F`
   - Pacote Completo (`dist/Karamelo_v0.9.74_macOS_arm64.tar.gz`): 123.040.652 bytes (123 MB)
   - Deploy realizado com sucesso para `187.127.59.127:/data/downloads/` com validação HTTP 200 no Nginx / Coolify.

---

## 33. Expansão de Catálogo: 8 Novos Ports e Atualização para 70 Ports (18/10/2026)

O catálogo de "Ports & Recomp" expande de 62 para **70 títulos** oficiais. Novos totais conferidos contra o `CREDITS.md` e o `src/port_runner.cpp`: **70 Windows 🪟, 42 Linux 🐧, 31 macOS 🍎**. A lista de jogos compatíveis com todos os três sistemas operacionais passa de 24 para **27 jogos**.

> **Aviso para a IA do site:** Atualizar contadores em cards, cabeçalhos e seletores de SO:
> - **Catálogo Geral**: 70 jogos em Ports & Recomp.
> - **Filtro Windows (🪟)**: 70 jogos.
> - **Filtro Linux (🐧)**: 42 jogos.
> - **Filtro macOS (🍎)**: 31 jogos.
> - **Compatibilidade Total (🪟🐧🍎)**: 27 jogos.

### Os 8 Novos Ports Integrados

1. **CPS-2 Recomp (Capcom Arcade)**:
   - **Repositório**: `tmcleroy/cps2recomp`
   - **Plataformas**: 🪟🐧🍎 (Windows, Linux, macOS).
   - **Descrição**: Recompilação estática de arcade para jogos Capcom CPS-1 e CPS-2. Inclui suporte nativo a Street Fighter Alpha 2, Cadillacs and Dinosaurs, Alien vs. Predator, Super Puzzle Fighter II Turbo, Vampire Savior 1/2 e Vampire Hunter 2.
   - **Como funciona**: O Karamelo copia automaticamente os romsets MAME/arcade compatíveis (`sfa2u.zip`, `dino.zip`, `avspu.zip`, etc.) de `roms/Arcade/` para a pasta do port antes da execução.

2. **Wayne Gretzky's 3D Hockey**:
   - **Repositório**: `geno55/wg3d-recomp`
   - **Plataformas**: 🪟 (Windows).
   - **Descrição**: Port nativo de Nintendo 64 via N64Recomp e RT64 (Direct3D 12 e Vulkan).
   - **Como funciona**: O Karamelo copia a ROM americana original de `roms/Nintendo64/` como `baserom.us.z64` diretamente para a pasta do port.

3. **Rush 2: Extreme Racing USA**:
   - **Repositório**: `bryantmh/rush2recompiled`
   - **Plataformas**: 🪟🐧 (Windows, Linux).
   - **Descrição**: Recompilação estática de Nintendo 64 via N64Recomp com suporte a altas resoluções e taxas de quadros destravadas.
   - **Como funciona**: O Karamelo passa automaticamente o caminho da ROM via argumento de inicialização.

4. **Automobili Lamborghini**:
   - **Repositório**: `alondero/automobililamborghini-recomp`
   - **Plataformas**: 🪟🐧 (Windows, Linux).
   - **Descrição**: Port nativo de Nintendo 64 via N64Recomp e RT64 com renderizador moderno, widescreen e 60 FPS.
   - **Como funciona**: O Karamelo repassa a ROM de `roms/Nintendo64/` diretamente via argumento `{rom}`.

5. **Road Rash 64**:
   - **Repositório**: `linkssy2/RoadRash64Recompiled`
   - **Plataformas**: 🪟🐧 (Windows, Linux).
   - **Descrição**: Recompilação estática de Nintendo 64 com suporte a resoluções ultrawide e melhorias de taxa de quadros.
   - **Como funciona**: Carregamento transparente da ROM original a partir de `roms/Nintendo64/`.

6. **Jungle Strike**:
   - **Repositório**: `Junior-Jones/Jungle-Strike-SNES-Static-Recomp`
   - **Plataformas**: 🪟 (Windows).
   - **Descrição**: Recompilação estática nativa do clássico de helicópteros do Super Nintendo com Launcher.exe dedicado.
   - **Como funciona**: O Karamelo localiza a ROM de SNES em `roms/SNES/` e espelha para a raiz do port e pasta `Rom/`.

7. **ICO**:
   - **Repositório**: `nathanialf/ico-pc`
   - **Plataformas**: 🪟🐧🍎 (Windows, Linux, macOS).
   - **Descrição**: Port nativo de PlayStation 2 em C++ via engenharia reversa do disco PAL SCES-50760. Suporte a 60 FPS, resoluções modernas e controles analógicos.
   - **Como funciona**: O jogador coloca a imagem original (.iso, .chd, .bin) em `roms/PS2/` e o Karamelo repassa via `{iso}` na linha de comando.

8. **Star Wars Battlefront: Renegade Squadron**:
   - **Repositório**: `GTTeancum/StarWars-RenegadeSquadron-PC`
   - **Plataformas**: 🪟 (Windows).
   - **Descrição**: Port nativo de PlayStation Portable (PSP) via recompilação estática com renderizador Direct3D 12, 60 FPS e suporte a controles analógicos duplos.
   - **Como funciona**: O jogador coloca a imagem UMD (`.iso`, versão americana ULUS10292) em `roms/PSP/`; na primeira execução o Karamelo extrai a pasta `PSP_GAME` para `data/` do port automaticamente. Arquivos `.cso` (comprimidos) não são aceitos.

### Atualização Adicional
- **F-Zero X (`craigshaw/FZeroRecomp`)**: Atualizado para incluir suporte oficial a **Linux** (🪟🐧🍎).

---

## 34. Lançamento da Versão 0.9.75 (19/10/2026)

Lançamento da versão **0.9.75** trazendo a expansão oficial do catálogo para **70 ports**, pipeline automático de extração para ports de PSP, e robustez na detecção de assets e correspondência de ROMs/ISOs.

### Principais Novidades da v0.9.75

1. **Expansão de Ports & Recomp (70 Títulos)**:
   - Inclusão dos 8 novos ports: CPS-2 Recomp (Capcom Arcade), Wayne Gretzky's 3D Hockey, Rush 2: Extreme Racing USA, Automobili Lamborghini, Road Rash 64, Jungle Strike (SNES), ICO (PS2) e Star Wars Battlefront: Renegade Squadron (PSP).
   - F-Zero X atualizado com build oficial para Linux (🪟🐧🍎).
   - Totais atualizados: **70 Windows 🪟, 42 Linux 🐧, 31 macOS 🍎** (27 rodam em todos).

2. **Pipeline de Extração PSP (`PSP_GAME`)**:
   - Para ports que leem o sistema de arquivos do UMD descompactado (como Star Wars Battlefront: Renegade Squadron), o Karamelo agora realiza a extração automática da árvore `PSP_GAME` diretamente da imagem `.iso` do jogador para a pasta de dados do port (`data/`), com validação de `SYSDIR/BOOT.BIN`.
   - Mensagens claras de toast orientando onde colocar o arquivo ISO caso ausente.

3. **Refinamento na Detecção de ROMs e Imagens de Disco**:
   - `rom_exclude_keywords`: impede conflitos entre edições do mesmo jogo (ex.: `Wayne Gretzky's 3D Hockey` não confunde mais com `Wayne Gretzky's 3D Hockey '98`).
   - `iso_keywords_whole_word`: casamento de palavras inteiras para títulos com nomes curtos (ex.: `ICO` não dispara falsos-positivos em arquivos com `Unicorn`).
   - Tratamento estrito de caracteres UTF-8 em `ToLowerStr` com `unsigned char` para evitar comportamento indefinido em caracteres não-ASCII.

4. **Robustez no Download de Ports**:
   - Fallback inteligente na API de Releases do GitHub quando a tag mais recente não possui assets binários ou foi publicada como pré-release (como a beta do CPS-2 Recomp).
   - Filtragem de notas e arquivos de soma de verificação (`.txt`, `.sha256`, `.md5`) na identificação de binários Linux soltos.

5. **Atualização de Versão e Manifestos**:
   - `include/app_info.h` atualizado para `APP_VERSION "0.9.75"` e `Karamelo_v0.9.75.exe`.
   - Manifesto `dist/version.json` atualizado para a versão `0.9.75`.
   - Suíte de testes unitários expandida para validar a versão `0.9.75`.

---

## 35. Lançamento da Versão 0.9.76 (20/10/2026)

Lançamento da versão **0.9.76** com foco em estabilidade e ciclo de vida de motores Libretro, correção crítica de encerramento no core Play! (PlayStation 2) e isolamento operacional de testes automatizados e Crash Reporter.

### Principais Novidades da v0.9.76

1. **Reordenação do Teardown de Motores Libretro (`core_runner`)**:
   - `RetroDeinitGuarded()` agora é invocado enquanto os streams de áudio e os contextos gráficos de hardware do frontend ainda estão ativos e válidos. Cores que mantêm threads de VM e subsistemas próprios em execução (como o `Play!` de PS2) agora conseguem sincronizar e encerrar suas threads ordenadamente sem dereferenciar buffers já destruídos.
   - Destruição de dispositivos de áudio SDL (`SDL_DestroyAudioStream`) e liberação de contextos gráficos (`HwReleaseCurrent`) passam a ocorrer estritamente após a conclusão de `retro_deinit`.

2. **Estabilidade e Isolamento de DLLs (`skip_free`)**:
   - O core `ps2_play.dll` foi adicionado à lista protegida de `skip_free` (junto a PCSX2, PPSSPP, GameCube e PSP), impedindo que o `FreeLibrary` desmapeie páginas de código com threads residuais da biblioteca em finalização ou force travamentos de *loader lock*.
   - Tratamento de `HwSetSkipContextDestroy` estendido para toda a família PS2, prevenindo chamadas concorrentes inválidas ao destruir contexto gráfico.

3. **Isolamento de Crash Reporter em CLI / Autotestes**:
   - Implementado `CrashReporterSetTransientDisable`: execuções headless ou de linha de comando (`--core-selftest`, `--install-port`, etc.) continuam registrando detalhes em `karamelo.log`, mas não gravam `crash_dump.txt` nem disparam transmissões de telemetria aos canais de produção.

4. **Sincronização de Versão e Baterias de Teste**:
   - `APP_VERSION` atualizado para `0.9.76` em `include/app_info.h` e executável `Karamelo_v0.9.76.exe`.
   - Manifesto `dist/version.json` atualizado para a versão `0.9.76`.
   - Suíte de testes unitários (`tests/test_updater.cpp`) expandida com cobertura para a nova versão.
   - Baterias de estresse `tools/stress_cores.ps1` executadas e validadas com 100% de aprovação (PCSX2 e Play!).

---

## 36. Lançamento da Versão 0.9.77 — Suporte a NEC PC-98, FM Towns / Marty, Descompactador Transparente e Auditoria Geral de BIOS (22/10/2026)

Inclusão oficial de dois dos computadores mais icônicos do mercado japonês no catálogo nativo do Karamelo: **NEC PC-9800 Series (PC-9801 / PC-9821)** e **Fujitsu FM Towns / FM Towns Marty**, elevação da contagem de sistemas para **37 sistemas** e **42 motores de emulação**, e suporte transparente a arquivos compactados (`.zip`, `.7z`, `.rar`).

### Especificação dos Novos Sistemas

1. **NEC PC-9801 / PC-9821**:
   - **Motor**: **NP2kai** (Neko Project II Kai) — `pc98.dll` (Windows), `pc98.so` (Linux), `pc98.dylib` (macOS).
   - **Pasta de ROMs**: `roms/PC98/`
   - **Formatos Aceitos**:
     - Discos rígidos: `.hdi`, `.nhd`, `.thd`, `.hdd`
     - Disquetes: `.fdi`, `.fdd`, `.d88`, `.88d`, `.2hd`, `.tfd`, `.hdm`, `.xdf`, `.dup`, `.cmd`
     - Imagens de CD: `.cue`, `.iso`, `.chd`
     - Compactados: `.zip`, `.7z`, `.rar`
   - **BIOS e Arquivos de Sistema**: Subpasta obrigatória `app/bios/np2kai/`.
     - `font.bmp` (ou `font.rom`): Obrigatório para desenhar kanji/kana legíveis nos jogos.
     - `bios.rom` (98.304 bytes - MD5 `a4fc2355403063f9cfb403f56b50eec3`), `itf.rom` (8.192 bytes - MD5 `185078a63503aa3ca8bb64b38d3ab16e`), `sound.rom` (8.192 bytes - MD5 `97800c7329524da5f6efb2e4d9c74567`).

2. **Fujitsu FM Towns / FM Towns Marty**:
   - **Motor**: **Geartowns** (Ignacio Sanchez / drhelius) — `fmtowns.dll` (Windows), `fmtowns.so` (Linux), `fmtowns.dylib` (macOS).
   - **Pasta de ROMs**: `roms/FMTowns/`
   - **Formatos Aceitos**:
     - Imagens de CD: `.chd`, `.cue/.bin`, `.iso`, `.m3u`
     - Disquetes de boot: `.d77`, `.d88`, `.hdm`, `.xdf`, `.bin`, `.img`
     - Compactados: `.zip`, `.7z`, `.rar`
   - **BIOS e Arquivos de Sistema**: Soltos em `app/bios/` ou `app/bios/fmtowns/`.
     - `FMT_SYS.ROM` (524.288 bytes - MD5 `3a4f31cbf48737db6f2df8ef036ab7d4`): BIOS de Sistema Towns OS.
     - `FMT_DOS.ROM` (524.288 bytes - MD5 `fa0c98f5a6b0c61ee34f9a72138fc6b5`): Towns OS DOS Kernel.
     - `FMT_FNT.ROM` (MD5 `a35ee3364f26b5270be7f3f185ef3577`): Fontes básicas.
     - `FMT_DIC.ROM` (524.288 bytes - MD5 `e28e67838634891b8d6505417ec651db`): Dicionário Kanji.
     - `FMT_F20.ROM` (1.253.376 bytes - MD5 `9559e2ab176041ec1eaae92795c6f671`): Fonte estendida 20-dot (opcional).

3. **Descompactador Transparente (`src/archive_helper.cpp`)**:
   - Resolução de formato estendida com suporte direto a `.hdi`, `.d88`, `.fdi`, `.nhd` e `.d77` dentro de zips.
   - Detecção automática de subpastas `roms/PC98` e `roms/FMTowns` para roteamento correto aos respectivos cores `cores/pc98.dll` e `cores/fmtowns.dll`.

4. **Auditoria e Instalação Completa de BIOS (100% de Conformidade)**:
   - **FM Towns**: Todas as 5 ROMs oficiais do Towns OS instaladas em `app/bios/` e `app/bios/fmtowns/` (`FMT_SYS.ROM`, `FMT_DOS.ROM`, `FMT_FNT.ROM`, `FMT_DIC.ROM`, `FMT_F20.ROM`), com suporte tanto ao set Model 1/2 quanto ao set Marty/Fresh/UX.
   - **NEC PC-98**: Conjunto completo de 17 arquivos de sistema instalado em `app/bios/np2kai/` (`font.bmp`, `font.rom`, `bios.rom`, `bios9821.rom`, `itf.rom`, `sound.rom`, `ide.rom`, `sasi.rom`, `scsi.rom`, `pci.rom`, `d8000.rom` e 6 samples WAV de percussão do chip FM YM2608).
   - **Complementos em Outros Sistemas**:
     - Atari 7800: `7800 BIOS (U).rom` instalado e conferido por MD5.
     - Master System / Game Gear: `bios_U.sms`, `bios_E.sms`, `bios_J.sms` e `bios.gg` instalados.
     - Commodore Amiga: Kickstart 3.1 AGA (`kick40068.A1200`) adicionado ao conjunto clássico A500 + CD32.
   - **Validação de Integridade (`tools/check_bios.ps1`)**:
     - 29/29 itens conferidos com 100% de sucesso (0 erros de hash, 0 obrigatórios faltando, 0 opcionais faltando).

5. **Impacto em Contagens Oficiais**:
   - `APP_VERSION`: 0.9.76 ➔ **0.9.77**.
   - `APP_SYSTEM_COUNT`: 35 ➔ **37 sistemas**.
   - `APP_CORE_ENGINES`: 40 ➔ **42 motores**.
   - `APP_CORE_FILES`: 41 ➔ **43 arquivos** de core.




