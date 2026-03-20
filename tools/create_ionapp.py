#!/usr/bin/env python3
# ╔══════════════════════════════════════════════════════════════════════════╗
# ║              IonOS App Packager — create_ionapp.py                       ║
# ║                                                                          ║
# ║  Creates .ionapp packages that can be installed on IonOS devices         ║
# ║  via the built-in App Store (SD card installation).                      ║
# ║                                                                          ║
# ║  Usage:                                                                  ║
# ║    python3 tools/create_ionapp.py --manifest app.json                    ║
# ║    python3 tools/create_ionapp.py --manifest app.json --code app.bin     ║
# ║    python3 tools/create_ionapp.py --manifest app.json --icon icon.png    ║
# ║    python3 tools/create_ionapp.py --example                              ║
# ║                                                                          ║
# ║  Package format:  [256-byte header] [2048-byte icon] [N-byte code]       ║
# ╚══════════════════════════════════════════════════════════════════════════╝

import argparse, json, os, struct, sys, zlib
from datetime import datetime

try:
    from PIL import Image
    HAS_PIL = True
except ImportError:
    HAS_PIL = False

# ── Constants ─────────────────────────────────────────────────────────────────
MAGIC           = b"IONAPP\x00\x00"
FORMAT_VERSION  = 1
ICON_W, ICON_H  = 32, 32
ICON_BYTES      = ICON_W * ICON_H * 2   # RGB565
HEADER_SIZE     = 256
IONOS_VERSION   = "1.0.0"

# Permission flags
PERM_FLAGS = {
    "wifi":  0x01,
    "sd":    0x02,
    "audio": 0x04,
    "led":   0x08,
    "radio": 0x10,
    "adc":   0x20,
}

# ── CRC32 ─────────────────────────────────────────────────────────────────────
def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF

# ── Icon processing ───────────────────────────────────────────────────────────
def png_to_rgb565_swapped(path: str) -> bytes:
    """Convert PNG/JPEG to 32×32 RGB565 (byte-swapped for SPI display)."""
    if not HAS_PIL:
        raise RuntimeError("Pillow is required for icon conversion: pip install Pillow")
    img = Image.open(path).convert("RGB").resize((ICON_W, ICON_H), Image.LANCZOS)
    pixels = img.load()
    out = bytearray(ICON_BYTES)
    for y in range(ICON_H):
        for x in range(ICON_W):
            r, g, b = pixels[x, y]
            rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            # Byte-swap for ESP32-S3 SPI display (LV_COLOR_16_SWAP=1)
            swapped = ((rgb565 & 0xFF) << 8) | (rgb565 >> 8)
            idx = (y * ICON_W + x) * 2
            out[idx]     = (swapped >> 8) & 0xFF
            out[idx + 1] = swapped & 0xFF
    return bytes(out)

def default_icon(color_hex: int) -> bytes:
    """Generate a solid colour 32×32 icon when no PNG is provided."""
    r = (color_hex >> 16) & 0xFF
    g = (color_hex >> 8)  & 0xFF
    b =  color_hex        & 0xFF
    rgb565  = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    swapped = ((rgb565 & 0xFF) << 8) | (rgb565 >> 8)
    pixel   = struct.pack(">H", swapped)
    return pixel * (ICON_W * ICON_H)

