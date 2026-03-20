#!/usr/bin/env python3
"""
IonOS Master Asset Generator
=============================
Generates ALL assets from pure code — no external files needed.
Outputs fully-embedded C/C++ arrays ready to compile into the firmware.

Generated:
  main/resources/generated/ion_icons.h/.c        — 32 custom 32x32 RGB565 icons
  main/resources/generated/ion_sounds.h/.c        — 5 synthesized PCM sounds
  main/resources/generated/ion_boot_frames.h/.c   — 30 animated boot frames
  main/resources/generated/ion_font_7x10.h/.c     — 7x10 bitmap font (ASCII 32-126)
  main/resources/resource_loader.h/.cpp           — Central asset access API
  main/themes/ion_themes.h/.cpp                   — 3 built-in themes
  main/fonts/ion_fonts.h/.cpp                     — Font system
"""

import os, sys, math, struct
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = "/home/claude/IonOS_full"
GEN  = f"{ROOT}/main/resources/generated"
RES  = f"{ROOT}/main/resources"
THM  = f"{ROOT}/main/themes"
FNT  = f"{ROOT}/main/fonts"
os.makedirs(GEN, exist_ok=True)

# ═══════════════════════════════════════════════════════════════════════════════
# PALETTE
# ═══════════════════════════════════════════════════════════════════════════════
BG      = (10,  14,  26)
SURFACE = (19,  25,  41)
ACCENT  = (0,   212, 255)
ACCENT2 = (123, 47,  255)
SUCCESS = (0,   255, 159)
WARNING = (255, 184,  0)
ERROR   = (255,  51, 102)
TEXT    = (238, 242, 255)
DIM     = (136, 153, 187)
WHITE   = (255, 255, 255)
BLACK   = (0,   0,   0)

def rgb565(r,g,b):
    return ((r>>3)<<11)|((g>>2)<<5)|(b>>3)

def to_bytes_swapped(val16):
    """RGB565 byte-swapped for SPI (LV_COLOR_16_SWAP=1)"""
    return struct.pack(">H", val16)

def img_to_c(img: Image.Image):
    img = img.convert("RGB")
    out = bytearray()
    for y in range(img.height):
        for x in range(img.width):
            r,g,b = img.getpixel((x,y))
            out += to_bytes_swapped(rgb565(r,g,b))
    return bytes(out)

def c_array(name, data, per_row=16):
    lines = [f"const uint8_t {name}[] = {{"]
    for i in range(0, len(data), per_row):
        chunk = data[i:i+per_row]
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
    lines.append("};")
    return "\n".join(lines)

def new_icon():
    img = Image.new("RGB", (32,32), BG)
    d   = ImageDraw.Draw(img)
    return img, d

def circle_bg(d, color, r=14):
    d.ellipse([1,1,30,30], fill=color, outline=None)

# ═══════════════════════════════════════════════════════════════════════════════
# ICON DRAWING FUNCTIONS
# Each returns a 32x32 RGB Image
# ═══════════════════════════════════════════════════════════════════════════════

def icon_settings():
    img,d = new_icon()
    circle_bg(d, SURFACE)
    # Gear: outer ring + 8 teeth
    cx,cy,ro,ri = 16,16,11,7
    for a in range(0,360,45):
        rad = math.radians(a)
        x0 = cx + ro*math.cos(rad); y0 = cy + ro*math.sin(rad)
        x1 = cx + (ro+3)*math.cos(rad+math.radians(15)); y1 = cy + (ro+3)*math.sin(rad+math.radians(15))
        x2 = cx + (ro+3)*math.cos(rad-math.radians(15)); y2 = cy + (ro+3)*math.sin(rad-math.radians(15))
        d.polygon([(x0,y0),(x1,y1),(x2,y2)], fill=ACCENT)
    d.ellipse([cx-ro,cy-ro,cx+ro,cy+ro], outline=ACCENT, width=2)
    d.ellipse([cx-ri,cy-ri,cx+ri,cy+ri], fill=BG, outline=ACCENT, width=1)
    return img

def icon_wifi():
    img,d = new_icon()
    circle_bg(d, SURFACE)
    cx,cy = 16,20
    for i,r in enumerate([13,9,5]):
        col = ACCENT if i<3 else DIM
        d.arc([cx-r,cy-r,cx+r,cy+r], 200, 340, fill=col, width=2)
    d.ellipse([cx-2,cy-2,cx+2,cy+2], fill=ACCENT)
    return img

def icon_music():
    img,d = new_icon()
    circle_bg(d, SURFACE)
    # Two eighth notes
    d.line([(12,8),(12,22)], fill=ACCENT, width=2)
    d.line([(19,6),(19,20)], fill=ACCENT, width=2)
    d.line([(12,8),(19,6)],  fill=ACCENT, width=2)
    d.ellipse([8,20,14,26],  fill=ACCENT)
    d.ellipse([15,18,21,24], fill=ACCENT)
    return img

def icon_files():
    img,d = new_icon()
    circle_bg(d, SURFACE)
    d.rectangle([8,10,21,24],  fill=SURFACE, outline=ACCENT, width=2)
    d.rectangle([10,7,18,12],  fill=ACCENT)
    d.line([(11,16),(19,16)],  fill=DIM, width=1)
    d.line([(11,19),(19,19)],  fill=DIM, width=1)
    d.line([(11,22),(17,22)],  fill=DIM, width=1)
    return img

