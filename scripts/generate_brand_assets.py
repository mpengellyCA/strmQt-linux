#!/usr/bin/env python3
"""
StrmQt Brand Asset Generator
Generates canonical vector SVGs and raster PNG icons for StrmQt
embodying the "Projection Booth" design language.
"""

import os
import math
import subprocess
from matplotlib import font_manager
from matplotlib.textpath import TextPath

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BRAND_DIR = os.path.join(REPO_ROOT, "assets", "brand")
ICONS_DIR = os.path.join(REPO_ROOT, "assets", "icons")
FONTS_DIR = os.path.join(REPO_ROOT, "assets", "fonts")

os.makedirs(BRAND_DIR, exist_ok=True)

# Color palettes according to Theme.qml
COLORS = {
    "ground": "#0C0B0A",
    "surface": "#141210",
    "surfaceRaised": "#1D1A17",
    "hairline": "#2E2A26",
    "track": "#262220",
    "textPrimary": "#F5F1EA",
    "textSecondary": "#A29A8E",
    "textTertiary": "#928A80",
    # Theme accents
    "projection": {"main": "#F0A02A", "bright": "#FFBE53", "deep": "#C87A10", "glow": "#F0A02A"},
    "emby":       {"main": "#52B54B", "bright": "#6FD866", "deep": "#3B8E35", "glow": "#52B54B"},
    "breeze":     {"main": "#3DAEE9", "bright": "#6FD0F7", "deep": "#2584B6", "glow": "#3DAEE9"},
    "jellyfin":   {"main": "#AA5CC3", "bright": "#C87CE0", "deep": "#7E3996", "glow": "#AA5CC3"},
}

def path_to_svg_d(text_path, offset_x=0.0, offset_y=0.0, scale_x=1.0, scale_y=-1.0):
    """Converts a matplotlib TextPath to an SVG path 'd' string."""
    parts = []
    vertices = text_path.vertices
    codes = text_path.codes
    i = 0
    while i < len(codes):
        code = codes[i]
        if code == TextPath.MOVETO:
            x = (vertices[i][0] * scale_x) + offset_x
            y = (vertices[i][1] * scale_y) + offset_y
            parts.append(f"M {x:.2f} {y:.2f}")
            i += 1
        elif code == TextPath.LINETO:
            x = (vertices[i][0] * scale_x) + offset_x
            y = (vertices[i][1] * scale_y) + offset_y
            parts.append(f"L {x:.2f} {y:.2f}")
            i += 1
        elif code == TextPath.CURVE3:
            x1 = (vertices[i][0] * scale_x) + offset_x
            y1 = (vertices[i][1] * scale_y) + offset_y
            x = (vertices[i+1][0] * scale_x) + offset_x
            y = (vertices[i+1][1] * scale_y) + offset_y
            parts.append(f"Q {x1:.2f} {y1:.2f} {x:.2f} {y:.2f}")
            i += 2
        elif code == TextPath.CURVE4:
            x1 = (vertices[i][0] * scale_x) + offset_x
            y1 = (vertices[i][1] * scale_y) + offset_y
            x2 = (vertices[i+1][0] * scale_x) + offset_x
            y2 = (vertices[i+1][1] * scale_y) + offset_y
            x = (vertices[i+2][0] * scale_x) + offset_x
            y = (vertices[i+2][1] * scale_y) + offset_y
            parts.append(f"C {x1:.2f} {y1:.2f} {x2:.2f} {y2:.2f} {x:.2f} {y:.2f}")
            i += 3
        elif code == TextPath.CLOSEPOLY:
            parts.append("Z")
            i += 1
        else:
            i += 1
    return " ".join(parts)


