#!/usr/bin/env python3
"""Gera o icone do Karamelo (src/karamelo.ico) por codigo, em vez de guardar
so o binario.

O icone anterior era um chip de circuito com um "M" grande no meio - marca do
nome antigo. Este e desenhado do zero, e por ser codigo pode ser reajustado sem
depender de reabrir um arquivo de arte que ninguem tem.

Decisoes de desenho, todas ditadas pelo tamanho de 16x16:

- O "K" e desenhado como poligono, nao como texto. Fonte dependeria de qual
  esta instalada na maquina que gera, e o resultado mudaria de um build para
  outro. Poligono e identico em qualquer lugar.
- Nada de detalhe fino. A 16px so sobrevivem duas coisas: a silhueta e o
  contraste entre fundo e letra. Os pinos do chip e o brilho existem para os
  tamanhos grandes e simplesmente desaparecem nos pequenos, sem virar sujeira.
- Tudo e desenhado a 1024x1024 e reduzido com LANCZOS. Desenhar direto em 16px
  produz borda serrilhada; reduzir a partir de um mestre grande da antialiasing
  de graca.

Uso:  python tools/make_icon.py [saida.ico]
"""

import sys
from PIL import Image, ImageDraw, ImageFilter

M = 1024  # lado do mestre
SIZES = [16, 32, 48, 64, 128, 256]

# Caramelo: ambar quente por cima, tostado por baixo.
CARAMEL_TOP = (245, 176, 74)
CARAMEL_BOT = (176, 84, 18)
PLAQUE = (26, 20, 16)
PLAQUE_EDGE = (58, 44, 34)
CREAM = (255, 245, 230)
PIN = (208, 150, 74)


def rounded_rect_mask(size, box, radius):
    """Mascara em escala de cinza de um retangulo arredondado."""
    m = Image.new("L", size, 0)
    ImageDraw.Draw(m).rounded_rectangle(box, radius=radius, fill=255)
    return m


def vertical_gradient(size, top, bottom):
    """Gradiente vertical, uma linha por vez - basta para 1024px."""
    g = Image.new("RGB", size)
    d = ImageDraw.Draw(g)
    h = size[1]
    for y in range(h):
        t = y / max(1, h - 1)
        d.line(
            [(0, y), (size[0], y)],
            fill=tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)),
        )
    return g


def draw_k(draw, cx, cy, h):
    """Pinta o "K" na mascara: haste + dois tracos grossos partindo do vertice.

    A primeira versao usava tres poligonos separados e o braco terminava em
    mid + 0.02h enquanto a perna comecava em mid - 0.02h: sobrava uma fresta de
    0.04h bem na juncao, e a sombra por tras aparecia dentro da letra. Tracos
    com largura se sobrepoem no vertice, entao a mascara sai continua e nao
    existe borda interna para serrilhar.
    """
    w = 0.20 * h                    # espessura da haste e dos tracos
    x0 = cx - 0.36 * h              # borda esquerda da haste
    top, bot = cy - 0.5 * h, cy + 0.5 * h
    xr = cx + 0.42 * h              # ponta direita dos tracos
    vx = x0 + 0.5 * w               # vertice, dentro da haste

    draw.rectangle([x0, top, x0 + w, bot], fill=255)
    draw.line([(vx, cy), (xr, top)], fill=255, width=int(w))
    draw.line([(vx, cy), (xr, bot)], fill=255, width=int(w))


