# 📁 Guia de Mapeamento de ROMs em Discos Externos / Partições Secundárias (Windows & Linux)

Este guia ensina como apontar as pastas de jogos (**Saturn, Dreamcast, PlayStation, PS2, GameCube, etc.**) para um HD secundário, partição `D:\` ou SSD externo, **sem precisar copiar os arquivos** e sem ocupar espaço no disco principal `C:\`.

---

## 🎯 Por que usar Junctions e Links Simbólicos?

* **Zero Espaço Duplicado:** Os jogos ficam no seu HD ou partição secundária (`D:\`, `E:\`), mas o Karamelo os enxerga como se estivessem dentro da pasta `roms/`.
* **Sem perda de desempenho:** O acesso é em nível de sistema de arquivos NTFS/ext4, garantindo velocidade máxima de leitura.
* **Compatibilidade 100%:** Saves, savestates, capas, retroachievements e carregamento de CHDs/ISOs funcionam de forma idêntica.

---

## 🪟 1. No Windows (Recomendado: `mklink /J`)

No Windows, o melhor método para discos locais é a **Junction** (`/J`). Ela não exige executar o terminal como Administrador.

> [!IMPORTANT]
> Se já existir uma pasta com o nome da plataforma vazia dentro de `roms` (por exemplo, `roms\Saturn`), **apague ou renomeie essa pasta vazia antes de rodar o comando**. O `mklink` precisa criar o atalho com esse nome.

### Passo a Passo via Prompt de Comando (CMD):

Abra o CMD na pasta do seu Karamelo (ou navegue até ela):

```cmd
cd /d "C:\Caminho\Ate\O\Karamelo"
```

#### Exemplos por Plataforma:

* **Sega Saturn:**
  ```cmd
  mklink /J "app\roms\Saturn" "D:\Saturn"
  ```
* **Sega Dreamcast:**
  ```cmd
  mklink /J "app\roms\Dreamcast" "D:\Dreamcast"
  ```
* **Sony PlayStation (PS1):**
  ```cmd
  mklink /J "app\roms\PlayStation" "D:\PlayStation"
  ```
* **Sony PlayStation 2 (PS2):**
  ```cmd
  mklink /J "app\roms\PlayStation2" "D:\PlayStation2"
  ```
* **Nintendo GameCube:**
  ```cmd
  mklink /J "app\roms\GameCube" "D:\GameCube"
  ```

---

### Mapeando a pasta `roms` INTEIRA de uma vez:

Se você já tem uma pasta com todas as plataformas organizadas no seu segundo HD (ex: `D:\TodasMinhasRoms`):

1. Certifique-se de que não há uma pasta `roms` ativa no Karamelo (ou renomeie para `roms_bkp`).
2. Execute:
   ```cmd
   mklink /J "app\roms" "D:\TodasMinhasRoms"
   ```
Pronto! Todas as plataformas do drive `D:` aparecem automaticamente no emulador.

---

### Alternativa via PowerShell:

Se preferir usar o PowerShell do Windows, utilize o cmdlet `New-Item`:

```powershell
New-Item -ItemType Junction -Path ".\app\roms\Saturn" -Target "D:\Saturn"
```

---

## 🗑️ Como Desfazer o Mapeamento com Segurança

Se um dia você quiser remover o atalho do Karamelo, **NUNCA use Shift+Delete pelo Windows Explorer** se não tiver certeza, pois versões antigas do Windows podiam excluir o conteúdo de destino.

O jeito 100% seguro de remover a Junction sem encostar nos seus jogos reais é pelo CMD:

```cmd
rmdir "app\roms\Saturn"
```

*(O comando `rmdir` apenas desfaz a ligação; seus jogos no drive `D:\` continuam intactos).*

---

## 🐧 2. No Linux / Karamelo OS

No Linux, o procedimento é feito utilizando links simbólicos nativos (`ln -s`):

```bash
# Apagar a pasta vazia existente
rmdir /opt/karamelo/roms/Saturn 2>/dev/null || true

# Criar o link simbólico apontando para o seu HD ou partição montada
ln -s "/mnt/meu_hd/Saturn" "/opt/karamelo/roms/Saturn"
```

Para fixar a montagem de um HD secundário no boot do Linux, adicione a entrada correspondente no `/etc/fstab`.