def generate_aperture_q_mark_paths(cx=256, cy=256, r_out=172, r_in=82, gap=6.5):
    """
    Constructs the 6 iris blades and Q-tail for the Aperture-Q symbol.
    """
    r_tan = r_in * 0.80
    s_in = math.sqrt(r_in**2 - r_tan**2)
    s_out = math.sqrt(r_out**2 - r_tan**2)
    
    blades = []
    for i in range(6):
        a_lead = i * (math.pi / 3.0)
        a_trail = (i + 1) * (math.pi / 3.0)
        
        u_lead = (math.cos(a_lead), math.sin(a_lead))
        v_lead = (-math.sin(a_lead), math.cos(a_lead))
        
        u_trail = (math.cos(a_trail), math.sin(a_trail))
        v_trail = (-math.sin(a_trail), math.cos(a_trail))
        
        # Leading edge shifted
        p_lead_in = (cx + r_tan * u_lead[0] + s_in * v_lead[0] + (gap/2.0) * u_lead[0],
                     cy + r_tan * u_lead[1] + s_in * v_lead[1] + (gap/2.0) * u_lead[1])
        p_lead_out = (cx + r_tan * u_lead[0] + s_out * v_lead[0] + (gap/2.0) * u_lead[0],
                      cy + r_tan * u_lead[1] + s_out * v_lead[1] + (gap/2.0) * u_lead[1])
                      
        # Trailing edge shifted
        p_trail_out = (cx + r_tan * u_trail[0] + s_out * v_trail[0] - (gap/2.0) * u_trail[0],
                       cy + r_tan * u_trail[1] + s_out * v_trail[1] - (gap/2.0) * u_trail[1])
        p_trail_in = (cx + r_tan * u_trail[0] + s_in * v_trail[0] - (gap/2.0) * u_trail[0],
                      cy + r_tan * u_trail[1] + s_in * v_trail[1] - (gap/2.0) * u_trail[1])
                      
        d = (f"M {p_lead_in[0]:.2f} {p_lead_in[1]:.2f} "
             f"L {p_lead_out[0]:.2f} {p_lead_out[1]:.2f} "
             f"A {r_out:.2f} {r_out:.2f} 0 0 1 {p_trail_out[0]:.2f} {p_trail_out[1]:.2f} "
             f"L {p_trail_in[0]:.2f} {p_trail_in[1]:.2f} "
             f"A {r_in:.2f} {r_in:.2f} 0 0 0 {p_lead_in[0]:.2f} {p_lead_in[1]:.2f} Z")
        blades.append(d)
        
    # Q-tail: angled stem emerging from blade 0 in the bottom-right (angle ~45 deg)
    # Starts around (cx + 0.65*r_out, cy + 0.65*r_out) and extends outward
    tail_path = (f"M {cx + 80:.2f} {cy + 96:.2f} "
                 f"L {cx + 172:.2f} {cy + 188:.2f} "
                 f"A 16 16 0 0 1 {cx + 150:.2f} {cy + 210:.2f} "
                 f"L {cx + 56:.2f} {cy + 116:.2f} Z")
                 
    # Centered play triangle pointing right
    # Inside r_in: height ~ 96, width ~ 84
    p1 = (cx - 26, cy - 48)
    p2 = (cx - 26, cy + 48)
    p3 = (cx + 42, cy)
    play_path = f"M {p1[0]} {p1[1]} L {p2[0]} {p2[1]} L {p3[0]} {p3[1]} Z"
    
    return blades, tail_path, play_path


