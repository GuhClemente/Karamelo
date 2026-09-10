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
| Plataforma | Windows x64, nativo |

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
    "exe_url": "https://karamelo-emu.com/downloads/Karamelo.exe",
    "exe_size": 4027392,
    "zip_url": "https://karamelo-emu.com/downloads/Karamelo_v0.9.4_Win64.zip",
    "version": "0.9.4",
    "release_date": "2026-09-09",
    "notes": "...",
    "exe_sha256": "268B286F...",
    "force_full_package": false,
    "title": "Karamelo v0.9.4"
}
```

O site deve montar o botão de download **lendo esse arquivo**, nunca com o
número da versão escrito à mão no código da página. Assim uma release nova não
exige tocar no site.

⚠️ **Esse arquivo é lido pelo auto-update do aplicativo.** Não altere, não
mova, não renomeie, não sirva com outro `Content-Type` e não coloque nada na
frente que responda HTML em vez do JSON. Se ele quebrar, todo mundo que tem o
app instalado perde a atualização automática.

### O que oferecer para download

| arquivo | oferecer? |
|---|---|
| `Karamelo_v<versão>_Win64.zip` | **sim** — é o download principal |
| `Karamelo.exe` | **não** ofereça como download avulso |
| `Karamelo_Pack_BIOS.zip` | **nao existe mais** |

O `Karamelo.exe` sozinho **não funciona**: ele localiza `cores/`, `bios/`,
`roms/` e `saves/` a partir da própria pasta. Quem baixar só ele fica com um
app quebrado. Ele existe naquela URL porque é o que o auto-update baixa, não
para consumo humano.

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

O código é aberto sob **PolyForm Noncommercial 1.0.0**:

```
https://github.com/GuhClemente/Karamelo/blob/main/LICENSE.md
```

Como descrever, corretamente:

> Qualquer pessoa pode ler, estudar, modificar, forkar e redistribuir o código.
> **Uso comercial não é permitido.**

**Não escreva "open source" sem qualificação.** Pela definição da OSI isto não
é open source, justamente por causa da restrição comercial — o próprio GitHub
não exibe selo de licença por isso. Formulações seguras: "código aberto",
"código-fonte disponível", "aberto sob PolyForm Noncommercial". Formulação
errada: "open source sob licença MIT/GPL", "software livre".

Também não descreva o app como "freeware" e ponto: é gratuito **e** tem o
código publicado **e** o uso comercial é vedado. As três coisas juntas.

### Terceiros

O app embute bibliotecas de terceiros com licenças próprias, listadas em
`THIRD-PARTY-NOTICES.md` no repositório. Se o site tiver página de créditos,
linke para lá em vez de reescrever a lista. Um ponto que costuma ser mal
entendido: o core de N64 `n64_gopher.dll` é **GPL-3.0**, e essa licença vale
para ele, **não** para o Karamelo — o core é um binário separado carregado
pela API libretro.

Os cores de emulação são projetos de terceiros, cada um com licença própria,
catalogados em `CREDITS.md`. O site não deve dar a entender que foram feitos
por este projeto.

---

## 5. Aviso legal obrigatório

Deve aparecer em qualquer página que mencione MiSTer FPGA, e no rodapé:

> O **Karamelo Emulador** é um projeto de software independente desenvolvido
> para o ecossistema Windows e **NÃO possui qualquer afiliação, vínculo ou
> endosso de Alexey Melnikov, do projeto oficial MiSTer FPGA ou de seus
> mantenedores**.

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

**Está certo e não deve mudar:** os **39** jogos de "Ports & Recomp". Esse
número foi conferido contra o `CREDITS.md`. É coincidência infeliz que 39 já
tenha sido, um dia, o número errado de motores — não confunda os dois.

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

- Os **39** jogos de Ports & Recomp, e o texto explicando que o app baixa só o
  binário de código aberto de cada projeto.
- A descrição da licença PolyForm Noncommercial, incluindo a ressalva de uso
  comercial.
- O FAQ sobre não distribuir ROM/BIOS, e o de "vocês reescreveram os cores?".
- O botão de download lendo o `version.json`.
- A ausência de qualquer link para o pack de BIOS.
- O texto "recriados do zero em C++20" — é verdade, e é exatamente a distinção
  que a seção 1 exige que o site preserve.
