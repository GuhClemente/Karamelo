# ROADMAP — Planejamento e Arquitetura de Implementações Futuras

> **Karamelo Emulador — Native Multiplatform Retrogaming Suite**  
> Documento mestre de especificação técnica e planejamento para versões futuras (v0.9.6+ e v1.0.0).  
> **Regra Fundamental**: Toda evolução futura deve preservar a filosofia clean-room, alto desempenho, latência zero e suporte multiplataforma nativo (Windows x64, Linux x64 e macOS ARM64).

---

## 🌟 Visão Geral das Frentes de Evolução

```
                    ┌──────────────────────────────────────────────┐
                    │      KARAMELO - ROADMAP DE EVOLUÇÃO          │
                    └──────────────────────┬───────────────────────┘
                                           │
         ┌──────────────────┬──────────────┴─────┬──────────────────┐
         │                  │                    │                  │
         ▼                  ▼                    ▼                  ▼
┌─────────────────┐┌─────────────────┐ ┌─────────────────┐ ┌─────────────────┐
│   Experiência   ││    Ports &      │ │   Integração    │ │   Comunicação   │
│   do Jogador    ││ Recompilações   │ │   Audiovisual   │ │   & Social      │
├─────────────────┤├─────────────────┤ ├─────────────────┤ ├─────────────────┤
│• Favoritos      ││• Auto-Update de │ │• Bezels/Molduras│ │• Discord RPC    │
│• Recentes       ││  Ports no OSD   │ │  Handheld & CRT │ │• RA Rich Presence│
│• Galeria Prints ││• Expansão Linux │ │• Filtros Custom │ │• Netplay Lobby  │
└─────────────────┘└─────────────────┘ └─────────────────┘ └─────────────────┘
```

---

## 1. Experiência do Jogador & OSD

### 1.1 Sistema de "Favoritos" & "Jogados Recentemente"
- **Objetivo**: Proporcionar acesso instantâneo aos jogos e ports prediletos sem necessidade de navegar manualmente pelas árvores de pastas de cada um dos 35 sistemas.
- **Especificação Técnica**:
  - **Atalho no Navegador**: No navegador de ROMs (`STATE_BROWSE`) e no menu de Ports (`STATE_PORTS`), a tecla de atalho `Y` (no gamepad) ou `F3` (no teclado) alternará o status de favorito do item selecionado.
  - **Persistência no `Config/karamelo.ini`**:
    ```ini
    [Favorites]
    count=2
    fav_0=roms/N64/Super Mario 64.z64
    fav_1=ports/Zelda64Recomp

    [Recent]
    max_history=15
    recent_0=roms/PSX/Castlevania - Symphony of the Night.chd
    recent_1=ports/DevilutionX
    ```
  - **Menu OSD**: Adicionar as entradas no topo do menu principal:
    - `⭐ Jogos Favoritos >` (exibe a lista consolidada com indicação do sistema/ícone).
    - `🕒 Jogados Recentemente >` (pilha FIFO dos últimos 15 títulos iniciados).
  - **Integridade**: Verificação transparente de existência via `fs::exists()` antes de tentar iniciar. Itens cujos arquivos foram removidos pelo usuário são limpos automaticamente com aviso discreto.

### 1.2 Gerenciador & Galeria de Capturas de Tela (Screenshots)
- **Estado Atual**: A função `CoreTakeScreenshot()` já está integrada (acessível via `F9` e pelo menu OSD `Screenshot (F9)`), salvando arquivos BMP datados na pasta `screenshots/`.
- **Evolução Planejada**:
  - **Compressão PNG transparente**: Implementar escrita de PNG via `miniz` ou libpng embutida para reduzir o tamanho dos arquivos em até 80% sem perda de nitidez.
  - **Modo de Captura Dual**: Opção nas configurações de Vídeo:
    - *Captura Raw (Framebuffer Original)*: Resolução nativa perfeita para preservação de arte.
    - *Captura CRT (Com Efeito de Tubo/Scanlines)*: Reproduz exatamente o que está sendo visto na tela.
  - **Visualizador no OSD (`STATE_SCREENSHOTS`)**: Submenu que lista os prints do jogo ativo com preview em thumbnail no canto do OSD.

---

## 2. Molduras Gráficas, Bezels e Aspect Ratios