def build_app_icon_svg(accent="projection", size=512):
    """
    Builds the canonical desktop application icon (ca.mikesdev.StrmQt.svg).
    """
    acc = COLORS[accent]
    cx, cy = size / 2.0, size / 2.0
    r_track = 176.0
    
    svg = [
        f'<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{size}" height="{size}" viewBox="0 0 {size} {size}" role="img" aria-label="StrmQt">',
        '  <defs>',
        f'    <linearGradient id="tileGrad" x1="0%" y1="0%" x2="0%" y2="100%">',
        f'      <stop offset="0%" stop-color="{COLORS["surfaceRaised"]}"/>',
        f'      <stop offset="100%" stop-color="{COLORS["ground"]}"/>',
        '    </linearGradient>',
        '    <linearGradient id="accentGrad" x1="0%" y1="0%" x2="100%" y2="100%">',
        f'      <stop offset="0%" stop-color="{acc["bright"]}"/>',
        f'      <stop offset="60%" stop-color="{acc["main"]}"/>',
        f'      <stop offset="100%" stop-color="{acc["deep"]}"/>',
        '    </linearGradient>',
        '    <radialGradient id="projectorGlow" cx="50%" cy="50%" r="50%">',
        f'      <stop offset="0%" stop-color="{acc["glow"]}" stop-opacity="0.26"/>',
        f'      <stop offset="65%" stop-color="{acc["glow"]}" stop-opacity="0.06"/>',
        f'      <stop offset="100%" stop-color="{acc["glow"]}" stop-opacity="0"/>',
        '    </radialGradient>',
        '  </defs>',
        f'  <!-- Ground Squircle -->',
        f'  <rect x="0" y="0" width="{size}" height="{size}" rx="112" ry="112" fill="url(#tileGrad)"/>',
        f'  <rect x="1.5" y="1.5" width="{size-3}" height="{size-3}" rx="110.5" ry="110.5" fill="none" stroke="{COLORS["hairline"]}" stroke-width="3" stroke-opacity="0.9"/>',
        f'  <!-- Projector Beam Glow -->',
        f'  <circle cx="{cx}" cy="{cy}" r="220" fill="url(#projectorGlow)"/>',
        f'  <!-- Media Progress Track -->',
        f'  <circle cx="{cx}" cy="{cy}" r="{r_track}" fill="none" stroke="{COLORS["track"]}" stroke-width="18"/>',
        f'  <!-- 270-degree Progress Sweep Arc (sweep length 830 of 1106) -->',
        f'  <circle cx="{cx}" cy="{cy}" r="{r_track}" fill="none" stroke="url(#accentGrad)" stroke-width="18" stroke-linecap="round" stroke-dasharray="829 277" transform="rotate(-90 {cx} {cy})"/>',
    ]
    
    # 4 Lens / Film registration tick marks
    for deg in [0, 90, 180, 270]:
        rad = math.radians(deg)
        x1 = cx + (r_track - 13) * math.cos(rad)
        y1 = cy + (r_track - 13) * math.sin(rad)
        x2 = cx + (r_track + 13) * math.cos(rad)
        y2 = cy + (r_track + 13) * math.sin(rad)
        svg.append(f'  <line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" stroke="{COLORS["ground"]}" stroke-width="3.5" stroke-linecap="round"/>')
        
    # Aperture blades & play symbol
    blades, tail, play = generate_aperture_q_mark_paths(cx=cx, cy=cy, r_out=134, r_in=66, gap=5.5)
    
    svg.append('  <!-- Aperture Iris Blades -->')
    svg.append('  <g fill="url(#accentGrad)">')
    for d in blades:
        svg.append(f'    <path d="{d}"/>')
    svg.append(f'    <!-- Q Tail -->\n    <path d="{tail}"/>')
    svg.append('  </g>')
    
    svg.append(f'  <!-- Center Play Glyph -->')
    svg.append(f'  <path d="{play}" fill="url(#accentGrad)" stroke="url(#accentGrad)" stroke-width="14" stroke-linejoin="round" stroke-linecap="round"/>')
    svg.append('</svg>\n')
    
    return "\n".join(svg)


