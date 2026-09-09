#!/usr/bin/env python3
"""
gen_from_svg.py — Build User-Switch-Icons.ttf from real Figma SVG button artwork.

Source SVGs: switch/preview/fonts/svg/{A,B,X,Y,L,R,ZL,ZR,Plus,Minus}.svg
Source pack: "Switch Button Icons (Essential pack)" by Alvaro Polo Valdenebro
  https://www.figma.com/community/file/RYQbKWJa1nu4i9NiWbKEI2
  Community files are shared for personal/community use under their stated
  license. These icons are used for **developer preview only** — they are
  NOT shipped in the .nro or any distributed Waystone build.

Each glyph = button `shape` (filled body) with `label` (letter) knocked out as
a counter, so in the dark-themed footer the letter shows through as background.
Winding: shape → CW in font coords (outer fill); label → CCW (hole).

Requires: fontTools  (pip install fonttools  /  already in homebrew Python 3.14)
Usage:
  /opt/homebrew/bin/python3 switch/preview/fonts/gen_from_svg.py
"""

import math
import os
import re
import sys
import xml.etree.ElementTree as ET

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

# ---------------------------------------------------------------------------
# Font metrics  (kept in sync with the existing User-Regular.ttf / borealis)
# ---------------------------------------------------------------------------
UPM       = 1000
ASCENDER  =  800
DESCENDER = -200
ADV       =  900   # advance width (same for all glyphs)
LSB       =   50   # notional left side bearing

# SVG canvas is 32 × 32 with the icon centred at (16, 16).
# We map it into font space:   font_x = FONT_CX + (svg_x − 16) × SCALE
#                              font_y = FONT_CY − (svg_y − 16) × SCALE  ← flip Y
SVG_CX  = 16.0
SVG_CY  = 16.0
SCALE   = 30.0        # 1 SVG px → 30 font units; icon slightly above cap-height for legibility
FONT_CX = 450         # horizontal centre of the advance width
FONT_CY = 360         # circle bottom lands at baseline; top at 720 (comfortably below ascender)

# ---------------------------------------------------------------------------
# Glyph table  (codepoint, glyph-name, SVG button name)
# ---------------------------------------------------------------------------
GLYPH_TABLE = [
    (0xE0A0, "uniE0A0", "A"),
    (0xE0A1, "uniE0A1", "B"),
    (0xE0A2, "uniE0A2", "X"),
    (0xE0A3, "uniE0A3", "Y"),
    (0xE0A4, "uniE0A4", "L"),
    (0xE0A5, "uniE0A5", "R"),
    (0xE0A6, "uniE0A6", "ZL"),
    (0xE0A7, "uniE0A7", "ZR"),
    (0xE0B5, "uniE0B5", "Plus"),
    (0xE0B6, "uniE0B6", "Minus"),
]

# ---------------------------------------------------------------------------
# Coordinate helpers
# ---------------------------------------------------------------------------

def svg_to_font(x: float, y: float):
    """Convert SVG (y-down) → font (y-up) coordinates, rounded to integers."""
    return (
        round(FONT_CX + (x - SVG_CX) * SCALE),
        round(FONT_CY - (y - SVG_CY) * SCALE),
    )


def signed_area(pts):
    """
    Shoelace formula.
    Result > 0 → CCW in standard math / font coords (y-up)  → inner hole.
    Result < 0 → CW                                          → outer fill.
    """
    n = len(pts)
    a = 0.0
    for i in range(n):
        x0, y0 = pts[i]
        x1, y1 = pts[(i + 1) % n]
        a += x0 * y1 - x1 * y0
    return a * 0.5


def ensure_winding(pts, ccw: bool):
    """
    Reverse the point list if the winding direction is wrong.
    ccw=True  → contour must be CCW (inner hole / counter)
    ccw=False → contour must be CW  (outer filled body)
    """
    a = signed_area(pts)
    if abs(a) < 0.5:
        return pts          # degenerate — leave untouched
    if ccw and a < 0:
        return pts[::-1]    # was CW → reverse to CCW
    if not ccw and a > 0:
        return pts[::-1]    # was CCW → reverse to CW
    return pts