### 2.1 Overlays Decorativos (Bezels de Tubo e Handhelds)
- **Objetivo**: Preencher de forma imersiva as faixas pretas laterais (pillarbox) em telas widescreen 16:9 / 16:10 ao jogar títulos clássicos em proporção 4:3, além de oferecer molduras temáticas para sistemas portáteis.
- **Arquitetura Técnica**:
  - **Estrutura de Arquivos**: Pasta `app/Bezels/<Sistema>/overlay.png` (formato PNG 32-bit com canal Alpha / transparência).
  - **Sistemas Prioritários**:
    - **Game Boy (DMG)**: Carcaça cinza clássica com tela esverdeada.
    - **Game Boy Advance**: Moldura roxa/índigo com proporção 3:2.
    - **Game Gear**: Carcaça preta horizontal característica.
    - **Neo Geo Pocket / WonderSwan**: Molduras temáticas dos consoles portáteis.
    - **TV CRT Vintage**: Moldura de tubo com curvas de madeira e botões analógicos.
  - **Pipeline Gráfico**:
    - No renderizador (D3D11 em Windows, OpenGL em Linux/macOS), desenhar o quad do bezel ao redor do viewport calculado por `MathCalculateViewport()`.
  - **Controle pelo OSD**: Em `Configurações de Vídeo`:
    - `Moldura / Bezel`: `< Auto (Por Sistema) | Forçar TV CRT | Desativado >`.

---

## 3. Subsistema de PC Ports & Recompilados

### 3.1 Verificador e Atualizador de Ports no OSD
- **Objetivo**: Permitir que o usuário atualize ports instalados para a versão mais recente do GitHub sem ter que apagar pastas ou refazer downloads manuais.
- **Especificação Técnica**:
  - Consumo assíncrono da API pública do GitHub (`/repos/{repo}/releases/latest`).
  - Armazenamento da tag instalada em `ports/<PortID>/.version`.
  - Ao abrir a página do port no OSD:
    - Se houver versão mais recente: exibe `Atualizar para vX.Y.Z >`.
    - Ao confirmar, o download do novo pacote é realizado preservando saves, configurações e ROMs base que estejam na pasta do port.

### 3.2 Expansão do Suporte Nativo para Linux e macOS
- **Meta Linux**: Subir de 32 para 40+ ports nativos:
  - Investigar repositórios da comunidade para compilações x86_64 nativas de *Ship of Harkinian*, *2 Ship 2 Harkinian*, *Space Station Silicon Valley*, *F-Zero X* e *WipEout*.
- **Meta macOS**: Subir de 24 para 30+ ports nativos:
  - Validar lançamentos recentes de ports com binários Mach-O Universal/ARM64.

---

## 4. Integração Social & Conectividade

### 4.1 Discord Rich Presence (RPC)
- **Objetivo**: Exibir no perfil do jogador o status detalhado da jogatina no Karamelo.
- **Arquitetura**:
  - Integração da biblioteca C leve `discord-rpc` via carregamento dinâmico (não trava caso o app do Discord não esteja rodando).
  - **Informações Transmitidas**:
    - *Estado*: "Jogando Super Mario 64" / "Jogando Zelda 64 Recompiled".
    - *Detalhe*: "Nintendo 64 (ParaLLEl N64)" ou "PC Port Nativo".
    - *Tempo Decorrido*: Timer de início da sessão atual.
    - *Ícone Grande*: Logotipo do sistema ou do port.
    - *Ícone Pequeno*: Logotipo do Karamelo.

### 4.2 RetroAchievements Rich Presence
- **Objetivo**: Conectar as strings de presença da biblioteca `rcheevos` (já integrada na v12.4.0) diretamente ao OSD e ao Discord RPC.
- **Exemplo**: "Fase 3-1 | Vidas: 4 | Moedas: 78 | Modo Hardcore: Ativo".

### 4.3 Navegador de Servidores de Netplay (Lobby Local / P2P)
- **Objetivo**: Substituir a necessidade de digitar endereços de IP externos manualmente para jogar online.
- **Especificação Técnica**:
  - Descoberta automática de instâncias de Karamelo na mesma rede local via UDP Broadcast no porto `55435`.
  - Catálogo de "Salas Salvas" no `karamelo.ini` para reconexão rápida entre amigos.

---

## 5. Matriz de Priorização das Versões

| Recurso | Versão Alvo | Impacto | Complexidade |
| :--- | :---: | :---: | :---: |
| **⭐ Favoritos & Recentes no OSD** | **v0.9.6** | Alto | Baixa |
| **📸 Galeria e PNG em Screenshots** | **v0.9.6** | Médio | Baixa |
| **💬 Discord Rich Presence** | **v0.9.7** | Alto | Média |
| **🖼️ Molduras e Bezels Handheld/CRT** | **v0.9.7** | Muito Alto | Média |
| **🔄 Auto-Update de Ports no OSD** | **v0.9.8** | Alto | Média |
| **🌐 Netplay Lobby & Descoberta Local** | **v1.0.0** | Muito Alto | Alta |

---

*Documento revisado e homologado para orientação de arquitetos, mantenedores e agentes de IA do Karamelo Emulador.*
