# Auditoria Completa do Código-Fonte — Karamelo Emulador (`v0.985`, Nov/2026)

Relatório técnico da auditoria integral dos módulos C++20 em `src/` e `include/` realizada sobre `main` (`f9d2eb8`), seguido do registro das correções aplicadas.

---

## 1. Achados Críticos (10)

1. **`src/archive_helper.cpp` — Exclusão recursiva do diretório de trabalho via `stem()`**
   - `fs::path("...zip").stem()` retorna `".."`, resolvendo `cache_dir` para `<cwd>/cache/..` (`<cwd>`) antes de `fs::remove_all(cache_dir)`.
   - **Correção**: sanitizar `stem` (rejeitar `.`/`..`/vazio/separadores, substituir por fallback seguro) e confirmar que `cache_dir` é descendente estrito de `<cwd>/cache`.

2. **`src/x360_dump.cpp` — Heap buffer overflow em `XexToolUncrypted`**
   - Entradas `kHeaderImportLibraries` duplicadas e overflow de `uint32_t` em `pos + import_size + 0xFFF` permitiam `memcpy` fora dos limites de `out`.
   - **Correção**: rejeitar `kHeaderImportLibraries` duplicado, acumular offsets em `uint64_t` com teto de 64 MiB e validar `val + body.size() <= out.size()` e `signed_end <= data_offset`.

3. **`src/core_runner.cpp` + `src/fsr1.cpp` — Data race multi-thread e corrupção in-place em CAS (`src == dst`)**
   - `FidelityFxCasWithPitch` era chamado com `src == dst`, lendo vizinhos `(x±1, y±1)` enquanto escrevia no mesmo buffer em bandas paralelas.
   - **Correção**: copiar para buffer temporário quando `src == dst` em `Fsr1RcasWithPitch` (ou usar buffer scratch reutilizável).

4. **`src/core_runner.cpp` — `core_pixel_format` não resetado entre cargas**
   - Permanecia `XRGB8888` de um jogo anterior ao carregar um core que depende do default `0RGB1555` da spec libretro (over-read 2×).
   - **Correção**: resetar `core_pixel_format = RETRO_PIXEL_FORMAT_0RGB1555` em `CoreLoad()` e `CoreUnload()`.

5. **`src/core_runner.cpp` — Use-After-Free em `CoreSetPlaybackMute` e race em `g_audio_tap_callback`**
   - `CoreSetPlaybackMute` acessava `g_audio_stream` sem sincronização enquanto `StopAudioContext` o destruía; `g_audio_tap_callback` era ponteiro não-atômico.
   - **Correção**: manter referência protegida por `audio_ctx_lock` ao aplicar mute/gain e tornar `g_audio_tap_callback` `std::atomic`.

6. **`src/core_runner.cpp` (POSIX) — `RecoverAfterKilledCore` destruía mutexes ativos**
   - No `#else` (Linux/macOS), chamava `DeleteCriticalSection` + `InitializeCriticalSection` em `options_lock`, `name_lock` e `toast_lock` sem verificar se outra thread viva detinha o lock.
   - **Correção**: evitar destruir/reinicializar `pthread_mutex_t` em uso por outras threads vivas; apenas liberar/resetar com segurança quando aplicável.

7. **`src/crash_reporter.cpp` — Chamadas async-signal-unsafe em `PosixSignalHandler`**
   - Alocava `std::string`, chamava `backtrace_symbols()`/`free()`, adquiria `std::mutex` e chamava `fopen`/`fprintf`/`gmtime` dentro do handler de sinal.
   - **Correção**: usar buffers fixos lock-free para `core`/`game`, `open`/`write`/`close` e `backtrace_symbols_fd` no POSIX; no Windows, checar `gmtime_s` e proteger escritas.

8. **`src/web_admin.cpp` — `FD_SET(-1, ...)` e corrupção de stack em `WebAdminStop()`**
   - Fechava `g_listen_sock` e atribuía `-1` antes de `g_web_thread.join()`.
   - **Correção**: aguardar `g_web_thread.join()` antes de fechar `g_listen_sock` (usando `shutdown()`/`g_web_running = false` para despertar `select()`).

9. **`src/desktop_chat.cpp` — Use-After-Free em `DesktopChatGetKarameloTag()`**
   - Retornava `g_karamelo_tag.c_str()` após soltar `g_chat_mutex`.
   - **Correção**: copiar para buffer `thread_local char` sob `g_chat_mutex`.