def build_master():
    img = Image.new("RGBA", (M, M), (0, 0, 0, 0))

    # 1. Placa de fundo: quadrado arredondado escuro, com uma borda mais clara
    #    so para destacar o icone contra fundo escuro do Windows.
    pad = int(M * 0.055)
    plaque_box = (pad, pad, M - pad, M - pad)
    plaque_r = int(M * 0.22)
    edge = Image.new("RGBA", (M, M), (0, 0, 0, 0))
    ImageDraw.Draw(edge).rounded_rectangle(
        plaque_box, radius=plaque_r, fill=PLAQUE + (255,), outline=PLAQUE_EDGE + (255,),
        width=int(M * 0.012)
    )
    img = Image.alpha_composite(img, edge)

    # 2. Pinos do chip: mantem a leitura de "hardware" que o icone antigo tinha,
    #    sem depender deles - somem nos tamanhos pequenos.
    pins = Image.new("RGBA", (M, M), (0, 0, 0, 0))
    dp = ImageDraw.Draw(pins)
    pin_w, pin_h, pin_r = int(M * 0.030), int(M * 0.075), int(M * 0.012)
    for i in range(3):
        y = int(M * (0.33 + i * 0.17))
        dp.rounded_rectangle(
            (int(M * 0.025), y, int(M * 0.025) + pin_w, y + pin_h), pin_r, fill=PIN + (255,)
        )
        dp.rounded_rectangle(
            (M - int(M * 0.025) - pin_w, y, M - int(M * 0.025), y + pin_h), pin_r,
            fill=PIN + (255,)
        )
    img = Image.alpha_composite(img, pins)

    # 3. Pastilha de caramelo com gradiente, recortada em quadrado arredondado.
    ipad = int(M * 0.125)
    inner_box = (ipad, ipad, M - ipad, M - ipad)
    inner_r = int(M * 0.16)
    grad = vertical_gradient((M, M), CARAMEL_TOP, CARAMEL_BOT).convert("RGBA")
    grad.putalpha(rounded_rect_mask((M, M), inner_box, inner_r))
    img = Image.alpha_composite(img, grad)

    # 4. Brilho: faixa clara na metade de cima, como caramelo lustroso.
    gloss = Image.new("RGBA", (M, M), (0, 0, 0, 0))
    ImageDraw.Draw(gloss).ellipse(
        (ipad - int(M * 0.05), ipad - int(M * 0.30),
         M - ipad + int(M * 0.05), ipad + int(M * 0.22)),
        fill=(255, 255, 255, 46),
    )
    gloss.putalpha(
        Image.composite(gloss.getchannel("A"),
                        Image.new("L", (M, M), 0),
                        rounded_rect_mask((M, M), inner_box, inner_r))
    )
    img = Image.alpha_composite(img, gloss.filter(ImageFilter.GaussianBlur(M * 0.012)))

    # 5. O "K". Sombra deslocada primeiro, letra por cima: da separacao mesmo
    #    quando o caramelo por tras esta claro.
    off = M * 0.012

    k_mask = Image.new("L", (M, M), 0)
    draw_k(ImageDraw.Draw(k_mask), M * 0.47, M * 0.5, M * 0.50)

    shadow_mask = Image.new("L", (M, M), 0)
    draw_k(ImageDraw.Draw(shadow_mask), M * 0.47 + off, M * 0.5 + off, M * 0.50)
    shadow = Image.new("RGBA", (M, M), (90, 40, 8, 0))
    shadow.putalpha(shadow_mask.point(lambda v: v * 120 // 255))
    img = Image.alpha_composite(img, shadow.filter(ImageFilter.GaussianBlur(M * 0.008)))

    letter = Image.new("RGBA", (M, M), CREAM + (0,))
    letter.putalpha(k_mask)
    img = Image.alpha_composite(img, letter)

    return img


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "src/karamelo.ico"
    master = build_master()

    frames = [master.resize((s, s), Image.LANCZOS) for s in SIZES]
    # save() com sizes= re-reduz a partir da primeira imagem; passando os frames
    # prontos, cada tamanho vem do mestre e nao de uma reducao de reducao.
    frames[-1].save(out, format="ICO", sizes=[(s, s) for s in SIZES],
                    append_images=frames[:-1])
    print("gravado: %s (%s)" % (out, ", ".join("%dx%d" % (s, s) for s in SIZES)))

    master.resize((256, 256), Image.LANCZOS).save("scratch/icon_preview_256.png")
    sheet = Image.new("RGBA", (sum(SIZES) + 20 * len(SIZES), 300), (64, 64, 64, 255))
    x = 10
    for s, f in zip(SIZES, frames):
        sheet.paste(f, (x, 150 - s // 2), f)
        x += s + 20
    sheet.save("scratch/icon_preview_sizes.png")
    print("previews em scratch/")


if __name__ == "__main__":
    main()