def build_symbol_svg(accent="projection", monochrome=False, mono_color="#F5F1EA"):
    """
    Builds the standalone brand mark (Aperture-Q Play Symbol).
    """
    acc = COLORS[accent]
    cx, cy = 256.0, 256.0
    
    svg = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<svg xmlns="http://www.w3.org/2000/svg" width="512" height="512" viewBox="0 0 512 512" role="img" aria-label="StrmQt Brand Mark">',
    ]
    
    if not monochrome:
        svg.extend([
            '  <defs>',
            '    <linearGradient id="markGrad" x1="0%" y1="0%" x2="100%" y2="100%">',
            f'      <stop offset="0%" stop-color="{acc["bright"]}"/>',
            f'      <stop offset="55%" stop-color="{acc["main"]}"/>',
            f'      <stop offset="100%" stop-color="{acc["deep"]}"/>',
            '    </linearGradient>',
            '    <radialGradient id="markGlow" cx="50%" cy="50%" r="50%">',
            f'      <stop offset="0%" stop-color="{acc["glow"]}" stop-opacity="0.30"/>',
            f'      <stop offset="70%" stop-color="{acc["glow"]}" stop-opacity="0.05"/>',
            f'      <stop offset="100%" stop-color="{acc["glow"]}" stop-opacity="0"/>',
            '    </radialGradient>',
            '  </defs>',
            f'  <circle cx="{cx}" cy="{cy}" r="210" fill="url(#markGlow)"/>',
        ])
        fill_attr = 'fill="url(#markGrad)"'
        stroke_attr = 'stroke="url(#markGrad)"'
    else:
        fill_attr = f'fill="{mono_color}"'
        stroke_attr = f'stroke="{mono_color}"'
        
    blades, tail, play = generate_aperture_q_mark_paths(cx=cx, cy=cy, r_out=176, r_in=86, gap=7.0)
    
    svg.append(f'  <g {fill_attr}>')
    for d in blades:
        svg.append(f'    <path d="{d}"/>')
    svg.append(f'    <path d="{tail}"/>')
    svg.append('  </g>')
    svg.append(f'  <path d="{play}" {fill_attr} {stroke_attr} stroke-width="18" stroke-linejoin="round" stroke-linecap="round"/>')
    svg.append('</svg>\n')
    
    return "\n".join(svg)


def build_horizontal_logo_svg(light_mode=False):
    """
    Builds the master horizontal logo lockup with pure vector paths.
    """
    fp_bold = font_manager.FontProperties(fname=os.path.join(FONTS_DIR, "Archivo[wdth,wght].ttf"), weight="bold")
    fp_mono = font_manager.FontProperties(fname=os.path.join(FONTS_DIR, "IBMPlexMono-SemiBold.ttf"))
    
    tp_strm = TextPath((0, 0), "Strm", size=136, prop=fp_bold)
    tp_qt = TextPath((0, 0), "Qt", size=136, prop=fp_bold)
    
    # Subtitle letter-spaced
    sub_text = "NATIVE MEDIA CLIENT"
    tp_sub = TextPath((0, 0), sub_text, size=22, prop=fp_mono)
    
    width = 920
    height = 280
    sym_cx, sym_cy = 130.0, 140.0
    
    # Text positions
    text_x_start = 280.0
    text_y_baseline = 168.0
    
    strm_w = tp_strm.get_extents().width
    d_strm = path_to_svg_d(tp_strm, offset_x=text_x_start, offset_y=text_y_baseline)
    d_qt = path_to_svg_d(tp_qt, offset_x=text_x_start + strm_w + 4.0, offset_y=text_y_baseline)
    d_sub = path_to_svg_d(tp_sub, offset_x=text_x_start + 4.0, offset_y=text_y_baseline + 42.0)
    
    blades, tail, play = generate_aperture_q_mark_paths(cx=sym_cx, cy=sym_cy, r_out=96, r_in=46, gap=4.5)
    
    if not light_mode:
        bg_fill = COLORS["ground"]
        strm_color = COLORS["textPrimary"]
        qt_color = COLORS["projection"]["main"]
        sub_color = COLORS["textTertiary"]
        acc = COLORS["projection"]
    else:
        bg_fill = "#FFFFFF"
        strm_color = COLORS["ground"]
        qt_color = COLORS["projection"]["deep"]
        sub_color = "#5F5851"
        acc = COLORS["projection"]
        
    svg = [
        f'<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}" role="img" aria-label="StrmQt Logo">',
        '  <defs>',
        '    <linearGradient id="logoAmber" x1="0%" y1="0%" x2="100%" y2="100%">',
        f'      <stop offset="0%" stop-color="{acc["bright"]}"/>',
        f'      <stop offset="60%" stop-color="{acc["main"]}"/>',
        f'      <stop offset="100%" stop-color="{acc["deep"]}"/>',
        '    </linearGradient>',
        '  </defs>',
    ]
    
    if not light_mode:
        svg.append(f'  <rect width="{width}" height="{height}" fill="{bg_fill}"/>')
        
    svg.append('  <!-- Symbol -->')
    svg.append('  <g fill="url(#logoAmber)">')
    for d in blades:
        svg.append(f'    <path d="{d}"/>')
    svg.append(f'    <path d="{tail}"/>')
    svg.append('  </g>')
    svg.append(f'  <path d="{play}" fill="url(#logoAmber)" stroke="url(#logoAmber)" stroke-width="10" stroke-linejoin="round" stroke-linecap="round"/>')
    
    svg.append('  <!-- Wordmark -->')
    svg.append(f'  <path d="{d_strm}" fill="{strm_color}"/>')
    svg.append(f'  <path d="{d_qt}" fill="{qt_color}"/>')
    svg.append(f'  <path d="{d_sub}" fill="{sub_color}"/>')
    
    svg.append('</svg>\n')
    return "\n".join(svg)


