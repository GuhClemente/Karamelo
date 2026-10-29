# KARAMELO OS & DOCKER — Especificação Arquitetural e Guia de Engenharia

Este documento especifica a estratégia, arquitetura e pipeline de engenharia para duas frentes de implantação do **Karamelo Emulador**:
1. **Karamelo em Docker**: Para servidores domésticos (NAS, Unraid, TrueNAS), cloud gaming / streaming local e CI/CD.
2. **Karamelo OS (ISO Linux Personalizada)**: Transformação de qualquer PC ou Mini-PC em um console de sala dedicado plug-and-play (estilo MiSTer FPGA e SteamOS 3).

---

## 1. Visão Geral das Abordagens

```
                           ┌───────────────────────────────────────────────┐
                           │          KARAMELO EMULADOR (C++20)            │
                           └───────────────────────┬───────────────────────┘
                                                   │
                         ┌─────────────────────────┴─────────────────────────┐
                         ▼                                                   ▼
       ┌───────────────────────────────────┐               ┌───────────────────────────────────┐
       │       KARAMELO EM DOCKER          │               │           KARAMELO OS             │
       │    (Container / Server Mode)      │               │     (Dedicated Console ISO)       │
       ├───────────────────────────────────┤               ├───────────────────────────────────┤
       │ • Servidores Domésticos (NAS)     │               │ • Mini-PCs (Intel N100, Ryzen)    │
       │ • Web Studio & FTP Ativos 24/7    │               │ • Boot em 6s direto na TV da sala │
       │ • Streaming WebRTC / Sunshine     │               │ • Zero teclado/mouse (só gamepad) │
       │ • Sem conflitos de dependências   │               │ • Gamescope (FSR, 4K, HDR, 60fps) │
       │ • CI/CD e testes automatizados    │               │ • Sistema imutável anti-queda     │
       └───────────────────────────────────┘               └───────────────────────────────────┘
```

---

## 2. Parte 1 — Karamelo em Docker

### 2.1 Cenários de Aplicação e Benefícios
O container Docker não substitui o console físico de sala, mas é a solução ideal para:

* **Servidores Domésticos & NAS (Unraid, TrueNAS SCALE, Synology, Proxmox)**:
  * Centralização da biblioteca de jogos da casa em um único servidor.
  * Execução do **Web Studio LAN** e servidor FTP do Karamelo 24h por dia para gerenciamento de ROMs, BIOS e saves.
* **Streaming Local / Cloud Gaming Pessoal**:
  * O Karamelo roda em modo headless com um framebuffer virtual (`Xvfb` ou `cage`) emparelhado com WebRTC ou Sunshine/Moonlight.
  * O usuário joga diretamente pelo navegador web da Smart TV, celular ou tablet sem instalar nada no cliente.
* **Ambiente de Testes Reproduzível (CI/CD)**:
  * Compilação e execução da suíte completa de testes unitários (53+ testes) no GitHub Actions de forma isolada.

### 2.2 Estrutura do `Dockerfile` (Referência)
```dockerfile
# packaging/docker/Dockerfile
FROM archlinux:base-devel AS build

# Dependências de compilação
RUN pacman -Syu --noconfirm git cmake ninja clang sdl3 libpulse mesa libpipewire glibc

WORKDIR /build
COPY . .
RUN ./compile_linux.sh

# Imagem final de execução
FROM archlinux:base
RUN pacman -Syu --noconfirm sdl3 mesa libpipewire libpulse gamescope && rm -rf /var/cache/pacman/pkg/*

WORKDIR /app
COPY --from=build /build/app/Karamelo /app/Karamelo
COPY --from=build /build/data /app/data

# Portas: Web Studio LAN (8080), Netplay P2P (18080)
EXPOSE 8080 18080

ENTRYPOINT ["/app/Karamelo"]
```

### 2.3 Execução com Aceleração de Hardware (`docker-compose.yml`)
Para rodar com aceleração por hardware (GPU e som direto do host):
```yaml
version: '3.8'
services:
  karamelo:
    image: karamelo-emu:latest
    container_name: karamelo-studio
    restart: unless-stopped
    devices:
      - /dev/dri:/dev/dri       # Passagem de GPU (Mesa/Vulkan/OpenGL)
      - /dev/snd:/dev/snd       # Áudio ALSA/Pulse
      - /dev/input:/dev/input   # Controles USB e Gamepads
    environment:
      - DISPLAY=:0
    volumes:
      - /mnt/storage/roms:/app/roms
      - /mnt/storage/bios:/app/bios
      - /mnt/storage/saves:/app/saves
    ports:
      - "8080:8080"
      - "18080:18080/udp"
```

