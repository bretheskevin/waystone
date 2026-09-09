#!/usr/bin/env python3
"""
Generate User-Switch-Icons.ttf — a minimal preview font mapping NintendoExt
Private-Use-Area codepoints to recognizable Nintendo Switch button glyphs.

Codepoints (authoritative source: WerWolv/libtesla, confirmed against the
NintendoExt PUA range 0xE0A0–0xE0BA from jenergy/jzIntvImGui ImGui port):
  U+E0A0  A face button   (circle ring)
  U+E0A1  B face button   (circle ring)
  U+E0A2  X face button   (circle ring)
  U+E0A3  Y face button   (circle ring)
  U+E0A4  L shoulder      (landscape rectangle)
  U+E0A5  R shoulder      (landscape rectangle)
  U+E0A6  ZL trigger      (wider landscape rectangle)
  U+E0A7  ZR trigger      (wider landscape rectangle)
  U+E0B5  Plus (+)        (cross shape)
  U+E0B6  Minus (-)       (horizontal bar)

License: CC0 1.0 Universal — no rights reserved.
Usage:
  python3 gen_switch_icons.py          # generates User-Switch-Icons.ttf next to this script
  python3 gen_switch_icons.py <path>   # generates at <path>
"""

import math
import sys
import os

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

# ---------------------------------------------------------------------------
# Font metrics
# ---------------------------------------------------------------------------
UPM = 1000
ASCENDER = 800
DESCENDER = -200
LINE_GAP = 0
ADV = 900    # advance width
LSB = 50     # left side bearing

# Logical center of glyph drawing area
CX = LSB + 400          # 450
CY = (ASCENDER + DESCENDER) // 2   # 300

# ---------------------------------------------------------------------------
# Geometry helpers (all coordinates: x right, y up — standard font coords)
# ---------------------------------------------------------------------------

def polygon_circle(cx, cy, r, n=24, clockwise=True):
    """
    (x,y) integer points for an n-gon approximating a circle.
    clockwise=True  → CW winding in font coords (y up) → filled outer contour.
    clockwise=False → CCW winding                      → inner hole.
    """
    pts = []
    for i in range(n):
        if clockwise:
            angle = math.pi / 2 - (2 * math.pi / n) * i
        else:
            angle = math.pi / 2 + (2 * math.pi / n) * i
        pts.append((round(cx + r * math.cos(angle)),
                    round(cy + r * math.sin(angle))))
    return pts


def trace_polygon(pen, pts):
    pen.moveTo(pts[0])
    for pt in pts[1:]:
        pen.lineTo(pt)
    pen.endPath()


def ring(pen, cx, cy, r_outer, r_inner, n=24):
    """Filled ring: outer CW circle + inner CCW circle."""
    trace_polygon(pen, polygon_circle(cx, cy, r_outer, n, clockwise=True))
    trace_polygon(pen, polygon_circle(cx, cy, r_inner, n, clockwise=False))