# ── Header builder ────────────────────────────────────────────────────────────
def build_header(manifest: dict, code_size: int, code_crc: int, perms: int) -> bytes:
    """
    Header layout (256 bytes):
      0-7    magic[8]          "IONAPP\\0\\0"
      8      version           1
      9-11   reserved
      12-15  header_crc32      CRC of bytes 12-255
      16-79  app_id[64]
      80-103 app_name[24]
      104-115 app_version[12]
      116-147 author[32]
      148-227 desc[80]
      228-231 accent_color     uint32 RGB888
      232-235 code_size        uint32
      236-239 code_crc32       uint32
      240     perms            uint8
      241-255 _pad[15]
    """
    def pad(s: str, n: int) -> bytes:
        b = s.encode("utf-8")[:n-1] + b"\x00"
        return b.ljust(n, b"\x00")

    accent = int(manifest.get("icon_color", 0x00D4FF)) & 0xFFFFFF

    hdr = bytearray(HEADER_SIZE)
    hdr[0:8]    = MAGIC
    hdr[8]      = FORMAT_VERSION
    # bytes 9-11: reserved (zero)
    # bytes 12-15: header_crc (filled in after)
    hdr[16:80]  = pad(manifest["id"], 64)
    hdr[80:104] = pad(manifest["name"][:23], 24)
    hdr[104:116]= pad(manifest["version"], 12)
    hdr[116:148]= pad(manifest.get("author", "Unknown"), 32)
    hdr[148:228]= pad(manifest.get("desc", ""), 80)
    struct.pack_into(">I", hdr, 228, accent)        # accent_color big-endian
    struct.pack_into("<I", hdr, 232, code_size)
    struct.pack_into("<I", hdr, 236, code_crc)
    hdr[240]    = perms
    # Compute CRC of bytes 12-255
    hdr_crc = crc32(bytes(hdr[12:]))
    struct.pack_into("<I", hdr, 12, hdr_crc)
    return bytes(hdr)

# ── Package writer ────────────────────────────────────────────────────────────
def create_package(manifest: dict, code_path: str | None, icon_path: str | None,
                   output: str) -> None:
    print(f"\n╔{'═'*52}╗")
    print(f"║  IonOS App Packager v1.0                           ║")
    print(f"╚{'═'*52}╝\n")

    # Validate required fields
    for field in ("id", "name", "version"):
        if field not in manifest:
            raise ValueError(f"manifest.json missing required field: '{field}'")
    if "." not in manifest["id"]:
        raise ValueError("'id' must be reverse-domain format: com.example.myapp")
    if len(manifest["name"]) > 23:
        raise ValueError("'name' must be ≤23 characters")

    print(f"  App ID:      {manifest['id']}")
    print(f"  Name:        {manifest['name']}")
    print(f"  Version:     {manifest['version']}")
    print(f"  Author:      {manifest.get('author', 'Unknown')}")
    print(f"  Description: {manifest.get('desc', '(none)')[:60]}")

    # ── Icon ────────────────────────────────────────────────────────────────
    print(f"\n  [1/4] Processing icon…")
    if icon_path:
        icon_data = png_to_rgb565_swapped(icon_path)
        print(f"        {icon_path} → RGB565 32×32 ({len(icon_data)} bytes)")
    else:
        color = int(manifest.get("icon_color", 0x00D4FF))
        icon_data = default_icon(color)
        print(f"        Generated solid colour icon (#{color:06X})")

    assert len(icon_data) == ICON_BYTES, f"Icon must be exactly {ICON_BYTES} bytes"

    # ── Code binary ─────────────────────────────────────────────────────────
    print(f"\n  [2/4] Processing code binary…")
    if code_path:
        if not os.path.exists(code_path):
            raise FileNotFoundError(f"Code file not found: {code_path}")
        code_data = open(code_path, "rb").read()
        code_crc  = crc32(code_data)
        print(f"        {code_path} ({len(code_data)/1024:.1f} KB)  CRC32=0x{code_crc:08X}")
    else:
        code_data = b""
        code_crc  = 0
        print(f"        No code binary (script-only app)")

    # ── Permissions ─────────────────────────────────────────────────────────
    perms     = 0
    perm_list = manifest.get("perms", [])
    for p in perm_list:
        if p in PERM_FLAGS:
            perms |= PERM_FLAGS[p]
        else:
            print(f"  WARNING: Unknown permission '{p}' ignored")
    if perm_list:
        print(f"\n  [3/4] Permissions: {', '.join(perm_list)} (0x{perms:02X})")
    else:
        print(f"\n  [3/4] Permissions: none")

    # ── Assemble package ─────────────────────────────────────────────────────
    print(f"\n  [4/4] Assembling package…")
    header = build_header(manifest, len(code_data), code_crc, perms)

    # Verify header CRC ourselves before writing
    hdr_crc_check = crc32(header[12:])
    stored_crc    = struct.unpack_from("<I", header, 12)[0]
    assert hdr_crc_check == stored_crc, "BUG: Header CRC mismatch in packager"

    total_size = len(header) + len(icon_data) + len(code_data)

    with open(output, "wb") as f:
        f.write(header)
        f.write(icon_data)
        f.write(code_data)

    print(f"\n  ┌─────────────────────────────────────────┐")
    print(f"  │  Package created successfully!          │")
    print(f"  ├─────────────────────────────────────────┤")
    print(f"  │  Output:  {output:<31}│")
    print(f"  │  Size:    {total_size/1024:>6.1f} KB total                  │")
    print(f"  │    Header:  {len(header):>5} bytes                      │")
    print(f"  │    Icon:    {len(icon_data):>5} bytes (32×32 RGB565)     │")
    print(f"  │    Code:    {len(code_data):>5} bytes                      │")
    print(f"  └─────────────────────────────────────────┘")
    print(f"\n  Copy {output} to /sdcard/apps/ on your IonOS device.")
    print(f"  Then open App Store → Available → tap the package → Install.\n")