def build_stacked_logo_svg():
    """
    Builds the stacked vertical brand lockup.
    """
    fp_bold = font_manager.FontProperties(fname=os.path.join(FONTS_DIR, "Archivo[wdth,wght].ttf"), weight="bold")
    fp_mono = font_manager.FontProperties(fname=os.path.join(FONTS_DIR, "IBMPlexMono-SemiBold.ttf"))
    
    tp_strm = TextPath((0, 0), "Strm", size=108, prop=fp_bold)
    tp_qt = TextPath((0, 0), "Qt", size=108, prop=fp_bold)
    tp_sub = TextPath((0, 0), "PROJECTION BOOTH", size=18, prop=fp_mono)
    
    width = 512
    height = 512
    sym_cx, sym_cy = 256.0, 180.0
    
    strm_w = tp_strm.get_extents().width
    qt_w = tp_qt.get_extents().width
    total_w = strm_w + qt_w + 3.0
    
    text_x_start = (width - total_w) / 2.0
    text_y_baseline = 380.0
    
    sub_w = tp_sub.get_extents().width
    sub_x = (width - sub_w) / 2.0
    
    d_strm = path_to_svg_d(tp_strm, offset_x=text_x_start, offset_y=text_y_baseline)
    d_qt = path_to_svg_d(tp_qt, offset_x=text_x_start + strm_w + 3.0, offset_y=text_y_baseline)
    d_sub = path_to_svg_d(tp_sub, offset_x=sub_x, offset_y=text_y_baseline + 40.0)
    
    blades, tail, play = generate_aperture_q_mark_paths(cx=sym_cx, cy=sym_cy, r_out=118, r_in=58, gap=5.0)
    acc = COLORS["projection"]
    
    svg = [
        f'<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}" role="img" aria-label="StrmQt Stacked Logo">',
        f'  <rect width="{width}" height="{height}" fill="{COLORS["ground"]}"/>',
        '  <defs>',
        '    <linearGradient id="amberGrad" x1="0%" y1="0%" x2="100%" y2="100%">',
        f'      <stop offset="0%" stop-color="{acc["bright"]}"/>',
        f'      <stop offset="60%" stop-color="{acc["main"]}"/>',
        f'      <stop offset="100%" stop-color="{acc["deep"]}"/>',
        '    </linearGradient>',
        '  </defs>',
        '  <!-- Mark -->',
        '  <g fill="url(#amberGrad)">',
    ]
    for d in blades:
        svg.append(f'    <path d="{d}"/>')
    svg.append(f'    <path d="{tail}"/>')
    svg.append('  </g>')
    svg.append(f'  <path d="{play}" fill="url(#amberGrad)" stroke="url(#amberGrad)" stroke-width="12" stroke-linejoin="round" stroke-linecap="round"/>')
    svg.append(f'  <path d="{d_strm}" fill="{COLORS["textPrimary"]}"/>')
    svg.append(f'  <path d="{d_qt}" fill="{COLORS["projection"]["main"]}"/>')
    svg.append(f'  <path d="{d_sub}" fill="{COLORS["textTertiary"]}"/>')
    svg.append('</svg>\n')
    return "\n".join(svg)