def icon_browser():
    img,d = new_icon()
    circle_bg(d, SURFACE)
    d.ellipse([5,5,27,27], outline=ACCENT, width=2)
    d.line([(16,5),(16,27)],   fill=ACCENT, width=1)
    d.line([(5,16),(27,16)],   fill=ACCENT, width=1)
    d.arc([5,5,27,27],   90, 270, fill=None)
    d.ellipse([10,10,22,22],   outline=DIM, width=1)
    return img

def icon_chatbot():
    img,d = new_icon()
    circle_bg(d, SURFACE)
    d.rounded_rectangle([5,8,27,22], radius=4, fill=ACCENT2, outline=ACCENT, width=1)
    d.polygon([(10,22),(14,26),(14,22)], fill=ACCENT2)
    for x in [10,16,22]:
        d.ellipse([x-2,13,x+2,17], fill=WHITE)
    return img

def icon_emulator():
    img,d = new_icon()
    circle_bg(d, SURFACE)
    d.rounded_rectangle([4,9,28,23], radius=3, fill=SURFACE, outline=ACCENT, width=2)
    d.rectangle([8,14,10,18],  fill=ACCENT)  # left
    d.rectangle([12,12,14,16], fill=ACCENT)  # up
    d.rectangle([12,16,14,20], fill=ACCENT)  # down
    d.ellipse([18,13,21,16],   fill=WARNING)
    d.ellipse([22,13,25,16],   fill=ERROR)
    d.ellipse([18,17,21,20],   fill=SUCCESS)
    d.ellipse([22,17,25,20],   fill=ACCENT)
    return img

def icon_power():
    img,d = new_icon()
    circle_bg(d, SURFACE)
    d.arc([7,7,25,25], 45, 315, fill=ACCENT, width=3)
    d.line([(16,6),(16,16)], fill=ACCENT, width=3)
    return img

def icon_battery():
    img,d = new_icon()
    circle_bg(d, SURFACE)
    d.rectangle([4,11,24,21],  fill=SURFACE, outline=SUCCESS, width=2)
    d.rectangle([24,13,27,19], fill=SUCCESS)
    d.rectangle([6,13,18,19],  fill=SUCCESS)
    return img

def icon_volume():
    img,d = new_icon()
    circle_bg(d, SURFACE)
    d.polygon([(6,13),(6,19),(11,19),(17,24),(17,8),(11,13)], fill=ACCENT)
    for i,r in enumerate([4,7]):
        d.arc([18-r,16-r,18+r,16+r], -45, 45, fill=ACCENT, width=2)
    return img

def icon_play():
    img,d = new_icon()
    circle_bg(d, SURFACE)
    d.polygon([(10,8),(10,24),(24,16)], fill=SUCCESS)
    return img

def icon_pause():
    img,d = new_icon()
    circle_bg(d, SURFACE)
    d.rectangle([9,9,13,23],  fill=WARNING)
    d.rectangle([19,9,23,23], fill=WARNING)
    return img

def icon_next():
    img,d = new_icon()
    circle_bg(d, SURFACE)
    d.polygon([(8,10),(8,22),(16,16)],  fill=ACCENT)
    d.polygon([(16,10),(16,22),(24,16)],fill=ACCENT)
    return img

def icon_prev():
    img,d = new_icon()
    circle_bg(d, SURFACE)
    d.polygon([(24,10),(24,22),(16,16)],fill=ACCENT)
    d.polygon([(16,10),(16,22),(8,16)], fill=ACCENT)
    return img

def icon_folder():
    img,d = new_icon()
    circle_bg(d, SURFACE)
    d.rectangle([4,13,28,24], fill=WARNING)
    d.rectangle([4,10,16,14], fill=WARNING)
    d.rectangle([5,14,27,23], fill=(180,130,0))
    return img

def icon_home():
    img,d = new_icon()
    circle_bg(d, SURFACE)
    d.polygon([(16,6),(4,16),(8,16),(8,26),(24,26),(24,16),(28,16)], fill=ACCENT)
    d.rectangle([12,18,20,26], fill=BG)
    return img

def icon_back():
    img,d = new_icon()
    circle_bg(d, SURFACE)
    d.polygon([(20,8),(10,16),(20,24)], fill=ACCENT)
    d.line([(10,16),(24,16)], fill=ACCENT, width=3)
    return img

def icon_add():
    img,d = new_icon()
    circle_bg(d, SURFACE)
    d.line([(16,6),(16,26)], fill=SUCCESS, width=3)
    d.line([(6,16),(26,16)], fill=SUCCESS, width=3)
    return img

def icon_delete():
    img,d = new_icon()
    circle_bg(d, SURFACE)
    d.line([(8,8),(24,24)],  fill=ERROR, width=3)
    d.line([(24,8),(8,24)],  fill=ERROR, width=3)
    return img

def icon_search():
    img,d = new_icon()
    circle_bg(d, SURFACE)
    d.ellipse([7,7,21,21],  outline=ACCENT, width=2)
    d.line([(19,19),(27,27)], fill=ACCENT, width=3)
    return img