# ── Example project generator ─────────────────────────────────────────────────
def create_example(out_dir: str = "my_ionapp") -> None:
    os.makedirs(out_dir, exist_ok=True)

    manifest = {
        "id":         "com.example.helloion",
        "name":       "Hello IonOS",
        "version":    "1.0.0",
        "author":     "Your Name",
        "desc":       "A demo app showing the IonOS SDK in action.",
        "icon_color": 0x00D4FF,
        "min_ionos":  "1.0.0",
        "perms":      ["audio", "led"]
    }
    open(f"{out_dir}/manifest.json", "w").write(
        json.dumps(manifest, indent=2))

    readme = f"""\
# {manifest['name']} — IonOS App

## Building

```bash
# Build the app binary (requires IonOS SDK headers)
# This stub compiles a minimal app using the IonOS external app API.
# See sdk/include/ion_sdk.h for the full API.

idf.py build            # or your custom Makefile
cp build/helloion.bin ./app.bin

# Package it
python3 tools/create_ionapp.py \\
    --manifest manifest.json \\
    --code app.bin \\
    --icon icon.png \\
    --output HelloIonOS.ionapp
```

## Installing on IonOS

1. Copy `HelloIonOS.ionapp` to `/sdcard/apps/` on the SD card
2. Insert SD card into your IonOS device
3. Navigate to **App Store** on the home screen
4. Go to **Available** tab
5. Tap **Hello IonOS** → tap **Install App**
6. App appears on home screen after installation

## Package Contents

| File | Description |
|------|-------------|
| manifest.json | App metadata (this file) |
| icon.png | 32×32 app icon (PNG/JPEG, will be converted) |
| app.bin | Compiled app binary |

## manifest.json Fields

| Field | Required | Description |
|-------|----------|-------------|
| `id` | ✓ | Reverse-domain unique ID: `com.example.myapp` |
| `name` | ✓ | Display name (max 23 chars) |
| `version` | ✓ | Semantic version: `1.0.0` |
| `author` | | Author name |
| `desc` | | Short description (max 127 chars) |
| `icon_color` | | Accent colour as integer (default `0x00D4FF`) |
| `min_ionos` | | Minimum IonOS version required |
| `perms` | | List: `wifi`, `sd`, `audio`, `led`, `radio` |
"""
    open(f"{out_dir}/README.md", "w").write(readme)

    src = """\
// ╔══════════════════════════════════════════════════════════╗
// ║  Hello IonOS — Example External App                      ║
// ║  Built with the IonOS SDK                                ║
// ╚══════════════════════════════════════════════════════════╝
#include "ion_sdk.h"

class HelloIonApp : public IonApp {
public:
    void onCreate() override {
        buildScreen("Hello IonOS");

        // Animated accent card
        lv_obj_t* card = lv_obj_create(m_screen);
        lv_obj_set_size(card, 200, 130);
        lv_obj_align(card, LV_ALIGN_CENTER, 0, -20);
        UIEngine::stylePanel(card);

        lv_obj_t* ico = lv_label_create(card);
        lv_label_set_text(ico, LV_SYMBOL_STAR);
        lv_obj_set_style_text_color(ico, lv_color_hex(ION_COLOR_ACCENT), 0);
        lv_obj_set_style_text_font(ico, &lv_font_montserrat_24, 0);
        lv_obj_align(ico, LV_ALIGN_TOP_MID, 0, 8);

        lv_obj_t* title = lv_label_create(card);
        lv_label_set_text(title, "Hello IonOS!");
        UIEngine::styleLabel(title, ION_COLOR_TEXT, &lv_font_montserrat_20);
        lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

        lv_obj_t* sub = lv_label_create(card);
        lv_label_set_text(sub, "Press X to celebrate");
        UIEngine::styleLabel(sub, ION_COLOR_TEXT_DIM, &lv_font_montserrat_12);
        lv_obj_align(sub, LV_ALIGN_BOTTOM_MID, 0, -6);

        m_subId = IonKernel::getInstance().subscribeEvent(ION_EVENT_KEY_DOWN,
            [this](const ion_event_t& e) { onEvent(e); });
    }

    void onEvent(const ion_event_t& e) {
        if (e.data == ION_KEY_X) {
            ION_NOTIFY("Hello!", "IonOS is awesome!", ION_NOTIF_SUCCESS);
            ION_SOUND("success");
            WS2812Driver::getInstance().setAnimation(LEDAnim::RAINBOW);
        }
        if (e.data == ION_KEY_B) {
            WS2812Driver::getInstance().setAnimation(LEDAnim::NONE);
            AppManager::getInstance().closeCurrentApp();
        }
    }

    void onDestroy() override {
        IonKernel::getInstance().unsubscribeEvent(m_subId);
        if (m_screen) { lv_obj_del(m_screen); m_screen = nullptr; }
    }

private:
    int m_subId = -1;
};

// Entry point called by IonOS when the app launches
extern "C" IonApp* app_create() { return new HelloIonApp(); }
"""
    open(f"{out_dir}/main.cpp", "w").write(src)

    print(f"\n  Example app created in: ./{out_dir}/")
    print(f"  Files: manifest.json, main.cpp, README.md")
    print(f"\n  Next steps:")
    print(f"    1. Edit manifest.json with your app details")
    print(f"    2. Write your app in main.cpp using the IonOS SDK")
    print(f"    3. Compile to app.bin using ESP-IDF")
    print(f"    4. python3 tools/create_ionapp.py --manifest {out_dir}/manifest.json \\")
    print(f"           --code app.bin --output MyApp.ionapp")
    print(f"    5. Copy MyApp.ionapp to /sdcard/apps/ on your device\n")