def build_badge_svg():
    """
    Builds a framed showcase badge on a squircle card.
    """
    w, h = 600, 340
    sym_cx, sym_cy = 135.0, 170.0
    
    fp_bold = font_manager.FontProperties(fname=os.path.join(FONTS_DIR, "Archivo[wdth,wght].ttf"), weight="bold")
    fp_mono = font_manager.FontProperties(fname=os.path.join(FONTS_DIR, "IBMPlexMono-SemiBold.ttf"))
    fp_body = font_manager.FontProperties(fname=os.path.join(FONTS_DIR, "PublicSans[wght].ttf"))
    
    tp_strm = TextPath((0, 0), "Strm", size=100, prop=fp_bold)
    tp_qt = TextPath((0, 0), "Qt", size=100, prop=fp_bold)
    tp_tag = TextPath((0, 0), "PROJECTION BOOTH", size=15, prop=fp_mono)
    tp_desc = TextPath((0, 0), "Native Media Center for Linux", size=18, prop=fp_body)
    
    text_x = 245.0
    text_y = 158.0
    strm_w = tp_strm.get_extents().width
    
    d_strm = path_to_svg_d(tp_strm, offset_x=text_x, offset_y=text_y)
    d_qt = path_to_svg_d(tp_qt, offset_x=text_x + strm_w + 3.0, offset_y=text_y)
    d_tag = path_to_svg_d(tp_tag, offset_x=text_x + 2.0, offset_y=text_y - 88.0)
    d_desc = path_to_svg_d(tp_desc, offset_x=text_x + 2.0, offset_y=text_y + 44.0)
    
    blades, tail, play = generate_aperture_q_mark_paths(cx=sym_cx, cy=sym_cy, r_out=88, r_in=42, gap=4.2)
    acc = COLORS["projection"]
    
    svg = [
        f'<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}" role="img" aria-label="StrmQt Badge">',
        '  <defs>',
        f'    <linearGradient id="cardGrad" x1="0%" y1="0%" x2="0%" y2="100%">',
        f'      <stop offset="0%" stop-color="{COLORS["surfaceRaised"]}"/>',
        f'      <stop offset="100%" stop-color="{COLORS["ground"]}"/>',
        '    </linearGradient>',
        '    <linearGradient id="badgeAmber" x1="0%" y1="0%" x2="100%" y2="100%">',
        f'      <stop offset="0%" stop-color="{acc["bright"]}"/>',
        f'      <stop offset="60%" stop-color="{acc["main"]}"/>',
        f'      <stop offset="100%" stop-color="{acc["deep"]}"/>',
        '    </linearGradient>',
        '  </defs>',
        f'  <rect x="0" y="0" width="{w}" height="{h}" rx="28" fill="url(#cardGrad)"/>',
        f'  <rect x="1" y="1" width="{w-2}" height="{h-2}" rx="27" fill="none" stroke="{COLORS["hairline"]}" stroke-width="2"/>',
        '  <!-- Mark -->',
        '  <g fill="url(#badgeAmber)">',
    ]
    for d in blades:
        svg.append(f'    <path d="{d}"/>')
    svg.append(f'    <path d="{tail}"/>')
    svg.append('  </g>')
    svg.append(f'  <path d="{play}" fill="url(#badgeAmber)" stroke="url(#badgeAmber)" stroke-width="10" stroke-linejoin="round" stroke-linecap="round"/>')
    svg.append(f'  <path d="{d_tag}" fill="{COLORS["projection"]["main"]}"/>')
    svg.append(f'  <path d="{d_strm}" fill="{COLORS["textPrimary"]}"/>')
    svg.append(f'  <path d="{d_qt}" fill="{COLORS["projection"]["main"]}"/>')
    svg.append(f'  <path d="{d_desc}" fill="{COLORS["textSecondary"]}"/>')
    svg.append('</svg>\n')
    return "\n".join(svg)