def rounded_rect(pen, x0, y0, x1, y1, r):
    """Filled rounded-corner rectangle (chamfered octagon). CW winding."""
    r = min(r, (x1 - x0) // 3, (y1 - y0) // 3)
    pts = [
        (x0 + r, y1),    # top-left → top-right
        (x1 - r, y1),
        (x1,     y1 - r),
        (x1,     y0 + r),
        (x1 - r, y0),
        (x0 + r, y0),
        (x0,     y0 + r),
        (x0,     y1 - r),
    ]
    trace_polygon(pen, pts)


def plus_sign(pen, cx, cy, arm, w):
    """Filled + shape. CW winding."""
    h = w // 2
    pts = [
        (cx - h, cy + arm), (cx + h, cy + arm),
        (cx + h, cy + h),   (cx + arm, cy + h),
        (cx + arm, cy - h), (cx + h, cy - h),
        (cx + h, cy - arm), (cx - h, cy - arm),
        (cx - h, cy - h),   (cx - arm, cy - h),
        (cx - arm, cy + h), (cx - h, cy + h),
    ]
    trace_polygon(pen, pts)


def minus_bar(pen, cx, cy, arm, w):
    """Filled horizontal bar. CW winding."""
    h = w // 2
    pts = [
        (cx - arm, cy + h), (cx + arm, cy + h),
        (cx + arm, cy - h), (cx - arm, cy - h),
    ]
    trace_polygon(pen, pts)


# ---------------------------------------------------------------------------
# Per-glyph draw functions  (pen is a TTGlyphPen)
# ---------------------------------------------------------------------------

def draw_face_button(pen):
    ring(pen, CX, CY, r_outer=320, r_inner=175, n=24)


def draw_shoulder(pen):
    rounded_rect(pen, LSB + 55, CY - 145, LSB + 745, CY + 145, r=90)


def draw_trigger(pen):
    rounded_rect(pen, LSB + 25, CY - 185, LSB + 775, CY + 185, r=75)


def draw_plus(pen):
    plus_sign(pen, CX, CY, arm=325, w=165)


def draw_minus(pen):
    minus_bar(pen, CX, CY, arm=305, w=165)


# ---------------------------------------------------------------------------
# Glyph table: (codepoint, glyph_name, draw_fn)
# ---------------------------------------------------------------------------

GLYPH_TABLE = [
    (0xE0A0, "uniE0A0",  draw_face_button),   # A button
    (0xE0A1, "uniE0A1",  draw_face_button),   # B button
    (0xE0A2, "uniE0A2",  draw_face_button),   # X button
    (0xE0A3, "uniE0A3",  draw_face_button),   # Y button
    (0xE0A4, "uniE0A4",  draw_shoulder),      # L shoulder
    (0xE0A5, "uniE0A5",  draw_shoulder),      # R shoulder
    (0xE0A6, "uniE0A6",  draw_trigger),       # ZL trigger
    (0xE0A7, "uniE0A7",  draw_trigger),       # ZR trigger
    (0xE0B5, "uniE0B5",  draw_plus),          # Plus (+)
    (0xE0B6, "uniE0B6",  draw_minus),         # Minus (-)
]


# ---------------------------------------------------------------------------
# Build the font
# ---------------------------------------------------------------------------

def build_font(out_path: str) -> None:
    all_names = [".notdef"] + [name for _, name, _ in GLYPH_TABLE]

    fb = FontBuilder(UPM, isTTF=True)
    fb.setupGlyphOrder(all_names)
    fb.setupCharacterMap({cp: name for cp, name, _ in GLYPH_TABLE})

    # Build glyf table via TTGlyphPen
    glyphs = {}

    # .notdef: empty square outline
    pen = TTGlyphPen(None)
    trace_polygon(pen, [(100, 0), (700, 0), (700, 700), (100, 700)])
    # inner hole (CCW) to make it an open-box .notdef
    trace_polygon(pen, [(150, 650), (650, 650), (650, 50), (150, 50)])
    glyphs[".notdef"] = pen.glyph()

    for _, name, draw_fn in GLYPH_TABLE:
        pen = TTGlyphPen(None)
        draw_fn(pen)
        glyphs[name] = pen.glyph()

    fb.setupGlyf(glyphs)

    # Metrics
    fb.setupHorizontalMetrics({
        ".notdef": (ADV, 100),
        **{name: (ADV, LSB) for _, name, _ in GLYPH_TABLE},
    })

    # Tables
    fb.setupHorizontalHeader(ascent=ASCENDER, descent=DESCENDER)
    fb.setupNameTable({
        "familyName": "Switch Icons Preview",
        "styleName":  "Regular",
    })
    fb.setupOS2(
        sTypoAscender=ASCENDER, sTypoDescender=DESCENDER,
        usWinAscent=ASCENDER,   usWinDescent=abs(DESCENDER),
        sxHeight=500, sCapHeight=700,
        fsType=0, achVendID="WS  ",
    )
    fb.setupHead(unitsPerEm=UPM)
    fb.setupPost()

    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    fb.font.save(out_path)
    size_kb = os.path.getsize(out_path) / 1024
    print(f"Generated: {out_path}  ({size_kb:.1f} KB, {len(GLYPH_TABLE)} glyphs)")


# ---------------------------------------------------------------------------
# Optional: generate User-Regular.ttf (Inter-Switch with NintendoExt stripped)
# ---------------------------------------------------------------------------
# NintendoExt PUA ranges — strip all so the FONT_SWITCH_ICONS fallback fires.
_NINTENDO_EXT_RANGES = [
    (0xE000, 0xE06B), (0xE070, 0xE07E), (0xE080, 0xE099),
    (0xE0A0, 0xE0BA), (0xE0C0, 0xE0D6), (0xE0E0, 0xE0F5),
    (0xE100, 0xE105), (0xE110, 0xE116), (0xE121, 0xE12C),
    (0xE130, 0xE13C), (0xE140, 0xE14D), (0xE150, 0xE153),
]


def _in_nintendo_ranges(cp: int) -> bool:
    return any(lo <= cp <= hi for lo, hi in _NINTENDO_EXT_RANGES)


def build_user_regular(inter_src: str, out_path: str) -> None:
    """
    Copy Inter-Switch.ttf → User-Regular.ttf with NintendoExt PUA codepoints
    removed from the cmap, so FONT_SWITCH_ICONS fallback works on desktop.
    """
    from fontTools.ttLib import TTFont as _TTFont
    font = _TTFont(inter_src)
    stripped = 0
    for subtable in font["cmap"].tables:
        if not hasattr(subtable, "cmap"):
            continue
        to_remove = [cp for cp in subtable.cmap if _in_nintendo_ranges(cp)]
        for cp in to_remove:
            del subtable.cmap[cp]
            stripped += 1
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    font.save(out_path)
    size_kb = os.path.getsize(out_path) / 1024
    print(f"Generated: {out_path}  ({size_kb:.1f} KB, {stripped} PUA entries stripped)")


if __name__ == "__main__":
    script_dir = os.path.dirname(os.path.abspath(__file__))
    out = (sys.argv[1] if len(sys.argv) > 1
           else os.path.join(script_dir, "User-Switch-Icons.ttf"))
    build_font(out)

    # Also regenerate User-Regular.ttf if Inter-Switch.ttf is nearby
    script_dir = os.path.dirname(os.path.abspath(__file__))
    inter_path = os.path.join(
        script_dir, "..", "..", "lib", "borealis", "resources", "inter", "Inter-Switch.ttf"
    )
    user_regular_out = os.path.join(script_dir, "User-Regular.ttf")
    if os.path.isfile(inter_path):
        build_user_regular(inter_path, user_regular_out)
    else:
        print(f"Note: Inter-Switch.ttf not found at {inter_path}, skipping User-Regular.ttf")
