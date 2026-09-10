# Licença — GNU General Public License v3.0

Copyright (C) 2026 Gustavo Clemente — <https://karamelo-emu.com>

Este programa é software livre: você pode redistribuí-lo e/ou modificá-lo sob
os termos da **GNU General Public License, versão 3**, publicada pela Free
Software Foundation.

Este programa é distribuído na esperança de ser útil, mas **SEM NENHUMA
GARANTIA**, nem mesmo a garantia implícita de COMERCIABILIDADE ou ADEQUAÇÃO A
UM DETERMINADO FIM. Veja a GNU General Public License para mais detalhes.

O texto completo e oficial da licença está em [LICENSE.md](../LICENSE.md), na
raiz do repositório, e também em <https://www.gnu.org/licenses/gpl-3.0.html>.
Aquele arquivo é a licença; **este aqui é só a explicação em português e não
tem valor legal** — onde os dois divergirem, vale o texto da GPL.

## O que isto significa, em português claro

Qualquer pessoa pode usar, estudar, modificar e redistribuir este código,
inclusive comercialmente. **Em troca, quem distribuir uma versão modificada é
obrigado a entregar o código-fonte dela sob esta mesma licença.** Não existe
fork fechado: melhorias feitas em cima deste projeto voltam para quem recebe o
programa.

Isto é uma decisão deliberada. O projeto esteve, por um único dia, sob a
PolyForm Noncommercial 1.0.0, que proibia uso comercial mas **permitia**
justamente o que mais importava impedir — pegar o código, fechar e não
devolver nada. A troca para a GPL-3 inverte as duas coisas de propósito:
vender é permitido, fechar não é.

Quem recebeu uma cópia sob a PolyForm continua com aqueles termos para aquela
cópia — uma licença concedida não se revoga. Todo o código a partir de
09/09/2026 é GPL-3.

## Escopo desta licença

Ela cobre **o código do Karamelo Emulador** — o que está em `src/`, `include/`
(menos os cabeçalhos de terceiros identificados abaixo), `tests/`, `tools/`,
`packaging/` e os scripts de build na raiz.

Ela **não** cobre, e não pode cobrir:

* O código de terceiros vendorizado em `third_party/`, que continua sob as
  licenças originais de cada projeto. Todas são compatíveis com a GPL-3:
  SDL3 (zlib), rcheevos (MIT), libchdr (BSD-3-Clause) e a fonte Unscii (CC0).
  Combiná-las num programa GPL-3 é permitido; o que a GPL exige é que o
  conjunto distribuído seja entregue sob os termos dela, preservando os avisos
  originais de cada uma.
* Os cores libretro distribuídos em `cores/`. São **programas independentes**,
  carregados em tempo de execução pela API libretro, cada um sob a licença do
  seu próprio projeto — agregação, não obra derivada. O `n64_gopher.dll` é
  GPL-3.0 por conta própria, o que agora coincide com a licença do frontend
  mas continua sendo decisão do projeto dele.
* Os jogos recompilados da categoria "Ports & Recomp", baixados na hora
  direto do repositório de cada projeto.
* BIOS e ROMs. Não acompanham o projeto e nunca acompanharam.

Quem é quem, arquivo por arquivo: [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md)
para as bibliotecas e [CREDITS.md](../CREDITS.md) para os cores e ports.