def main():
    print("Generating StrmQt Brand Assets...")
    
    # 1. Desktop Canonical App Icon
    app_icon_svg = build_app_icon_svg(accent="projection", size=512)
    app_icon_path = os.path.join(ICONS_DIR, "ca.mikesdev.StrmQt.svg")
    with open(app_icon_path, "w", encoding="utf-8") as f:
        f.write(app_icon_svg)
    print(f"  ✓ {app_icon_path}")
    
    # 2. Accent variant icons in assets/brand/
    for theme_name in ["amber", "emby", "breeze", "jellyfin"]:
        key = "projection" if theme_name == "amber" else theme_name
        svg_content = build_app_icon_svg(accent=key, size=512)
        out_path = os.path.join(BRAND_DIR, f"strmqt-icon-{theme_name}.svg")
        with open(out_path, "w", encoding="utf-8") as f:
            f.write(svg_content)
        print(f"  ✓ {out_path}")
        
    # 3. Standalone brand marks
    symbol_full = build_symbol_svg(accent="projection", monochrome=False)
    with open(os.path.join(BRAND_DIR, "strmqt-symbol.svg"), "w", encoding="utf-8") as f:
        f.write(symbol_full)
    print(f"  ✓ {os.path.join(BRAND_DIR, 'strmqt-symbol.svg')}")
    
    symbol_mono_white = build_symbol_svg(monochrome=True, mono_color="#F5F1EA")
    with open(os.path.join(BRAND_DIR, "strmqt-symbol-monochrome-white.svg"), "w", encoding="utf-8") as f:
        f.write(symbol_mono_white)
    print(f"  ✓ {os.path.join(BRAND_DIR, 'strmqt-symbol-monochrome-white.svg')}")

    symbol_mono_dark = build_symbol_svg(monochrome=True, mono_color="#0C0B0A")
    with open(os.path.join(BRAND_DIR, "strmqt-symbol-monochrome-dark.svg"), "w", encoding="utf-8") as f:
        f.write(symbol_mono_dark)
    print(f"  ✓ {os.path.join(BRAND_DIR, 'strmqt-symbol-monochrome-dark.svg')}")

    # 4. Logo lockups
    logo_horiz = build_horizontal_logo_svg(light_mode=False)
    with open(os.path.join(BRAND_DIR, "strmqt-logo-horizontal.svg"), "w", encoding="utf-8") as f:
        f.write(logo_horiz)
    print(f"  ✓ {os.path.join(BRAND_DIR, 'strmqt-logo-horizontal.svg')}")

    logo_horiz_light = build_horizontal_logo_svg(light_mode=True)
    with open(os.path.join(BRAND_DIR, "strmqt-logo-horizontal-light.svg"), "w", encoding="utf-8") as f:
        f.write(logo_horiz_light)
    print(f"  ✓ {os.path.join(BRAND_DIR, 'strmqt-logo-horizontal-light.svg')}")

    logo_stacked = build_stacked_logo_svg()
    with open(os.path.join(BRAND_DIR, "strmqt-logo-stacked.svg"), "w", encoding="utf-8") as f:
        f.write(logo_stacked)
    print(f"  ✓ {os.path.join(BRAND_DIR, 'strmqt-logo-stacked.svg')}")

    logo_badge = build_badge_svg()
    with open(os.path.join(BRAND_DIR, "strmqt-badge.svg"), "w", encoding="utf-8") as f:
        f.write(logo_badge)
    print(f"  ✓ {os.path.join(BRAND_DIR, 'strmqt-badge.svg')}")

    # 5. Rasterize hicolor PNGs using rsvg-convert
    sizes = [32, 48, 64, 128, 256, 512]
    for s in sizes:
        png_dir = os.path.join(ICONS_DIR, "hicolor", f"{s}x{s}", "apps")
        os.makedirs(png_dir, exist_ok=True)
        png_path = os.path.join(png_dir, "ca.mikesdev.StrmQt.png")
        cmd = ["rsvg-convert", "-w", str(s), "-h", str(s), "-o", png_path, app_icon_path]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            print(f"  ✗ Failed to rasterize {s}x{s}: {res.stderr}")
        else:
            print(f"  ✓ {png_path} ({s}x{s})")
            
    print("\nAll StrmQt Brand Assets generated successfully!")

if __name__ == "__main__":
    main()