---

## 3. Parte 2 — Karamelo OS: A ISO Linux Dedicada

O **Karamelo OS** é uma imagem de sistema operacional completa que transforma qualquer computador antigo, Mini-PC de sala ou console portátil x86 (Steam Deck, ROG Ally) em um console retrô de inicialização imediata.

### 3.1 A Escolha da Base: Arch Linux Minimal (`archiso`)
Entre as alternativas existentes, o **Arch Linux** foi definido como a base canônica (mesma decisão da Valve para o **SteamOS 3**):

1. **Drivers Gráficos e Kernel de Ponta**:
   * O Arch fornece os kernels e drivers Mesa/Vulkan mais recentes, essenciais para rodar com eficiência em Mini-PCs populares (Intel N95, N100, chips AMD Ryzen 5500U/5700U e gráficos Intel Arc).
2. **Compatibilidade Total com os 59 Ports de PC**:
   * O Karamelo possui 59 ports e recompilações nativas para Linux (`data/ports.json`). Como a base Arch utiliza `glibc` completa e bibliotecas dinâmicas modernas, todos os ports rodam nativamente sem a necessidade de contêineres ou flatpaks pesados.
3. **Facilidade de Customização com `archiso`**:
   * A ferramenta oficial `archiso` (`mkarchiso`) permite gerar imagens ISO limpas, scriptáveis e reproduzíveis em poucos minutos.

### 3.2 O Papel do Gamescope (Micro-Compositor da Valve)
O **Gamescope** é o componente responsável pela mágica da experiência de console:
* **Isolamento de Janela**: O Karamelo não roda sob uma área de trabalho pesada (sem X11, sem GNOME/KDE). Ele roda isolado no Gamescope.
* **Integer-Scaling (Pixel Perfect) Nativo**: Jogos em 240p (SNES, Genesis) ou 480p (PS1, N64) são redimensionados para 4K na TV com proporção exata e zero borrão, preservando a nitidez das scanlines dos shaders CRT.
* **AMD FSR (FidelityFX Super Resolution)**: Upscaling inteligente direto no compositor para ports mais pesados.
* **Zero Input Lag**: O Gamescope conversa diretamente com o DRM/KMS da placa de vídeo, eliminando a latência de buffer comum de gerenciadores de janelas.
* **HDR para TVs Modernas**: Capacidade de injetar iluminação HDR em shaders de TV de tubo nas telas OLED e QLED.

Comando de inicialização padrão no sistema:
```bash
gamescope -W 3840 -H 2160 -w 1920 -h 1080 -r 60 -f -- /usr/bin/Karamelo
```

### 3.3 Arquitetura de Armazenamento e Tolerância a Quedas de Energia
Consoles de sala são rotineiramente desligados da tomada sem desligamento prévio. Para evitar corrupção:

```
┌────────────────────────────────────────────────────────┐
│                   Disco / SSD / Pendrive               │
├───────────────────────────┬────────────────────────────┤
│ Partição 1: Sistema (OS) │ Partição 2: Dados (Storage)│
│       [READ-ONLY]         │       [READ-WRITE]         │
│      (SquashFS + EROFS)   │          (Btrfs/EXT4)      │
├───────────────────────────┼────────────────────────────┤
│ • Kernel e Drivers        │ • roms/                    │
│ • Gamescope e PipeWire    │ • bios/                    │
│ • Binário /usr/bin/Karamelo│ • saves/                   │
│ • Bibliotecas do sistema  │ • config/karamelo.ini      │
└───────────────────────────┴────────────────────────────┘
```

* **Partição 1 (Root Imutável)**: Montada como apenas-leitura (`ro`). Se a energia for cortada no meio de um jogo, o sistema nunca será corrompido.
* **Partição 2 (`/storage`)**: Partição com journaling (EXT4 ou Btrfs) que armazena jogos e saves.
* **Auto-Expansão no Primeiro Boot (Flash & Play)**: Ao gravar a ISO em um SSD de 512 GB ou pendrive de 64 GB, um serviço executado no primeiro boot expande automaticamente a partição de armazenamento para ocupar 100% do disco disponível.

---

## 4. Recursos da Experiência "Karamelo Appliance"

### 4.1 Gestão Sem Fio pelo Celular / Web Studio LAN
* Ao iniciar o Karamelo OS, o endereço e o PIN de acesso aparecem discretamente na interface:
  ```
  🌐 Rede: 192.168.1.15  |  Web Studio: http://192.168.1.15:8080 (PIN: 3914)
  ```
