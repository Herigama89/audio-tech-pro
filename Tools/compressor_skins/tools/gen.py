# gen.py out_dir header: the faces' layouts as a C++ table (Source/ui/CompressorFaceData.h)
import json, sys, glob, os
out, header = sys.argv[1], sys.argv[2]
kinds = {'turn': 0, 'pointer': 1, 'needle': 2, 'lamp': 3, 'region': 4, 'bar': 5, 'arc': 6, 'ring': 7}
lines = ['#pragma once', '', '// Made by Tools/compressor_skins/tools/gen.py from the faces cut from the photos: do not edit.', '',
         'namespace riff::effectdesign::photo', '{', '    enum PartKind { turn, pointer, needle, lamp, region, bar, arc, ring };', '',
         '    struct Part { const char* name; const char* image; int kind; float cx, cy, R, angle; int x, y, w, h; int wx0, wy0, wx1, wy1; };',
         '    struct Face { const char* name; const char* panel; int width, height; };', '', '    inline const Face faces[] {']
faces = []
for f in ['plat', 'svca', 'sfet', 'cvca', 'vvca', 'vfet', 'vopto']:
    m = json.load(open(f'{out}/{f}.json')); faces.append((f, m))
    lines.append(f'        {{ "{f}", "{m["panel"]}", {m["size"][0]}, {m["size"][1]} }},')
lines += ['    };', '', '    inline const Part parts[] {']
for f, m in faces:
    for k, p in m['parts'].items():
        w = p.get('window') or [0, 0, 0, 0]
        lines.append(f'        {{ "{f}.{k}", "{p["image"]}", {kinds[p["kind"]]}, {float(p.get("cx", 0)):.2f}f, {float(p.get("cy", 0)):.2f}f, {float(p.get("R", 0)):.2f}f, {float(p.get("angle", 0)):.2f}f, '
                     f'{p.get("x", 0)}, {p.get("y", 0)}, {p.get("w", 0)}, {p.get("h", 0)}, {w[0]}, {w[1]}, {w[2]}, {w[3]} }},')
lines += ['    };', '}', '']
open(header, 'w').write('\n'.join(lines))
print(sum(len(m['parts']) for _, m in faces), 'parts')