# ---------------------------------------------------------------------------
# SVG path parser
# ---------------------------------------------------------------------------

def _tokenize(d: str):
    """Split path data into command letters and numeric tokens."""
    return re.findall(
        r'[MmLlHhVvCcQqSsTtZz]'
        r'|[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?',
        d,
    )


def _sample_cubic(p0, p1, p2, p3, n: int = 10):
    """Return n points sampled along a cubic bezier (p0 excluded, p3 included)."""
    out = []
    for k in range(1, n + 1):
        t = k / n
        mt = 1.0 - t
        x = mt**3*p0[0] + 3*mt**2*t*p1[0] + 3*mt*t**2*p2[0] + t**3*p3[0]
        y = mt**3*p0[1] + 3*mt**2*t*p1[1] + 3*mt*t**2*p2[1] + t**3*p3[1]
        out.append((x, y))
    return out


def parse_svg_path(d: str):
    """
    Parse an SVG path data string.
    Returns a list of closed contours; each contour is a list of (x, y) floats.
    Handles M m L l H h V v C c S s Q q Z z.
    """
    tokens = _tokenize(d)
    contours = []
    current = []
    x = y = 0.0
    start_x = start_y = 0.0
    last_ctrl = None        # for S/s smooth cubic

    i = 0
    cmd = 'M'

    def read_float():
        nonlocal i
        v = float(tokens[i])
        i += 1
        return v

    def close_current():
        if len(current) >= 3:
            contours.append(list(current))
        current.clear()

    while i < len(tokens):
        tok = tokens[i]
        if tok.isalpha():
            cmd = tok
            i += 1
            if cmd in ('Z', 'z'):
                x, y = start_x, start_y
                close_current()
            continue

        # Numeric token — execute current command
        if cmd in ('M', 'm'):
            close_current()
            nx, ny = read_float(), read_float()
            x, y = (nx, ny) if cmd == 'M' else (x + nx, y + ny)
            start_x, start_y = x, y
            current.append((x, y))
            cmd = 'L' if cmd == 'M' else 'l'  # implicit lineto after moveto

        elif cmd in ('L', 'l'):
            nx, ny = read_float(), read_float()
            x, y = (nx, ny) if cmd == 'L' else (x + nx, y + ny)
            current.append((x, y))
            last_ctrl = None

        elif cmd in ('H', 'h'):
            nx = read_float()
            x = nx if cmd == 'H' else x + nx
            current.append((x, y))
            last_ctrl = None

        elif cmd in ('V', 'v'):
            ny = read_float()
            y = ny if cmd == 'V' else y + ny
            current.append((x, y))
            last_ctrl = None

        elif cmd in ('C', 'c'):
            if cmd == 'C':
                x1, y1 = read_float(), read_float()
                x2, y2 = read_float(), read_float()
                x3, y3 = read_float(), read_float()
            else:
                x1, y1 = x + read_float(), y + read_float()
                x2, y2 = x + read_float(), y + read_float()
                x3, y3 = x + read_float(), y + read_float()
            current.extend(_sample_cubic((x, y), (x1, y1), (x2, y2), (x3, y3)))
            last_ctrl = (x2, y2)
            x, y = x3, y3

        elif cmd in ('S', 's'):
            # Smooth cubic: first control point is reflection of last
            if last_ctrl is not None:
                x1, y1 = 2 * x - last_ctrl[0], 2 * y - last_ctrl[1]
            else:
                x1, y1 = x, y
            if cmd == 'S':
                x2, y2 = read_float(), read_float()
                x3, y3 = read_float(), read_float()
            else:
                x2, y2 = x + read_float(), y + read_float()
                x3, y3 = x + read_float(), y + read_float()
            current.extend(_sample_cubic((x, y), (x1, y1), (x2, y2), (x3, y3)))
            last_ctrl = (x2, y2)
            x, y = x3, y3

        elif cmd in ('Q', 'q'):
            if cmd == 'Q':
                x1, y1 = read_float(), read_float()
                x2, y2 = read_float(), read_float()
            else:
                x1, y1 = x + read_float(), y + read_float()
                x2, y2 = x + read_float(), y + read_float()
            # Sample quadratic bezier → 6 points
            for k in range(1, 7):
                t = k / 6.0
                mt = 1.0 - t
                px = mt**2 * x + 2 * mt * t * x1 + t**2 * x2
                py = mt**2 * y + 2 * mt * t * y1 + t**2 * y2
                current.append((px, py))
            last_ctrl = None
            x, y = x2, y2

        else:
            # Unknown or skipped command: consume one number to stay in sync
            try:
                read_float()
            except Exception:
                i += 1

    close_current()
    return contours