* O usuário no sofá pega o celular, conecta e:
  * Faz upload de ROMs e arquivos de BIOS via browser.
  * Acompanha conquistas do RetroAchievements em tempo real.
  * Usa o controle touch remoto se o joystick descarregar.

### 4.2 Compartilhamento de Rede Samba (SMB) Integrado
O sistema sobe automaticamente um servidor Samba leve, tornando as pastas do Karamelo visíveis no Windows Explorer e no Finder do Mac:
* `\\KARAMELO\roms`
* `\\KARAMELO\bios`
* `\\KARAMELO\saves`

### 4.3 Pareamento Rápido de Controles Bluetooth (BlueZ)
* Suporte nativo a gamepads Xbox Wireless, PlayStation (DualShock 4 / DualSense), Nintendo Switch Pro e toda a linha 8BitDo.
* Wizard de pareamento integrado ao menu OSD ou ativável via Web Studio.

### 4.4 Atualizações OTA (Over-The-Air) com 1 Botão
* Atualizações futuras do Karamelo não exigem regravar a imagem do pendrive.
* O sistema utiliza o auto-atualizador nativo do Karamelo para baixar o binário Linux verificado com SHA-256 direto de `https://karamelo-emu.com/downloads/` e reinicia o serviço instantaneamente.

---

## 5. Blueprint de Construção da ISO (`packaging/os/archiso`)

### 5.1 Árvore de Diretórios
```
packaging/os/archiso/
├── profiledef.sh                            # Metadados da ISO (nome, autor, versão)
├── packages.x86_64                          # Pacotes obrigatórios do Arch Linux
├── airootfs/                                # Sistema de arquivos injetado
│   ├── etc/
│   │   ├── systemd/system/
│   │   │   ├── karamelo.service             # Auto-start do Gamescope + Karamelo
│   │   │   ├── resize-storage.service       # Auto-expansão no 1º boot
│   │   │   └── smb.conf                     # Compartilhamento de rede das ROMs
│   │   └── sudoers.d/karamelo
│   └── usr/
│       └── bin/
│           ├── Karamelo                     # Executável compilado
│           └── karamelo-session             # Script de inicialização do Gamescope
└── build_iso.sh                             # Script de automação da compilação
```

### 5.2 Pacotes Mínimos (`packages.x86_64`)
```text
linux
linux-firmware
systemd
systemd-boot
networkmanager
bluez
bluez-utils
pipewire
pipewire-audio
pipewire-alsa
pipewire-pulse
mesa
vulkan-intel
vulkan-radeon
gamescope
samba
parted
e2fsprogs
sdl3
```

### 5.3 O Serviço de Inicialização (`karamelo.service`)
```ini
[Unit]
Description=Karamelo Console Appliance
After=network.target sound.target bluetooth.target
DefaultDependencies=no

[Service]
Type=simple
User=karamelo
PAMName=login
Environment=XDG_RUNTIME_DIR=/run/user/1000
ExecStart=/usr/bin/gamescope -W 3840 -H 2160 -w 1920 -h 1080 -r 60 -f -- /usr/bin/Karamelo
Restart=always
RestartSec=1

[Install]
WantedBy=multi-user.target
```

### 5.4 Comando de Geração da ISO
```bash
# Executado em qualquer máquina Linux ou container com archiso instalado:
sudo mkarchiso -v -w /tmp/archiso-work -o dist/ packaging/os/archiso/
```

---

## 6. Dispositivos e Hardware Alvo

| Categoria | Exemplos de Hardware | Experiência Esperada |
| :--- | :--- | :--- |
| **Mini-PCs Modernos** | Intel N95/N100, AMD Ryzen 5500U/5700U | Resolução 4K na TV, todos os 37 sistemas e 59 ports a 60 FPS cravados. |
| **PCs de Sala / Gabinetes Antigos** | Core i5 de 4ª a 8ª geração, GT 1030 / RX 550 | Recuperação de máquinas antigas como estações arcade completas. |
| **Handhelds x86** | Steam Deck, ROG Ally, Ayaneo, Legion Go | Portabilidade com interface MiSTer, consumo de bateria ultra-otimizado pelo C++20. |

---

## 7. Status e Próximos Passos
- [x] Binário Linux x86_64 nativo compilável e testado via `compile_linux.sh`.
- [x] Web Admin LAN com pareamento por PIN e visualizador de troféus.
- [ ] Criação dos arquivos de configuração em `packaging/docker/`.
- [ ] Criação da receita `packaging/os/archiso/` para geração da ISO preliminar `KarameloOS_Preview.iso`.
