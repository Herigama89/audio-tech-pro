# svgfaces.py out_dir: the Studio VCA and Vintage Opto faces from their vector designs (svg/*.svg).
# The static face is rendered (Chromium, twice the design's size) to a panel; what moves - the knobs' pointers, the
# meters' needles, the switches - is left out and drawn by the app; lamps are rendered lit, as parts.
#   python3 svgfaces.py ../../../scratchpad/comp/out   (then tools/gen.py out Source/ui/CompressorFaceData.h)
import json, os, re, sys
from playwright.sync_api import sync_playwright

HERE = os.path.dirname(os.path.abspath(__file__))
S = 2   # (the panel's pixels per design unit)


def clean_vca(svg):
    # no makers' names: a plain badge
    svg = svg.replace('>Matrix</text>', '>VCA</text>').replace('>RED 3</text>', '>STUDIO VCA</text>')
    svg = svg.replace('font-size="18" font-weight="900" letter-spacing="3"', 'font-size="15" font-weight="900" letter-spacing="2"')
    # what moves: the knobs' pointers, the needles, the two lit keys' lamps
    svg = re.sub(r'\s*<line x1="0" y1="0"[^>]*/>', '', svg)
    svg = re.sub(r'\s*<!-- Simulated Physical Needle -->\s*<line[^>]*/>', '', svg)
    svg = re.sub(r'\s*<!-- GR Needle[^>]*-->\s*<line[^>]*/>', '', svg)
    svg = re.sub(r'\s*<!-- Yellow glowing[^>]*-->\s*<circle[^>]*/>', '\n    <circle cx="0" cy="8" r="5" fill="#444"/>', svg)
    return svg


def clean_opto(svg):
    svg = svg.replace('>LA-2A</text>', '>VINTAGE OPTO</text>').replace('font-size="36" font-weight="bold" fill="#111111" text-anchor="middle" letter-spacing="4"',
                                                                         'font-size="30" font-weight="bold" fill="#111111" text-anchor="middle" letter-spacing="4"')
    svg = re.sub(r'>TELETRONIX LA-2A LEVELING AMPLIFIER<', '>LEVELING AMPLIFIER<', svg)
    svg = re.sub(r'\s*<line x1="500" y1="275"[^>]*/>', '', svg)                       # the needle
    svg = re.sub(r'\s*<circle[^>]*id="knob_[a-z_]+_indicator"/>', '', svg)            # the knobs' dots
    svg = re.sub(r'\s*<circle[^>]*id="switch_toggle_handle"/>', '', svg)              # the COMP / LIMIT bat
    svg = svg.replace('<rect x="880" y="315" width="20" height="20" fill="#555555"/>', '')   # the power switch's rocker
    # the pilot light dark (its lit glass is a lamp)
    svg = svg.replace('<circle cx="890" cy="240" r="14" fill="#D63031" stroke="#780808" stroke-width="2"/>',
                      '<circle cx="890" cy="240" r="14" fill="#5a1616" stroke="#3a0808" stroke-width="2"/>')
    svg = svg.replace('<circle cx="887" cy="237" r="5" fill="#FF7675" opacity="0.7"/>', '<circle cx="887" cy="237" r="5" fill="#a04040" opacity="0.35"/>')
    return svg


def render(page, svg, w, h, path, transparent=False):
    html = f'<html><body style="margin:0;background:{"transparent" if transparent else "#000"}">{svg}</body></html>'
    page.set_viewport_size({'width': w, 'height': h})
    page.set_content(html)
    page.screenshot(path=path, omit_background=transparent, clip={'x': 0, 'y': 0, 'width': w, 'height': h})


def lamp_svg(cx, cy, r, body):
    # (a lamp alone, in the design's units, cut to a square round it)
    m = r * 2.4
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{cx - m} {cy - m} {2 * m} {2 * m}" width="{int(2 * m * S)}" height="{int(2 * m * S)}">'
            f'<defs><filter id="glow" x="-100%" y="-100%" width="300%" height="300%"><feGaussianBlur stdDeviation="{r * 0.5}"/></filter></defs>{body}</svg>'), m


def main(out):
    from PIL import Image
    eff = os.path.join(out, 'png')
    os.makedirs(out, exist_ok=True)
    with sync_playwright() as p:
        b = p.chromium.launch()
        page = b.new_page(device_scale_factor=S)
        for name, src, cleaner in (('svca', 'studio_vca.svg', clean_vca), ('vopto', 'vintage_opto.svg', clean_opto)):
            svg = cleaner(open(os.path.join(HERE, 'svg', src)).read()).replace('width="100%" height="100%"', 'width="1000" height="450"')
            png = os.path.join(out, f'{name}_panel.png')
            render(page, svg, 1000, 450, png)
            Image.open(png).convert('RGB').save(os.path.join(out, f'{name}_panel.jpg'), quality=93)
            os.remove(png)
            parts = {}
            def knob(k, cx, cy, r):
                parts[k] = {'image': 'none', 'kind': 'pointer', 'cx': cx * S, 'cy': cy * S, 'R': r * S, 'angle': 0}
            def lamp(k, cx, cy, r, body):
                s_, m = lamp_svg(cx, cy, r, body)
                render(page, s_, int(2 * m), int(2 * m), os.path.join(out, f'{name}_{k}.png'), transparent=True)
                parts[k] = {'image': f'{name}_{k}_png', 'kind': 'lamp', 'cx': cx * S, 'cy': cy * S, 'R': m * S, 'angle': 0}
            if name == 'svca':
                for k, cx, cy in (('input', 230, 95), ('threshold', 230, 245), ('ratio', 380, 95), ('attack', 530, 95), ('release', 580, 245), ('blend', 380, 245)):
                    knob(k, cx, cy, 32)
                lit = ('<circle cx="{x}" cy="373" r="7" fill="#FFEB3B" opacity="0.55" filter="url(#glow)"/>'
                       '<circle cx="{x}" cy="373" r="5" fill="#FFEB3B"/><circle cx="{x1}" cy="371.5" r="1.8" fill="#fffbe0" opacity="0.9"/>')
                lamp('autoLamp', 480, 373, 5, lit.format(x=480, x1=478.6))
                lamp('linkLamp', 560, 373, 5, lit.format(x=560, x1=558.6))
            else:
                knob('peak', 230, 240, 70)
                knob('gain', 770, 240, 70)
                lamp('pilot', 890, 240, 14, '<circle cx="890" cy="240" r="20" fill="#ff3b30" opacity="0.45" filter="url(#glow)"/>'
                                            '<circle cx="890" cy="240" r="14" fill="#D63031" stroke="#780808" stroke-width="2"/>'
                                            '<circle cx="887" cy="237" r="5" fill="#FF7675" opacity="0.7"/>')
            json.dump({'panel': f'{name}_panel_jpg', 'size': [1000 * S, 450 * S], 'origin': [0, 0], 'parts': parts},
                      open(os.path.join(out, f'{name}.json'), 'w'), indent=1)
        b.close()


if __name__ == '__main__':
    main(sys.argv[1])
