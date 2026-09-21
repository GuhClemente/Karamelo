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

## 5. Diagnóstico & Crash Reporter Multiplataforma (Karamelo Crashlytics)

### 5.1 Visão Geral e Filosofia
- **Objetivo**: Descobrir, rastrear e corrigir proativamente falhas e crashes ocorridos nas máquinas dos jogadores, mesmo quando o usuário não sabe ou não tem tempo de abrir uma Issue no GitHub.
- **Princípios Inegociáveis**:
  - **100% Transparente & Configurável**: O usuário possui total autonomia para ativar ou desativar a qualquer momento nas configurações do OSD.
  - **Privacidade Absoluta (Zero Dados Pessoais)**: O relatório nunca inclui senhas, tokens, nomes de usuário do SO ou dados confidenciais. Apenas dados técnicos estritos de execução.
  - **Comunicação Educativa**: Explicar claramente que o recurso ajuda a comunidade a sanar bugs e acelerar o desenvolvimento, mas que sua desativação impedirá a equipe de identificar problemas automaticamente.

---

### 5.2 Captura de Falhas Multiplataforma (Nativa em C++)

```
                     ┌─────────────────────────────────────────┐
                     │           OCORRÊNCIA DE FALHA           │
                     └────────────────────┬────────────────────┘
                                          │
                  ┌───────────────────────┴───────────────────────┐
                  ▼                                               ▼
         [Windows x64]                                   [Linux / macOS]
    SetUnhandledExceptionFilter()                       sigaction() Handlers
  + CheckWindowsCrashReportsOnStartup()             (SIGSEGV, SIGBUS, SIGABRT, SIGFPE)
                  │                                               │
                  └───────────────────────┬───────────────────────┘
                                          ▼
                         [Gravação em crash_dump.txt]
                         • Versão do Karamelo & Build
                         • Sistema Operacional & Arquitetura
                         • Core Libretro & Jogo em Execução
                         • Código de Exceção / Sinal
                         • Stack Trace / RVAs no Executável
                                          │
                                          ▼
                               [Reinicialização do App]
                                          │
                     ┌────────────────────┴────────────────────┐
                     │  crash_reporting == 1 ?                 │
                     ├─────────────────────────┬───────────────┤
                     ▼ [SIM]                   ▼ [NÃO]
           [Envio Assíncrono POST]        [Apaga crash_dump.txt]
          karamelo-emu.com/api/crash      Nenhum dado transmitido
                     │
                     ▼
          [Notificação Webhook Discord]
```

#### Windows x64:
- Já conta com `SetUnhandledExceptionFilter(CrashHandler)` e monitoramento de `WER` (Windows Error Reporting). Captura códigos de exceção, endereços de memória, módulos faltosos e realiza stack walk por RVAs do executável.

#### Linux x64 e macOS ARM64:
- Instalação de manipuladores de sinal via `sigaction`:
  - `SIGSEGV` (Violação de segmentação / memória inválida)
  - `SIGBUS` (Erro de barramento / desalinhamento)
  - `SIGABRT` (Aborto do processo)
  - `SIGFPE` (Exceção de ponto flutuante)
  - `SIGILL` (Instrução ilegal)
- Emissão de stack trace via `backtrace()` e `backtrace_symbols()` (POSIX / macOS) ou captura de registradores de CPU do contexto `ucontext_t`.

---

### 5.3 Configuração e Transparência no OSD (`Settings`)

No menu **`Settings > Diagnóstico / Privacidade`** (ou integrado em `Settings > About`):

```
┌────────────────────────────────────────────────────────────┐
│                    DIAGNOSTICO & PRIVACIDADE               │
├────────────────────────────────────────────────────────────┤
│  Relatorio de Falhas     < Ativado (Recomendado) >         │
│  Abrir Pasta de Logs     >                                 │
│  Reportar Bug no GitHub  >                                 │
│                                                            │
│  [INFO]: O envio automatico de falhas ajuda a sanar bugs   │
│  rapidamente. Ao desativar, problemas ocorridos no seu     │
│  sistema nao serao detectados automaticamente pela equipe. │
│  Nenhum dado pessoal ou confidencial e transmitido.        │
└────────────────────────────────────────────────────────────┘
```

#### Persistência no `Config/karamelo.ini`:
```ini
[Diagnostics]
crash_reporting=1      ; 1 = Ativado (Padrão), 0 = Desativado
crash_notify_osd=1     ; 1 = Exibe aviso discreto pós-crash no OSD
```

---

### 5.4 Sanitização e Proteção de Dados (Filtro de Privacidade)

Antes de qualquer transmissão de log ou dump de erro:
1. **Remoção de Nomes de Usuário do Sistema**:
   - `C:\Users\Fulano\Documents\` -> `C:\Users\[USER]\Documents\`
   - `/Users/ciclano/` -> `~[USER]/`
   - `/home/beltrano/` -> `~[USER]/`
2. **Mascaramento de Credenciais**:
   - Tokens de RetroAchievements: `token=a1b2c3d4...` -> `token=[REDACTED]`
   - Senhas ou hashes presentes no arquivo `.ini` são estritamente excluídos.
3. **Payload Transmitido (JSON Seguro)**:
   ```json
   {
       "app_version": "0.9.5",
       "os": "macOS 15.0 arm64",
       "core": "ParaLLEl N64 (n64_parallel.dylib)",
       "game_stem": "Super Mario 64",
       "fault_type": "SIGSEGV (0x0000000B)",
       "fault_module": "n64_parallel.dylib",
       "stack_trace": [
           "0x000000010012a4c0",
           "0x000000010012bc88",
           "0x0000000100140210"
       ],
       "timestamp": "2026-09-21T19:25:00Z"
   }
   ```

---

### 5.5 Backend e Integração com o Servidor (`karamelo-emu.com`)

1. **Endpoint REST Leve**:
   - `POST https://karamelo-emu.com/api/crash-report`
   - Implementado no container Next.js ou microserviço Go/Node existente.
   - Salva os relatórios em `/data/crash-reports/YYYY-MM/` organizados por versão e core.
2. **Notificação Instantânea para a Equipe**:
   - Disparo automático de Webhook para canal privado `#crash-reports` no Discord do Guh Clemente:
     - Título: 🚨 `[CRASH DETECTADO] Karamelo v0.9.5 - macOS arm64`
     - Detalhes: Core `n64_parallel`, Jogo `Super Mario 64`, Falha `SIGSEGV offset 0x4A12`.

---

## 6. Matriz de Priorização das Versões

| Recurso | Versão Alvo | Impacto | Complexidade |
| :--- | :---: | :---: | :---: |
| **⭐ Favoritos & Recentes no OSD** | **v0.9.6** | Alto | Baixa |
| **📸 Galeria e PNG em Screenshots** | **v0.9.6** | Médio | Baixa |
| **🛡️ Crash Reporter Nativo Multiplataforma** | **v0.9.7** | Crítico | Média |
| **💬 Discord Rich Presence** | **v0.9.7** | Alto | Média |
| **🖼️ Molduras e Bezels Handheld/CRT** | **v0.9.7** | Muito Alto | Média |
| **🔄 Auto-Update de Ports no OSD** | **v0.9.8** | Alto | Média |
| **🌐 Netplay Lobby & Descoberta Local** | **v1.0.0** | Muito Alto | Alta |

---

*Documento revisado e homologado para orientação de arquitetos, mantenedores e agentes de IA do Karamelo Emulador.*