def circle_to_pts(cx: float, cy: float, r: float, n: int = 32):
    """Approximate a circle as an n-gon in SVG (y-down) coordinates."""
    pts = []
    for k in range(n):
        angle = math.pi / 2.0 - (2.0 * math.pi / n) * k
        pts.append((cx + r * math.cos(angle), cy + r * math.sin(angle)))
    return pts


# ---------------------------------------------------------------------------
# SVG geometry extraction
# ---------------------------------------------------------------------------

def _strip_ns(tag: str) -> str:
    """'{ http://... }circle' → 'circle'"""
    return tag.split('}')[-1] if '}' in tag else tag


def _find_by_id(root, eid: str):
    """Return the first descendant with id=eid, or None."""
    for el in root.iter():
        if el.get('id') == eid:
            return el
    return None


def get_button_geometry(svg_path: str, button_name: str):
    """
    Parse the SVG and extract shape + label contours in SVG coordinate space.

    Returns (shape_contours, label_contours):
      shape_contours — list of [(x,y),...] for the button body
      label_contours — list of [(x,y),...] for the letter/symbol
    """
    tree = ET.parse(svg_path)
    root = tree.getroot()

    group_id = f"{button_name}_Button"
    btn_group = _find_by_id(root, group_id)
    if btn_group is None:
        raise ValueError(f"Group '{group_id}' not found in {svg_path}")

    # ---- shape ----
    shape_el = _find_by_id(btn_group, "shape")
    if shape_el is None:
        raise ValueError(f"No #shape in {group_id}")

    tag = _strip_ns(shape_el.tag)
    if tag == 'circle':
        cx = float(shape_el.get('cx', 0))
        cy = float(shape_el.get('cy', 0))
        r  = float(shape_el.get('r',  0))
        shape_contours = [circle_to_pts(cx, cy, r, n=32)]
    elif tag == 'path':
        shape_contours = parse_svg_path(shape_el.get('d', ''))
    else:
        raise ValueError(f"Unexpected shape tag '{tag}' in {group_id}")

    # ---- label ----
    label_el = _find_by_id(btn_group, "label")
    if label_el is None:
        raise ValueError(f"No #label in {group_id}")

    label_tag = _strip_ns(label_el.tag)
    label_contours = []
    if label_tag == 'path':
        label_contours = parse_svg_path(label_el.get('d', ''))
    elif label_tag == 'g':
        # ZL / ZR: label is a group containing two path elements
        for child in label_el:
            if _strip_ns(child.tag) == 'path':
                label_contours.extend(parse_svg_path(child.get('d', '')))
    else:
        raise ValueError(f"Unexpected label tag '{label_tag}' in {group_id}")

    return shape_contours, label_contours


# ---------------------------------------------------------------------------
# TTF glyph drawing
# ---------------------------------------------------------------------------

def _draw_contour(pen, svg_pts, ccw: bool):
    """
    Convert SVG contour to font coordinates, enforce winding, and draw with pen.
    """
    if len(svg_pts) < 3:
        return
    font_pts = [svg_to_font(x, y) for x, y in svg_pts]
    font_pts = ensure_winding(font_pts, ccw=ccw)
    pen.moveTo(font_pts[0])
    for pt in font_pts[1:]:
        pen.lineTo(pt)
    pen.endPath()