def icon_lock():
    img,d = new_icon()
    circle_bg(d, SURFACE)
    d.arc([10,7,22,17], 180, 0, fill=WARNING, width=2)
    d.rectangle([8,15,24,26], fill=WARNING, outline=WARNING)
    d.ellipse([14,18,18,22], fill=BG)
    return img

def icon_star():
    img,d = new_icon()
    circle_bg(d, SURFACE)
    pts = []
    for i in range(10):
        a   = math.radians(i*36 - 90)
        r   = 11 if i%2==0 else 5
        pts.append((16+r*math.cos(a), 16+r*math.sin(a)))
    d.polygon(pts, fill=WARNING)
    return img

def icon_cloud():
    img,d = new_icon()
    circle_bg(d, SURFACE)
    d.ellipse([6,12,18,22],  fill=ACCENT)
    d.ellipse([12,9,24,21],  fill=ACCENT)
    d.ellipse([16,13,26,23], fill=ACCENT)
    d.rectangle([6,17,26,23], fill=ACCENT)
    return img

def icon_radio():
    img,d = new_icon()
    circle_bg(d, SURFACE)
    d.ellipse([10,10,22,22], outline=ACCENT, width=2)
    d.ellipse([14,14,18,18], fill=ACCENT)
    d.line([(16,5),(22,11)], fill=ACCENT, width=2)
    return img

def icon_controller():
    img,d = new_icon()
    circle_bg(d, SURFACE)
    d.rounded_rectangle([4,11,28,23], radius=5, fill=ACCENT2, outline=ACCENT, width=1)
    d.line([(10,14),(10,20)], fill=WHITE, width=2)
    d.line([(7,17),(13,17)],  fill=WHITE, width=2)
    d.ellipse([19,13,22,16], fill=WARNING)
    d.ellipse([23,13,26,16], fill=ERROR)
    d.ellipse([19,17,22,20], fill=SUCCESS)
    d.ellipse([23,17,26,20], fill=ACCENT)
    return img

def icon_heart():
    img,d = new_icon()
    circle_bg(d, SURFACE)
    d.ellipse([6,9,16,18],   fill=ERROR)
    d.ellipse([16,9,26,18],  fill=ERROR)
    d.polygon([(6,14),(16,26),(26,14)], fill=ERROR)
    return img

def icon_map():
    img,d = new_icon()
    circle_bg(d, SURFACE)
    d.polygon([(4,8),(4,24),(12,20),(20,24),(28,8),(20,12),(12,8)], outline=ACCENT, fill=SURFACE, width=1)
    d.line([(12,8),(12,20)],  fill=ACCENT, width=1)
    d.line([(20,12),(20,24)], fill=ACCENT, width=1)
    d.ellipse([14,11,18,15], fill=WARNING)
    return img

def icon_download():
    img,d = new_icon()
    circle_bg(d, SURFACE)
    d.line([(16,6),(16,21)],         fill=ACCENT, width=3)
    d.polygon([(9,16),(16,23),(23,16)], fill=ACCENT)
    d.line([(8,25),(24,25)],          fill=ACCENT, width=2)
    return img

def icon_info():
    img,d = new_icon()
    circle_bg(d, SURFACE)
    d.ellipse([5,5,27,27], outline=ACCENT, width=2)
    d.ellipse([14,9,18,13], fill=ACCENT)
    d.line([(16,15),(16,23)], fill=ACCENT, width=3)
    return img

def icon_check():
    img,d = new_icon()
    circle_bg(d, SURFACE)
    d.ellipse([5,5,27,27], fill=SUCCESS)
    d.line([(9,16),(14,22),(23,10)], fill=WHITE, width=3)
    return img

def icon_warning():
    img,d = new_icon()
    circle_bg(d, SURFACE)
    d.polygon([(16,5),(3,27),(29,27)], fill=WARNING)
    d.line([(16,13),(16,21)], fill=BG, width=2)
    d.ellipse([14,23,18,27],  fill=BG)
    return img

def icon_clock():
    img,d = new_icon()
    circle_bg(d, SURFACE)
    d.ellipse([5,5,27,27], outline=ACCENT, width=2)
    d.line([(16,16),(16,9)],  fill=ACCENT, width=2)
    d.line([(16,16),(22,16)], fill=WARNING, width=2)
    d.ellipse([14,14,18,18],  fill=ACCENT)
    return img

def icon_edit():
    img,d = new_icon()
    circle_bg(d, SURFACE)
    d.polygon([(8,22),(12,26),(26,12),(22,8)], outline=ACCENT, fill=SURFACE, width=2)
    d.polygon([(8,22),(12,26),(9,27)], fill=ACCENT)
    d.line([(22,8),(26,12)], fill=ACCENT, width=2)
    return img

# ─── Icon registry ────────────────────────────────────────────────────────────
ICONS = [
    ("settings",    icon_settings),
    ("wifi",        icon_wifi),
    ("music",       icon_music),
    ("files",       icon_files),
    ("browser",     icon_browser),
    ("chatbot",     icon_chatbot),
    ("emulator",    icon_emulator),
    ("power",       icon_power),
    ("battery",     icon_battery),
    ("volume",      icon_volume),
    ("play",        icon_play),
    ("pause",       icon_pause),
    ("next",        icon_next),
    ("prev",        icon_prev),
    ("folder",      icon_folder),
    ("home",        icon_home),
    ("back",        icon_back),
    ("add",         icon_add),
    ("delete",      icon_delete),
    ("search",      icon_search),
    ("lock",        icon_lock),
    ("star",        icon_star),
    ("cloud",       icon_cloud),
    ("radio",       icon_radio),
    ("controller",  icon_controller),
    ("heart",       icon_heart),
    ("map",         icon_map),
    ("download",    icon_download),
    ("info",        icon_info),
    ("check",       icon_check),
    ("warning_ico", icon_warning),
    ("clock",       icon_clock),
    ("edit",        icon_edit),
]