# ── Inspect an existing .ionapp file ─────────────────────────────────────────
def inspect_package(path: str) -> None:
    with open(path, "rb") as f:
        raw = f.read(HEADER_SIZE)

    if len(raw) < HEADER_SIZE or raw[:8] != MAGIC:
        print(f"ERROR: {path} is not a valid .ionapp package")
        return

    magic       = raw[:8]
    fmt_ver     = raw[8]
    stored_crc  = struct.unpack_from("<I", raw, 12)[0]
    actual_crc  = crc32(raw[12:])
    app_id      = raw[16:80].split(b"\x00")[0].decode("utf-8", errors="replace")
    app_name    = raw[80:104].split(b"\x00")[0].decode("utf-8", errors="replace")
    app_ver     = raw[104:116].split(b"\x00")[0].decode("utf-8", errors="replace")
    author      = raw[116:148].split(b"\x00")[0].decode("utf-8", errors="replace")
    desc        = raw[148:228].split(b"\x00")[0].decode("utf-8", errors="replace")
    accent      = struct.unpack_from(">I", raw, 228)[0] & 0xFFFFFF
    code_size   = struct.unpack_from("<I", raw, 232)[0]
    code_crc    = struct.unpack_from("<I", raw, 236)[0]
    perms       = raw[240]

    crc_ok      = "✓ VALID" if stored_crc == actual_crc else f"✗ CORRUPT (expected 0x{actual_crc:08X})"
    file_size   = os.path.getsize(path)

    perm_names  = [k for k, v in PERM_FLAGS.items() if perms & v]

    print(f"\n  .ionapp Inspector — {path}")
    print(f"  {'─'*50}")
    print(f"  Magic:        {magic}")
    print(f"  Format ver:   {fmt_ver}")
    print(f"  Header CRC:   0x{stored_crc:08X}  {crc_ok}")
    print(f"  App ID:       {app_id}")
    print(f"  Name:         {app_name}")
    print(f"  Version:      {app_ver}")
    print(f"  Author:       {author}")
    print(f"  Description:  {desc}")
    print(f"  Accent color: #{accent:06X}")
    print(f"  Code size:    {code_size} bytes ({code_size/1024:.1f} KB)")
    print(f"  Code CRC:     0x{code_crc:08X}")
    print(f"  Permissions:  {', '.join(perm_names) if perm_names else 'none'}")
    print(f"  File size:    {file_size} bytes ({file_size/1024:.1f} KB)")
    print(f"  {'─'*50}\n")

    # Verify code CRC if file is large enough
    if file_size >= HEADER_SIZE + ICON_BYTES + code_size and code_size > 0:
        with open(path, "rb") as f:
            f.seek(HEADER_SIZE + ICON_BYTES)
            code_data = f.read(code_size)
        actual_code_crc = crc32(code_data)
        if actual_code_crc == code_crc:
            print(f"  Code CRC: ✓ VALID")
        else:
            print(f"  Code CRC: ✗ CORRUPT (expected 0x{code_crc:08X}, got 0x{actual_code_crc:08X})")