10. **`include/compat_win32.h` — Race e `pthread_join`/`pthread_detach` duplo**
    - `WaitForSingleObject` e `CloseHandle` acessavam `h->thread.joined` fora de `h->thread.mtx`.
    - **Correção**: proteger o estado de ciclo de vida da thread (`0 = running`, `1 = joining`, `2 = joined/detached`) integralmente sob `h->thread.mtx`.

---

## 2. Achados Altos e Médios Corrigidos

### Archive, X360, Ports, Updater, CHD, RetroAchievements e JSON
- `src/archive_helper.cpp`: validação de componente `..` (evitando falso positivo em `Game..v2.sfc`), quoting seguro de `\` no Windows, remoção de `::tolower` com `char` assinado, fallback `7z` chamando `reset_dest()`, `PowerShell` sem `catch {}` silencioso, e timeout real com `TerminateProcess` / `poll()`.
- `src/x360_dump.cpp`: checagem de `_ftelli64(f) >= 0`.
- `src/port_runner.cpp`: `FindBestExecutable` passa a retornar `BestExeAmong(nested)`; `PortLaunch` aceita ports com `downloads_json_url` sem `repo`; `setup_relpath` executado via `CreateProcessW` direto sem encadeamento vulnerável em `cmd.exe /c`; `std::error_code` em `fs::exists`/`fs::absolute`; `try/catch` em `std::thread`; retorno correto em `SymphonyRecomp`.
- `src/port_catalog.cpp`: checagem de finitude e faixa em `GetInt` (sem UB); validação de `repo`, `release_tag` e `iso_dirs`.
- `src/updater.cpp`: rejeitar HTTP e usar `WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP` no Windows; `std::error_code` e `GetCurrentExecutablePath() + ".old"` em `UpdaterInit`; checagem de retorno de `fwrite`; `EINTR` em `waitpid`; `ren` no rollback do `.bat`; `unsigned char` em `tolower`.
- `src/chd_reader.cpp`: suporte a `RC_HASH_CDTRACK_LAST`, `RC_HASH_CDTRACK_LARGEST` e `RC_HASH_CDTRACK_FIRST_OF_SECOND_SESSION`, retorno `NULL` quando faixa não existe, `return false` em metadado negativo em `ReadTrackTable`, soma de `postgap` e proteção contra overflow.
- `src/retroachievements.cpp`: buffer seguro em `RaGetGameTitle()` (elimina UAF), `RaHasToken()` sob `g_client_lock`, rechecagem de `g_client` sob lock, `g_http_lock` em `RaShutdown`, `curl` POSIX com `--connect-timeout`/`--max-time` e fechamento de FDs no filho, `chmod 0600` em `retroachievements.cfg`, `wct` zerado no Windows.
- `src/json_mini.cpp`: rejeitar low surrogates isolados (`0xDC00`–`0xDFFF`) e parse de números independente de `LC_NUMERIC` (com validação de finitude).

### Core Runner, HW Render, Color Correction e Crash Reporter
- `src/hw_render_vulkan.cpp`: zerar todos os handles órfãos após timeout em `VkHwContextDestroy`, limpar `g_pending_num_semaphores`/`g_pending_signal_semaphore` em `VkHwShutdown`, checar retornos de `vkBindBufferMemory_`/`vkWaitForFences_` e limpar `g_pending_image_valid` em erro.
- `src/hw_render.cpp`: substituir `signal()` por `sigaction()` preservando `SA_SIGINFO` no POSIX; limpar `g_fbo`/`g_color_tex`/`g_depth_rb` se FBO ficar incompleto; resetar `GL_PACK_ROW_LENGTH` em `HwReadPixels`.
- `src/hw_render_d3d11.cpp`: incluir `DXGI_FORMAT_R8G8B8A8_UNORM_SRGB` em `needs_swap` e validar MSAA (`SampleDesc.Count == 1`).
- `src/color_correction.cpp`: permitir 3D LUT `.cube` customizada quando `lut_mode == LUT_MODE_OFF` e remover expressão morta `(1.0f - 1.0f)`.
- `src/core_runner.cpp`: resetar `p_retro_*` em falhas de `CoreLoad`; abortar `CoreLoadGame` se `surface_ok == false`; proteger `loaded_system_dir` e usar `std::error_code` em `fs::absolute`; retornar `g_output_sample_rate` em `CoreGetAudioSampleRate()`; usar `_wfopen` no Windows para SRAM/savestate/screenshot; mover `InterlockedExchange64(&toast_expires_ms, ...)` para dentro de `toast_lock`; corrigir separador `"/"` em `msx_sys_dir` no POSIX; aplicar CVD/LUT/CAS também no `filter_mode == 3`; sincronizar métricas de render e `CoreLogPrintf`.

### Web Admin, Desktop Chat, Cloud Sync, Voice Chat, Netplay, Telemetria e DSU
- `src/web_admin.cpp`: validar caminho em `/api/remote` (`launch`) dentro de `roms/` ou `ports/`; recusar sessão vazia se `RandomHexToken` falhar; corrigir delimitadores em `ExtractHeaderValue`/`GetQueryParam`; separar `ec` em `/api/upload`; checar retorno de `fs::remove` em `/api/roms/delete`; `thread_local` nos buffers de retorno; `waitpid` sem zumbis no POSIX; limitar body pré-auth.
- `src/desktop_chat.cpp`: regenerar `peer.room_code` a partir de `from_ip` em pacotes UDP `'p'` (evita spoofing); `thread_local` em `DesktopChatGetNickname`; `localtime_s`/`localtime_r`; armazenar porta real via `getsockname()` quando `port == 0`; proteger `g_extra_peer_*` e `g_scroll_offset`.
- `src/cloud_sync.cpp`: proteger `g_client_id`/`g_client_secret`/`g_last_sync_utc` sob `g_cloud_mutex`; `thread_local` nos getters de status/tempo; evitar sync concorrente em `CloudSyncStartOAuthFlow`; URL-encode de `f.id`; validar newline em headers `curl -K` e criar `out_file` com `0600` (via `mkstemp`/permissão restrita) no POSIX; corrigir timeout real do callback OAuth.
- `src/voice_chat.cpp`: mover `SDL_QuitSubSystem(SDL_INIT_AUDIO)` fora da thread secundária (ou manter no teardown principal); proteger estado DSP e validar salto absurdo de `seq` UDP.
- `src/netplay.cpp` + `src/netplay_protocol.cpp`: adicionar `exceptfds` + timeout ao `select()` de `NETPLAY_CONNECTING` no Windows; validar `host_ip`/`INADDR_NONE` e retorno de `connect()`; limitar `NET_BLOB_MASK` a `kNetMaskMaxBlocks` (512); usar `std::unique_lock` em vez de `unlock()`/`lock()` sob `lock_guard`; deadline em `relay_stage == 2`; `inet_ntop`.
- `src/telemetry.cpp`: `std::error_code` em `fs::create_directories`/`fs::exists`; `curl -f` no POSIX; arquivo temporário seguro no dir temp.
- `src/dsu_client.cpp`: checar `from_addr` contra `s_server_addr`, null-checks em `DsuDecodePacket`, resetar `from_len` no loop, mutex em `s_latest_motion` e expiração de `connected`.

### Menu, Input, Compat, OSD, Gamepad, Tray e Plataformas
- `src/menu.cpp` + `src/input_map.cpp`: carregar bindings `=0` globais corretamente em `MenuLoadSettings()`; completar `ResetAllSettingsToDefault()` (9 configs + preset); corrigir `action_id == 799` com perfil de sistema ativo; mover handler `KEY_SELECT` de `action_id == 310` para `STATE_MAIN`/`STATE_VIDEO`; corrigir transições de `MenuGoBack()` (`STATE_ABOUT`, `STATE_DIAGNOSTICS`, `STATE_RESET_CONFIRM`); incluir OpenBOR (`138`) em `g_system_count`; mover `PortGetAvailableList()` para fora dos loops de favoritos/recentes.
- `include/compat_win32.h`: corrigir `MultiByteToWideChar` (UTF-8 real, `cbMultiByte`, sem overflow de NUL), `Sleep()` com `nanosleep`, `GetModuleFileNameA` com `nSize == 0`, e `std::atomic` em `TerminateThread`.
- `src/osd.cpp`: resetar `col = 0` e preencher resto da linha em `\r`/`\n`; guarda de divisão por zero em `RespawnStar`.
- `src/gamepad_sdl.cpp`: chamar `SDL_OpenGamepad` fora de `AcquireSRWLockExclusive`; limitar retorno de `snprintf` em `MapUnknownJoystick`.
- `src/tray_manager.cpp`: proteger `g_tray_hwnd`/`g_tray_added` sob `g_tray_mutex` e enviar `PostMessageW(target_hwnd, WM_NULL, 0, 0)` após `TrackPopupMenu`.
- `src/main_win32.cpp` + `src/main_linux.cpp`: adicionar `F11` (Tela Cheia) no Windows; checar `if (!hdc) return;` em `PaintNow`; aceitar `Karamelo` versionado no WER; chamar `PortShutdown()` nos modos CLI; adicionar frame pacer sem V-Sync, persistência de geometria, `GamepadShutdown()` após `CoreShutdown()` e `pclose(fp_rec_video)` ao sair no Linux/macOS.
