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
| `APP_VERSION` | 0.9.3 | prefira ler do `version.json` (seção 3) |

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
    "zip_url": "https://karamelo-emu.com/downloads/Karamelo_v0.9.3_Win64.zip",
    "version": "0.9.3",
    "release_date": "2026-09-09",
    "notes": "...",
    "exe_sha256": "268B286F...",
    "force_full_package": false,
    "title": "Karamelo v0.9.3"
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
- **Publicar 39 motores** porque foi copiado de outra página, quando a fonte
  dizia outra coisa. Leia do `app_info.h`.
- **Oferecer o `.exe` avulso** como download principal. Não funciona sozinho.
- **Chamar de "port do MiSTer".** É falso e conflita com a licença.
- **Chamar de "open source"** sem ressalva. A restrição comercial impede.