def draw_glyph(pen, shape_contours, label_contours):
    """
    shape contours → CW in font coords (outer filled body).
    label contours → CCW in font coords (knocked-out letter / hole).
    """
    for svg_pts in shape_contours:
        _draw_contour(pen, svg_pts, ccw=False)  # CW = outer fill
    for svg_pts in label_contours:
        _draw_contour(pen, svg_pts, ccw=True)   # CCW = inner hole


# ---------------------------------------------------------------------------
# Font builder
# ---------------------------------------------------------------------------

def build_font(svg_dir: str, out_path: str) -> None:
    all_names = [".notdef"] + [n for _, n, _ in GLYPH_TABLE]

    fb = FontBuilder(UPM, isTTF=True)
    fb.setupGlyphOrder(all_names)
    fb.setupCharacterMap({cp: n for cp, n, _ in GLYPH_TABLE})

    glyphs: dict = {}

    # .notdef — simple hollow square
    pen = TTGlyphPen(None)
    pen.moveTo((100, 0));  pen.lineTo((700, 0));  pen.lineTo((700, 700)); pen.lineTo((100, 700)); pen.endPath()
    pen.moveTo((650, 50)); pen.lineTo((650, 650)); pen.lineTo((150, 650)); pen.lineTo((150, 50));  pen.endPath()
    glyphs[".notdef"] = pen.glyph()

    for cp, name, btn_name in GLYPH_TABLE:
        svg_file = os.path.join(svg_dir, f"{btn_name}.svg")
        try:
            shape_c, label_c = get_button_geometry(svg_file, btn_name)
        except Exception as exc:
            print(f"WARNING [{btn_name}]: {exc}", file=sys.stderr)
            pen = TTGlyphPen(None)
            glyphs[name] = pen.glyph()
            continue

        pen = TTGlyphPen(None)
        draw_glyph(pen, shape_c, label_c)
        glyphs[name] = pen.glyph()
        print(f"  {btn_name:6s}  shape={len(shape_c)} contour(s), label={len(label_c)} contour(s)")

    fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics({
        ".notdef": (ADV, 100),
        **{n: (ADV, LSB) for _, n, _ in GLYPH_TABLE},
    })
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
    print(f"\nGenerated: {out_path}  ({size_kb:.1f} KB, {len(GLYPH_TABLE)} glyphs)")


# ---------------------------------------------------------------------------
# User-Regular.ttf helper (unchanged from gen_switch_icons.py)
# ---------------------------------------------------------------------------

_NINTENDO_EXT_RANGES = [
    (0xE000, 0xE06B), (0xE070, 0xE07E), (0xE080, 0xE099),
    (0xE0A0, 0xE0BA), (0xE0C0, 0xE0D6), (0xE0E0, 0xE0F5),
    (0xE100, 0xE105), (0xE110, 0xE116), (0xE121, 0xE12C),
    (0xE130, 0xE13C), (0xE140, 0xE14D), (0xE150, 0xE153),
]


def _in_nintendo_ranges(cp: int) -> bool:
    return any(lo <= cp <= hi for lo, hi in _NINTENDO_EXT_RANGES)


def build_user_regular(inter_src: str, out_path: str) -> None:
    """Strip NintendoExt PUA codepoints from Inter-Switch.ttf → User-Regular.ttf."""
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


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    script_dir = os.path.dirname(os.path.abspath(__file__))
    svg_dir    = os.path.join(script_dir, "svg")
    out_path   = (sys.argv[1]
                  if len(sys.argv) > 1
                  else os.path.join(script_dir, "User-Switch-Icons.ttf"))

    print(f"Building {out_path} from SVGs in {svg_dir} …\n")
    build_font(svg_dir, out_path)

    inter_path       = os.path.join(script_dir, "..", "..", "lib", "borealis",
                                    "resources", "inter", "Inter-Switch.ttf")
    user_regular_out = os.path.join(script_dir, "User-Regular.ttf")
    if os.path.isfile(inter_path):
        build_user_regular(inter_path, user_regular_out)
    else:
        print(f"Note: Inter-Switch.ttf not found at {inter_path!r}; skipping User-Regular.ttf")
