import os
import sys
from PIL import Image

def convert_all_wallpapers(wallpapers_dir, target_w=640, target_h=360):
    if not os.path.isdir(wallpapers_dir):
        print(f"Directory not found: {wallpapers_dir}")
        return

    files = [f for f in os.listdir(wallpapers_dir) if f.lower().endswith(('.jpg', '.jpeg', '.png', '.bmp'))]
    if not files:
        print("No image files found to convert.")
        return

    print(f"Converting images in {wallpapers_dir} to {target_w}x{target_h} 32-bit RAW (BGRA)...")
    converted_count = 0

    for filename in sorted(files):
        src_path = os.path.join(wallpapers_dir, filename)
        stem = os.path.splitext(filename)[0]
        dst_path = os.path.join(wallpapers_dir, stem + ".raw")

        try:
            with Image.open(src_path) as img:
                img_resized = img.resize((target_w, target_h), Image.Resampling.LANCZOS).convert("RGBA")
                r, g, b, a = img_resized.split()
                bgra = Image.merge("RGBA", (b, g, r, a))
                raw_bytes = bgra.tobytes()

                with open(dst_path, "wb") as f_out:
                    f_out.write(raw_bytes)

            converted_count += 1
            print(f"  [OK] {filename} -> {stem}.raw ({len(raw_bytes)} bytes)")
        except Exception as e:
            print(f"  [FAIL] {filename}: {e}")

    print(f"\nDone! Successfully converted {converted_count} files.")

if __name__ == "__main__":
    default_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "app", "Wallpapers"))
    target_dir = sys.argv[1] if len(sys.argv) > 1 else default_dir
    convert_all_wallpapers(target_dir)
