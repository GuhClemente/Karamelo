import os
from PIL import Image, ImageDraw, ImageFont

artifact_dir = "/Users/guhf/.gemini/antigravity/brain/04cd2dfb-3d28-4e35-8213-7939406bf5c4"
os.makedirs(artifact_dir, exist_ok=True)

title_paths = [
    "app/screenshots/wave_race_mode0_native_title_raw_320x240.bmp",
    "app/screenshots/wave_race_mode1_upscale2x_title_raw_640x240.bmp",
    "app/screenshots/wave_race_mode2_upscale4x_title_raw_1280x240.bmp",
]

race_paths = [
    "app/screenshots/wave_race_mode0_native_race_raw_320x240.bmp",
    "app/screenshots/wave_race_mode1_upscale2x_race_raw_640x240.bmp",
    "app/screenshots/wave_race_mode2_upscale4x_race_raw_1280x240.bmp",
]

title_imgs = [Image.open(p) for p in title_paths]
race_imgs = [Image.open(p) for p in race_paths]

mode_labels = [
    "Modo 0: Nativo (320x240)",
    "Modo 1: Upscale 2x (640x240)",
    "Modo 2: Upscale 4x (1280x240)",
]

# 1. Full frames in 4:3 1440x1080 (nearest neighbor)
for i, img in enumerate(title_imgs):
    full = img.resize((1440, 1080), Image.Resampling.NEAREST)
    out_path = os.path.join(artifact_dir, f"proof_title_mode{i}_full.png")
    full.save(out_path)
    print(f"Saved: {out_path}")

for i, img in enumerate(race_imgs):
    full = img.resize((1440, 1080), Image.Resampling.NEAREST)
    out_path = os.path.join(artifact_dir, f"proof_race_mode{i}_full.png")
    full.save(out_path)
    print(f"Saved: {out_path}")

# Function to create side-by-side comparison composite
def make_comparison(img_list, norm_box, out_filename, title_text, panel_w=500, panel_h=450):
    nx0, ny0, nx1, ny1 = norm_box
    crops = []
    for img in img_list:
        w, h = img.size
        box = (int(nx0 * w), int(ny0 * h), int(nx1 * w), int(ny1 * h))
        crop = img.crop(box)
        # Scale to identical panel size with NEAREST to expose horizontal resolution
        crop_scaled = crop.resize((panel_w, panel_h), Image.Resampling.NEAREST)
        crops.append(crop_scaled)

    # Composite canvas: header height 60, padding 10
    pad = 12
    hdr_h = 70
    comp_w = pad * 4 + panel_w * 3
    comp_h = hdr_h + panel_h + pad * 2
    comp = Image.new("RGB", (comp_w, comp_h), (20, 24, 33))
    draw = ImageDraw.Draw(comp)

    # Try font or fallback
    try:
        font_title = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 26)
        font_panel = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 18)
        font_badge = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 14)
    except:
        font_title = ImageFont.load_default()
        font_panel = ImageFont.load_default()
        font_badge = ImageFont.load_default()

    # Draw header title
    draw.text((pad, 16), title_text, fill=(255, 215, 0), font=font_title)

    for i, crop in enumerate(crops):
        x = pad + i * (panel_w + pad)
        y = hdr_h
        # Draw panel background border
        draw.rectangle((x - 2, y - 2, x + panel_w + 1, y + panel_h + 1), outline=(60, 70, 90), width=2)
        comp.paste(crop, (x, y))

        # Panel header / label badge
        badge_text = mode_labels[i]
        draw.rectangle((x, y, x + panel_w, y + 30), fill=(10, 14, 20, 200))
        draw.text((x + 10, y + 6), badge_text, fill=(0, 230, 255) if i > 0 else (200, 200, 200), font=font_panel)

    out_path = os.path.join(artifact_dir, out_filename)
    comp.save(out_path)
    print(f"Comparison saved: {out_path}")

# Regions:
# 1. Title Screen: Kawasaki & Wave Race Logo
make_comparison(title_imgs, (0.15, 0.12, 0.85, 0.50), "comparison_title_logo.png",
                "Comparacao Tela de Titulo: Logo WAVE RACE 64")

# 2. Title Screen: Jet Ski Model
make_comparison(title_imgs, (0.30, 0.40, 0.70, 0.88), "comparison_title_jetski.png",
                "Comparacao Tela de Titulo: Jet Ski & Piloto 3D")

# 3. Demo Race: Rider & Jet Ski in motion
make_comparison(race_imgs, (0.35, 0.45, 0.65, 0.82), "comparison_race_rider.png",
                "Comparacao Dolphin Park: Piloto & Spray de Agua 3D")

# 4. Demo Race: Water surface & Course buoys
make_comparison(race_imgs, (0.05, 0.40, 0.45, 0.75), "comparison_race_water.png",
                "Comparacao Dolphin Park: Malha de Ondas e Geometria da Agua")

# 5. Demo Race: HUD & Lap timer
make_comparison(race_imgs, (0.65, 0.05, 0.98, 0.30), "comparison_race_hud.png",
                "Comparacao Dolphin Park: HUD Superior (Posicao & Tempo)")

print("All comparisons successfully generated.")