# ═══════════════════════════════════════════════════════════════════════════════
# BOOT ANIMATION FRAMES — IonOS logo materializing
# ═══════════════════════════════════════════════════════════════════════════════

def make_boot_frame(frame_idx, total=30, w=120, h=80):
    t   = frame_idx / max(total-1, 1)  # 0.0 → 1.0
    img = Image.new("RGB", (w, h), BG)
    d   = ImageDraw.Draw(img)
    cx, cy = w//2, h//2

    # Phase 1 (0.0–0.3): expanding rings
    if t < 0.3:
        p     = t / 0.3
        alpha = int(p * 255)
        for ring in range(3):
            r   = int((ring+1) * 15 * p)
            col = tuple(int(c * p * 0.6) for c in ACCENT)
            if r > 0:
                d.ellipse([cx-r,cy-r,cx+r,cy+r], outline=col, width=1)

    # Phase 2 (0.2–0.7): "ION" text assembles from particles
    if t > 0.2:
        p    = min(1.0, (t-0.2)/0.5)
        fade = int(p*255)
        col  = tuple(int(c*p) for c in ACCENT)
        # Draw "ION" as simple pixel blocks
        def draw_letter_I(ox,oy,col):
            d.rectangle([ox,oy,ox+8,oy+2],   fill=col)
            d.rectangle([ox+3,oy+2,ox+5,oy+14], fill=col)
            d.rectangle([ox,oy+14,ox+8,oy+16],  fill=col)
        def draw_letter_O(ox,oy,col):
            d.rectangle([ox,oy,ox+10,oy+2],   fill=col)
            d.rectangle([ox,oy+14,ox+10,oy+16],fill=col)
            d.rectangle([ox,oy,ox+2,oy+16],    fill=col)
            d.rectangle([ox+8,oy,ox+10,oy+16], fill=col)
        def draw_letter_N(ox,oy,col):
            d.rectangle([ox,oy,ox+2,oy+16],    fill=col)
            d.rectangle([ox+8,oy,ox+10,oy+16], fill=col)
            d.polygon([(ox,oy),(ox+2,oy),(ox+10,oy+16),(ox+8,oy+16)], fill=col)
        lx = cx-17; ly = cy-12
        draw_letter_I(lx,    ly, col)
        draw_letter_O(lx+14, ly, col)
        draw_letter_N(lx+28, ly, col)

    # Phase 3 (0.5–1.0): subtitle "OS" + glow ring
    if t > 0.5:
        p   = min(1.0, (t-0.5)/0.5)
        col = tuple(int(c*p) for c in ACCENT2)
        def draw_letter_O2(ox,oy,col):
            d.rectangle([ox,oy,ox+8,oy+2],   fill=col)
            d.rectangle([ox,oy+10,ox+8,oy+12],fill=col)
            d.rectangle([ox,oy,ox+2,oy+12],   fill=col)
            d.rectangle([ox+6,oy,ox+8,oy+12], fill=col)
        def draw_letter_S(ox,oy,col):
            d.rectangle([ox,oy,ox+8,oy+2],    fill=col)
            d.rectangle([ox,oy,ox+2,oy+6],    fill=col)
            d.rectangle([ox,oy+5,ox+8,oy+7],  fill=col)
            d.rectangle([ox+6,oy+6,ox+8,oy+12],fill=col)
            d.rectangle([ox,oy+10,ox+8,oy+12], fill=col)
        sx = cx - 9; sy = cy + 4
        draw_letter_O2(sx,    sy, col)
        draw_letter_S( sx+12, sy, col)
        # Glow ring
        r = int(30 + p*5)
        col2 = tuple(int(c*p*0.4) for c in ACCENT)
        d.ellipse([cx-r,cy-r,cx+r,cy+r], outline=col2, width=1)

    # Phase 4 (0.85–1.0): progress bar
    if t > 0.85:
        p   = (t-0.85)/0.15
        bw  = 80; bh=4; bx=cx-bw//2; by=cy+28
        d.rectangle([bx,by,bx+bw,by+bh], outline=DIM, width=1)
        d.rectangle([bx+1,by+1,bx+1+int((bw-2)*p),by+bh-1], fill=ACCENT)

    return img

# ═══════════════════════════════════════════════════════════════════════════════
# SOUND SYNTHESIS
# ═══════════════════════════════════════════════════════════════════════════════

SR = 44100  # Sample rate

def sine(freq, dur_ms, amp=20000):
    n = int(SR * dur_ms / 1000)
    t = np.linspace(0, dur_ms/1000, n, False)
    s = (np.sin(2*np.pi*freq*t) * amp).astype(np.int16)
    return s