# ── CLI ───────────────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        description="IonOS App Packager — create .ionapp packages for SD card installation",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Create a package with custom icon
  python3 tools/create_ionapp.py --manifest app/manifest.json \\
      --code build/app.bin --icon app/icon.png --output MyApp.ionapp

  # Create a package without code (config/data app)
  python3 tools/create_ionapp.py --manifest app/manifest.json

  # Generate an example project skeleton
  python3 tools/create_ionapp.py --example --output my_project

  # Inspect an existing package
  python3 tools/create_ionapp.py --inspect MyApp.ionapp
        """
    )
    parser.add_argument("--manifest", "-m", help="Path to manifest.json")
    parser.add_argument("--code",     "-c", help="Path to compiled app binary (.bin/.elf)")
    parser.add_argument("--icon",     "-i", help="Path to icon image (PNG/JPEG, will be resized to 32×32)")
    parser.add_argument("--output",   "-o", help="Output .ionapp file (or directory for --example)")
    parser.add_argument("--example",        action="store_true", help="Generate example project skeleton")
    parser.add_argument("--inspect",        help="Inspect an existing .ionapp file")
    parser.add_argument("--sign",           action="store_true", help="[Future] Code-sign the package")

    args = parser.parse_args()

    if args.inspect:
        inspect_package(args.inspect)
        return

    if args.example:
        out = args.output or "my_ionapp"
        create_example(out)
        return

    if not args.manifest:
        parser.error("--manifest is required (or use --example / --inspect)")

    if not os.path.exists(args.manifest):
        print(f"ERROR: manifest not found: {args.manifest}")
        sys.exit(1)

    manifest = json.loads(open(args.manifest).read())

    # Default output name from app name
    if not args.output:
        safe = manifest.get("name", "App").replace(" ", "")
        args.output = f"{safe}.ionapp"

    create_package(manifest, args.code, args.icon, args.output)


if __name__ == "__main__":
    main()
