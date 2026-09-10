# Avisos de terceiros

O Karamelo Emulador é distribuído sob a [GNU GPL-3.0](LICENSE.md) (explicação em [docs/LICENCA.md](docs/LICENCA.md)), mas ele não
é feito só de código próprio. Este arquivo lista tudo que entra no binário ou é
distribuído junto, com a licença original de cada um.

Todas as bibliotecas abaixo são permissivas (zlib, MIT, BSD-3, CC0) e portanto
compatíveis com a GPL-3: podem ser combinadas num programa GPL-3 desde que os
avisos originais sigam junto — que é exatamente para isso que este arquivo
existe.

Isto aqui é **obrigação legal**, não cortesia: zlib, BSD-3 e MIT exigem que o
aviso de copyright acompanhe qualquer redistribuição, inclusive em forma
binária. Se este arquivo sair do pacote, a distribuição fica irregular.

Os cores libretro (`cores/`) e os jogos recompilados ("Ports & Recomp") **não**
estão aqui — são programas independentes de terceiros, cada um com licença
própria, e estão catalogados em [CREDITS.md](CREDITS.md).

---

## Compilado dentro do executável

### SDL3 — zlib License

Janela, entrada, gamepad e áudio. Vendorizado em `third_party/SDL3/`, linkado
estaticamente (`build/sdl3/SDL3-static.lib`). Não é modificado.

```
Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
```

### rcheevos — MIT License

Conquistas (RetroAchievements). Vendorizado em `third_party/rcheevos/`,
compilado junto com o app. Não é modificado.

```
MIT License

Copyright (c) 2018 RetroAchievements.org

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### libchdr — BSD 3-Clause

Leitura de imagens de disco `.chd`. Vendorizado em `third_party/libchdr/`,
compilado como unity build (`unity.c`). Não é modificado.

```
Copyright Romain Tisserand
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the <organization> nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

O libchdr traz três dependências próprias, e elas entram no binário junto com
ele:

#### miniz 3.1.2 — MIT License

`third_party/libchdr/deps/miniz-3.1.2/`

```
Copyright 2013-2014 RAD Game Tools and Valve Software
Copyright 2010-2014 Rich Geldreich and Tenacious Software LLC
All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
IN THE SOFTWARE.
```

#### Zstandard 1.5.7 — BSD 3-Clause (opção adotada)

`third_party/libchdr/deps/zstd-1.5.7/`. O zstd é licenciado em dupla
BSD-3 **ou** GPL-2, à escolha de quem usa; aqui vale a BSD-3, e é por isso que
o GPL-2 não alcança este projeto.

```
Copyright (c) Meta Platforms, Inc. and affiliates.
All rights reserved.

This source code is licensed under both the BSD-style license (found in the
LICENSE file in the root directory of this source tree) and the GPLv2 (found
in the COPYING file in the root directory of this source tree).
You may select, at your option, one of the above-listed licenses.
```

#### LZMA SDK 26.02 — domínio público

`third_party/libchdr/deps/lzma-26.02/`

```
LZMA SDK is placed in the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or distribute the
original LZMA SDK code, either in source code form or as a compiled binary,
for any purpose, commercial or non-commercial, and by any means.
```

---

## Só cabeçalhos (nada é linkado)

### API libretro — MIT-style, The RetroArch team

`include/libretro.h`, `include/libretro_vulkan.h`, `include/libretro_d3d11.h`.
São a definição da interface que os cores implementam. O aviso completo está
no topo de cada arquivo.

```
Copyright (C) 2010-2020 The RetroArch team

The following license statement only applies to these libretro API headers.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### Vulkan-Headers — Apache-2.0 OR MIT

Copyright 2015-2026 The Khronos Group Inc. Vendorizado em
`third_party/Vulkan-Headers/`, usado só como cabeçalho pelo renderer Vulkan.
Texto completo em `third_party/Vulkan-Headers/LICENSE.md`.

---

## Distribuído como DLL separada

### gopher64 — GPL-3.0

Este é o único caso de copyleft no projeto, e merece leitura atenta.

O `cores/n64_gopher.dll` é compilado por `build_gopher64.bat` a partir do
fonte em `third_party/gopher64/`, que é **GPL-3.0** — e nós **modificamos**
esse fonte (a correção de alinhamento de RDRAM em `src/device/rdram.rs`,
registrada também em `docs/patch-upstream-gopher64-rdram.patch`).

Consequências, para deixar explícito:

* Distribuir o `n64_gopher.dll` obriga a oferecer o fonte modificado
  correspondente, sob GPL-3.0. **Este repositório é essa oferta** — o fonte
  completo, com as modificações, está em `third_party/gopher64/`.
* A GPL-3.0 vale para o gopher64. Ela **não** se estende ao Karamelo: o core
  é um binário separado, carregado em tempo de execução pela API libretro,
  que é uma interface pública implementada por dezenas de programas
  independentes. O frontend não linka o gopher64 nem deriva do código dele.
* Texto completo da licença: `third_party/gopher64/LICENSE`.

Os demais cores em `cores/` não são compilados aqui — são baixados prontos
dos projetos de origem por `download_cores.ps1`. Cada um segue a própria
licença; a lista verificada está em [CREDITS.md](CREDITS.md).

---

## O que não é software

* **Wallpapers** (`app/Wallpapers/`) e **ícone** (`src/karamelo.ico`): arte do
  projeto, sob a mesma [LICENSE.md](LICENSE.md).
* **Fonte 8x8 do OSD** (`src/charrom.cpp`): bitmap desenhado para este
  projeto. Ver o cabeçalho do arquivo.
* **BIOS e ROMs**: não acompanham o projeto, não estão versionados e não são
  redistribuídos. `bios/`, `roms/` e `cores/` estão no `.gitignore`.