def fade(s, ms_in=10, ms_out=20):
    in_n  = int(SR*ms_in/1000)
    out_n = int(SR*ms_out/1000)
    s = s.astype(np.float32)
    if in_n:  s[:in_n]   *= np.linspace(0,1,in_n,  dtype=np.float32)
    if out_n: s[-out_n:]  *= np.linspace(1,0,out_n, dtype=np.float32)
    return np.clip(s,-32767,32767).astype(np.int16)

def to_stereo(s):
    return np.column_stack([s, s]).flatten()

def sound_click():
    # Short 880Hz tap, 40ms
    s = fade(sine(880, 40, 12000), 2, 15)
    return to_stereo(s)

def sound_notification():
    # Two-tone ascending: C5(523Hz)→E5(659Hz), 80ms each
    s1 = fade(sine(523, 80), 5, 20)
    s2 = fade(sine(659, 80), 5, 30)
    s  = np.concatenate([s1, np.zeros(int(SR*0.02), np.int16), s2])
    return to_stereo(s)

def sound_error():
    # Descending buzz: 300Hz→200Hz, harsh, 150ms
    n = int(SR*0.15)
    t = np.linspace(0,0.15,n)
    f = np.linspace(300, 180, n)
    s = (np.sin(2*np.pi*np.cumsum(f)/SR) * 18000).astype(np.int16)
    s = fade(s, 5, 30)
    return to_stereo(s)

def sound_boot():
    # IonOS startup jingle: 4 ascending notes with harmony
    notes = [(392,120),(494,100),(587,100),(784,200)]
    parts = []
    for freq, ms in notes:
        s  = fade(sine(freq, ms, 18000), 10, 40)
        s2 = fade(sine(freq*1.5, ms, 8000), 10, 40)  # Harmony
        s  = np.clip(s.astype(np.int32) + s2.astype(np.int32), -32767, 32767).astype(np.int16)
        parts.append(s)
        parts.append(np.zeros(int(SR*0.03), np.int16))
    return to_stereo(np.concatenate(parts))

def sound_success():
    # Short upward two-tone: G4→C5, crisp
    s1 = fade(sine(392, 60, 14000), 5, 15)
    s2 = fade(sine(523, 80, 16000), 5, 25)
    s  = np.concatenate([s1, np.zeros(int(SR*0.01), np.int16), s2])
    return to_stereo(s)

SOUNDS = [
    ("click",        sound_click),
    ("notification", sound_notification),
    ("error",        sound_error),
    ("boot",         sound_boot),
    ("success",      sound_success),
]

# ═══════════════════════════════════════════════════════════════════════════════
# 7×10 BITMAP FONT — ASCII 32-126
# ═══════════════════════════════════════════════════════════════════════════════

# Each char: 7 columns × 10 rows, packed as 10 bytes (1 per row, bit 6=leftmost)
# Minimal monospace font hand-crafted for embedded displays

FONT_7x10 = {
 ' ':  [0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00],
 '!':  [0x08,0x08,0x08,0x08,0x08,0x00,0x08,0x00,0x00,0x00],
 '"':  [0x14,0x14,0x14,0x00,0x00,0x00,0x00,0x00,0x00,0x00],
 '#':  [0x14,0x14,0x3E,0x14,0x3E,0x14,0x14,0x00,0x00,0x00],
 '$':  [0x08,0x3C,0x0A,0x1C,0x28,0x1E,0x08,0x00,0x00,0x00],
 '%':  [0x22,0x12,0x10,0x08,0x04,0x22,0x11,0x00,0x00,0x00],  # simplified
 '&':  [0x0C,0x12,0x0A,0x04,0x2A,0x12,0x2C,0x00,0x00,0x00],
 "'":  [0x08,0x08,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00],
 '(':  [0x10,0x08,0x04,0x04,0x04,0x08,0x10,0x00,0x00,0x00],
 ')':  [0x04,0x08,0x10,0x10,0x10,0x08,0x04,0x00,0x00,0x00],
 '*':  [0x00,0x08,0x2A,0x1C,0x2A,0x08,0x00,0x00,0x00,0x00],
 '+':  [0x00,0x08,0x08,0x3E,0x08,0x08,0x00,0x00,0x00,0x00],
 ',':  [0x00,0x00,0x00,0x00,0x0C,0x08,0x04,0x00,0x00,0x00],
 '-':  [0x00,0x00,0x00,0x3E,0x00,0x00,0x00,0x00,0x00,0x00],
 '.':  [0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00,0x00,0x00],
 '/':  [0x20,0x10,0x10,0x08,0x04,0x04,0x02,0x00,0x00,0x00],
 '0':  [0x1C,0x22,0x32,0x2A,0x26,0x22,0x1C,0x00,0x00,0x00],
 '1':  [0x08,0x0C,0x08,0x08,0x08,0x08,0x1C,0x00,0x00,0x00],
 '2':  [0x1C,0x22,0x20,0x18,0x04,0x02,0x3E,0x00,0x00,0x00],
 '3':  [0x3E,0x20,0x10,0x18,0x20,0x22,0x1C,0x00,0x00,0x00],
 '4':  [0x10,0x18,0x14,0x12,0x3E,0x10,0x10,0x00,0x00,0x00],
 '5':  [0x3E,0x02,0x1E,0x20,0x20,0x22,0x1C,0x00,0x00,0x00],
 '6':  [0x18,0x04,0x02,0x1E,0x22,0x22,0x1C,0x00,0x00,0x00],
 '7':  [0x3E,0x20,0x10,0x08,0x04,0x04,0x04,0x00,0x00,0x00],
 '8':  [0x1C,0x22,0x22,0x1C,0x22,0x22,0x1C,0x00,0x00,0x00],
 '9':  [0x1C,0x22,0x22,0x3C,0x20,0x10,0x0C,0x00,0x00,0x00],
 ':':  [0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00,0x00,0x00,0x00],
 ';':  [0x00,0x0C,0x0C,0x00,0x0C,0x08,0x04,0x00,0x00,0x00],
 '<':  [0x10,0x08,0x04,0x02,0x04,0x08,0x10,0x00,0x00,0x00],
 '=':  [0x00,0x00,0x3E,0x00,0x3E,0x00,0x00,0x00,0x00,0x00],
 '>':  [0x04,0x08,0x10,0x20,0x10,0x08,0x04,0x00,0x00,0x00],
 '?':  [0x1C,0x22,0x20,0x10,0x08,0x00,0x08,0x00,0x00,0x00],
 '@':  [0x1C,0x22,0x2A,0x3A,0x1A,0x02,0x3C,0x00,0x00,0x00],
 'A':  [0x08,0x14,0x22,0x3E,0x22,0x22,0x22,0x00,0x00,0x00],
 'B':  [0x1E,0x22,0x22,0x1E,0x22,0x22,0x1E,0x00,0x00,0x00],
 'C':  [0x1C,0x22,0x02,0x02,0x02,0x22,0x1C,0x00,0x00,0x00],
 'D':  [0x1E,0x22,0x22,0x22,0x22,0x22,0x1E,0x00,0x00,0x00],
 'E':  [0x3E,0x02,0x02,0x1E,0x02,0x02,0x3E,0x00,0x00,0x00],
 'F':  [0x3E,0x02,0x02,0x1E,0x02,0x02,0x02,0x00,0x00,0x00],
 'G':  [0x1C,0x22,0x02,0x3A,0x22,0x22,0x1C,0x00,0x00,0x00],
 'H':  [0x22,0x22,0x22,0x3E,0x22,0x22,0x22,0x00,0x00,0x00],
 'I':  [0x1C,0x08,0x08,0x08,0x08,0x08,0x1C,0x00,0x00,0x00],
 'J':  [0x38,0x10,0x10,0x10,0x12,0x12,0x0C,0x00,0x00,0x00],
 'K':  [0x22,0x12,0x0A,0x06,0x0A,0x12,0x22,0x00,0x00,0x00],
 'L':  [0x02,0x02,0x02,0x02,0x02,0x02,0x3E,0x00,0x00,0x00],
 'M':  [0x22,0x36,0x2A,0x2A,0x22,0x22,0x22,0x00,0x00,0x00],
 'N':  [0x22,0x26,0x2A,0x32,0x22,0x22,0x22,0x00,0x00,0x00],
 'O':  [0x1C,0x22,0x22,0x22,0x22,0x22,0x1C,0x00,0x00,0x00],
 'P':  [0x1E,0x22,0x22,0x1E,0x02,0x02,0x02,0x00,0x00,0x00],
 'Q':  [0x1C,0x22,0x22,0x22,0x2A,0x12,0x2C,0x00,0x00,0x00],
 'R':  [0x1E,0x22,0x22,0x1E,0x0A,0x12,0x22,0x00,0x00,0x00],
 'S':  [0x1C,0x22,0x02,0x1C,0x20,0x22,0x1C,0x00,0x00,0x00],
 'T':  [0x3E,0x08,0x08,0x08,0x08,0x08,0x08,0x00,0x00,0x00],
 'U':  [0x22,0x22,0x22,0x22,0x22,0x22,0x1C,0x00,0x00,0x00],
 'V':  [0x22,0x22,0x22,0x22,0x22,0x14,0x08,0x00,0x00,0x00],
 'W':  [0x22,0x22,0x22,0x2A,0x2A,0x36,0x22,0x00,0x00,0x00],
 'X':  [0x22,0x22,0x14,0x08,0x14,0x22,0x22,0x00,0x00,0x00],
 'Y':  [0x22,0x22,0x14,0x08,0x08,0x08,0x08,0x00,0x00,0x00],
 'Z':  [0x3E,0x20,0x10,0x08,0x04,0x02,0x3E,0x00,0x00,0x00],
 '[':  [0x1C,0x04,0x04,0x04,0x04,0x04,0x1C,0x00,0x00,0x00],
 '\\': [0x02,0x04,0x04,0x08,0x10,0x10,0x20,0x00,0x00,0x00],
 ']':  [0x1C,0x10,0x10,0x10,0x10,0x10,0x1C,0x00,0x00,0x00],
 '^':  [0x08,0x14,0x22,0x00,0x00,0x00,0x00,0x00,0x00,0x00],
 '_':  [0x00,0x00,0x00,0x00,0x00,0x00,0x3E,0x00,0x00,0x00],
 '`':  [0x04,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00],
 'a':  [0x00,0x00,0x1C,0x20,0x3C,0x22,0x3C,0x00,0x00,0x00],
 'b':  [0x02,0x02,0x1E,0x22,0x22,0x22,0x1E,0x00,0x00,0x00],
 'c':  [0x00,0x00,0x1C,0x02,0x02,0x22,0x1C,0x00,0x00,0x00],
 'd':  [0x20,0x20,0x3C,0x22,0x22,0x22,0x3C,0x00,0x00,0x00],
 'e':  [0x00,0x00,0x1C,0x22,0x3E,0x02,0x1C,0x00,0x00,0x00],
 'f':  [0x18,0x04,0x04,0x0E,0x04,0x04,0x04,0x00,0x00,0x00],
 'g':  [0x00,0x3C,0x22,0x22,0x3C,0x20,0x1C,0x00,0x00,0x00],
 'h':  [0x02,0x02,0x1A,0x26,0x22,0x22,0x22,0x00,0x00,0x00],
 'i':  [0x08,0x00,0x0C,0x08,0x08,0x08,0x1C,0x00,0x00,0x00],
 'j':  [0x10,0x00,0x18,0x10,0x10,0x12,0x0C,0x00,0x00,0x00],
 'k':  [0x02,0x02,0x12,0x0A,0x06,0x0A,0x12,0x00,0x00,0x00],
 'l':  [0x0C,0x08,0x08,0x08,0x08,0x08,0x1C,0x00,0x00,0x00],
 'm':  [0x00,0x00,0x36,0x2A,0x2A,0x22,0x22,0x00,0x00,0x00],
 'n':  [0x00,0x00,0x1A,0x26,0x22,0x22,0x22,0x00,0x00,0x00],
 'o':  [0x00,0x00,0x1C,0x22,0x22,0x22,0x1C,0x00,0x00,0x00],
 'p':  [0x00,0x1E,0x22,0x22,0x1E,0x02,0x02,0x00,0x00,0x00],
 'q':  [0x00,0x3C,0x22,0x22,0x3C,0x20,0x20,0x00,0x00,0x00],
 'r':  [0x00,0x00,0x1A,0x06,0x02,0x02,0x02,0x00,0x00,0x00],
 's':  [0x00,0x00,0x1C,0x02,0x1C,0x20,0x1E,0x00,0x00,0x00],
 't':  [0x04,0x04,0x0E,0x04,0x04,0x24,0x18,0x00,0x00,0x00],
 'u':  [0x00,0x00,0x22,0x22,0x22,0x22,0x1C,0x00,0x00,0x00],
 'v':  [0x00,0x00,0x22,0x22,0x22,0x14,0x08,0x00,0x00,0x00],
 'w':  [0x00,0x00,0x22,0x22,0x2A,0x2A,0x14,0x00,0x00,0x00],
 'x':  [0x00,0x00,0x22,0x14,0x08,0x14,0x22,0x00,0x00,0x00],
 'y':  [0x00,0x22,0x22,0x3C,0x20,0x22,0x1C,0x00,0x00,0x00],
 'z':  [0x00,0x00,0x3E,0x10,0x08,0x04,0x3E,0x00,0x00,0x00],
 '{':  [0x18,0x04,0x04,0x02,0x04,0x04,0x18,0x00,0x00,0x00],
 '|':  [0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x00,0x00,0x00],
 '}':  [0x06,0x08,0x08,0x10,0x08,0x08,0x06,0x00,0x00,0x00],
 '~':  [0x00,0x00,0x24,0x1A,0x00,0x00,0x00,0x00,0x00,0x00],
}

# ═══════════════════════════════════════════════════════════════════════════════
# WRITE C FILES
# ═══════════════════════════════════════════════════════════════════════════════

def write_icons():
    print(f"Generating {len(ICONS)} icons...")
    h = ["// IonOS Icons — Generated by tools/gen_assets.py", "// DO NOT EDIT",
         "#pragma once", '#include "lvgl/lvgl.h"', "",
         f"#define ION_ICON_COUNT {len(ICONS)}", "#define ION_ICON_W 32", "#define ION_ICON_H 32", "",
         "typedef enum {"]
    for i,(name,_) in enumerate(ICONS):
        h.append(f"    ION_ICON_{name.upper()} = {i},")
    h += ["    ION_ICON__MAX", "} ion_icon_id_t;", "",
          "extern const lv_img_dsc_t* const ion_icons[ION_ICON_COUNT];",
          "static inline const lv_img_dsc_t* ion_get_icon(ion_icon_id_t id) {",
          "    return (id < ION_ICON__MAX) ? ion_icons[id] : ion_icons[0];", "}", ""]

    c = ["// IonOS Icon Data — Generated", '#include "ion_icons.h"', ""]
    for i,(name,fn) in enumerate(ICONS):
        img  = fn()
        data = img_to_c(img)
        c.append(c_array(f"_icon_{name}_px", data))
        c.append(f"static const lv_img_dsc_t _icon_{name} = {{")
        c.append(f"    .header={{.always_zero=0,.w=32,.h=32,.cf=LV_IMG_CF_TRUE_COLOR}},")
        c.append(f"    .data_size={len(data)}, .data=_icon_{name}_px")
        c.append("};")
        c.append("")
    c.append("const lv_img_dsc_t* const ion_icons[ION_ICON_COUNT] = {")
    for name,_ in ICONS:
        c.append(f"    &_icon_{name},")
    c.append("};")

    open(f"{GEN}/ion_icons.h","w").write("\n".join(h))
    open(f"{GEN}/ion_icons.c","w").write("\n".join(c))
    print(f"  ✓ ion_icons.h/c  ({len(ICONS)} × 32×32 RGB565)")

def write_sounds():
    print("Generating sounds...")
    h = ["// IonOS Sounds — Generated", "#pragma once", "#include <stdint.h>", "#include <stddef.h>", "",
         "typedef struct { const int16_t* data; size_t len; uint32_t sr; } ion_sound_t;", ""]
    c = ["// IonOS Sound Data — Generated", '#include "ion_sounds.h"', ""]
    for name, fn in SOUNDS:
        s    = fn().astype(np.int16)
        raw  = s.tobytes()
        c.append(c_array(f"_snd_{name}_raw", raw))
        c.append(f"const ion_sound_t ion_sound_{name} = {{")
        c.append(f"    .data=(const int16_t*)_snd_{name}_raw, .len={len(s)}, .sr={SR}")
        c.append("};")
        c.append("")
        h.append(f"extern const ion_sound_t ion_sound_{name};")
        print(f"  ✓ {name}: {len(raw)//1024}KB")
    open(f"{GEN}/ion_sounds.h","w").write("\n".join(h))
    open(f"{GEN}/ion_sounds.c","w").write("\n".join(c))

def write_boot_frames():
    n = 30
    print(f"Generating {n} boot animation frames...")
    h = ["// IonOS Boot Frames — Generated", "#pragma once", '#include "lvgl/lvgl.h"', "",
         f"#define ION_BOOT_FRAME_COUNT {n}", "#define ION_BOOT_FRAME_W 120", "#define ION_BOOT_FRAME_H 80",
         "#define ION_BOOT_FRAME_MS    50", "",
         f"extern const lv_img_dsc_t* const ion_boot_frames[ION_BOOT_FRAME_COUNT];"]
    c = ["// IonOS Boot Frame Data — Generated", '#include "ion_boot_frames.h"', ""]
    for i in range(n):
        img  = make_boot_frame(i, n)
        data = img_to_c(img)
        c.append(c_array(f"_bf_{i:02d}", data))
        c.append(f"static const lv_img_dsc_t _bfd_{i:02d}={{")
        c.append(f"    .header={{.always_zero=0,.w=120,.h=80,.cf=LV_IMG_CF_TRUE_COLOR}},")
        c.append(f"    .data_size={len(data)},.data=_bf_{i:02d}")
        c.append("};")
        c.append("")
    c.append("const lv_img_dsc_t* const ion_boot_frames[ION_BOOT_FRAME_COUNT] = {")
    for i in range(n):
        c.append(f"    &_bfd_{i:02d},")
    c.append("};")
    open(f"{GEN}/ion_boot_frames.h","w").write("\n".join(h))
    open(f"{GEN}/ion_boot_frames.c","w").write("\n".join(c))
    print(f"  ✓ ion_boot_frames.h/c  (30 × 120×80 RGB565)")

def write_font():
    print("Generating 7×10 bitmap font...")
    chars = sorted(FONT_7x10.keys())
    h = ["// IonOS 7×10 Bitmap Font — Generated", "#pragma once", "#include <stdint.h>", "",
         "#define ION_FONT_7x10_W 7", "#define ION_FONT_7x10_H 10",
         "#define ION_FONT_FIRST_CHAR 32", f"#define ION_FONT_CHAR_COUNT {len(chars)}", "",
         "// font_data[char - 32][row] — 10 bytes per glyph",
         "extern const uint8_t ion_font_7x10[ION_FONT_CHAR_COUNT][10];",
         "// Get glyph pointer for char c (returns space if not found)",
         "static inline const uint8_t* ion_font_glyph(char c) {",
         "    int idx = (int)c - ION_FONT_FIRST_CHAR;",
         "    if (idx < 0 || idx >= ION_FONT_CHAR_COUNT) idx = 0;",
         "    return ion_font_7x10[idx];",
         "}"]
    rows = []
    for ch in range(32, 127):
        c = chr(ch)
        data = FONT_7x10.get(c, [0]*10)
        rows.append("    {" + ",".join(f"0x{b:02X}" for b in data) + f"}},  // '{c if c != chr(92) else chr(92)+chr(92)}'")
    src = ["// IonOS Font Data — Generated", '#include "ion_font_7x10.h"', "",
           "const uint8_t ion_font_7x10[ION_FONT_CHAR_COUNT][10] = {"] + rows + ["};"]
    open(f"{GEN}/ion_font_7x10.h","w").write("\n".join(h))
    open(f"{GEN}/ion_font_7x10.c","w").write("\n".join(src))
    print(f"  ✓ ion_font_7x10.h/c  ({len(chars)} glyphs)")

# ─── Run ─────────────────────────────────────────────────────────────────────
print("=" * 60)
print("IonOS Asset Generator")
print("=" * 60)
write_icons()
write_sounds()
write_boot_frames()
write_font()
print("=" * 60)
total = sum(os.path.getsize(f"{GEN}/{f}") for f in os.listdir(GEN) if os.path.isfile(f"{GEN}/{f}"))
print(f"All assets generated → {GEN}")
print(f"Total size: {total//1024} KB")
