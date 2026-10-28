"""Builds the Nemix sound library from free (CC0 / CC-BY) sample libraries.

For each instrument: which recordings to use (from an SFZ, a drum kit's folders, or file names), keeping a few
velocity layers and round robins, then a job list for the converter (Ogg Vorbis, trimmed) and a key map.
Usage: build_library.py <git folder> <out folder> <job file>"""
import os, re, sys, subprocess, collections, math
sys.path.insert(0, os.path.dirname(__file__))
import sfz

G, OUT, JOBS = sys.argv[1], sys.argv[2], sys.argv[3]
NOTES = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}

def note_of(text):
    m = re.match(r'^([A-Ga-g])([#b]?)(-?\d)$', text)
    if not m: return None
    n = NOTES[m.group(1).upper()] + (1 if m.group(2) == '#' else -1 if m.group(2) == 'b' else 0)
    return (int(m.group(3)) + 1) * 12 + n

instruments = []   # (id, name, category, credit, regions [dict], options)
fetch = collections.defaultdict(set)   # repo -> paths

def thin_layers(regs, keep):
    """Keeps `keep` velocity layers (evenly, always the loudest), stretching them to cover 1..127."""
    layers = sorted(set((r['lovel'], r['hivel']) for r in regs))
    if len(layers) <= keep:
        chosen = layers
    else:
        idx = sorted(set(round(i * (len(layers) - 1) / (keep - 1)) for i in range(keep))) if keep > 1 else [len(layers) - 1]
        chosen = [layers[i] for i in idx]
    out = []
    for i, layer in enumerate(chosen):
        lo = 1 if i == 0 else chosen[i - 1][1] + 1
        hi = 127 if i == len(chosen) - 1 else layer[1]
        for r in regs:
            if (r['lovel'], r['hivel']) == layer:
                q = dict(r); q['lovel'], q['hivel'] = lo, hi
                out.append(q)
    return out

def thin_rr(regs, keep):
    out = []
    for r in regs:
        if r['seq_length'] > 1:
            if r['seq_position'] > keep: continue
            r = dict(r); r['seq_length'] = min(keep, r['seq_length'])
        out.append(r)
    # random round robins (lorand/hirand) become a sequence
    groups = collections.defaultdict(list)
    for r in out:
        groups[(r['lokey'], r['hikey'], r['lovel'], r['hivel'])].append(r)
    final = []
    for key, rs in groups.items():
        if len(rs) > 1 and all(r['seq_length'] == 1 for r in rs):
            rs = sorted(rs, key=lambda r: r['lorand'])[:keep]
            for i, r in enumerate(rs):
                r = dict(r); r['seq_position'] = i + 1; r['seq_length'] = len(rs); final.append(r)
        else:
            final.extend(rs)
    return final

def from_sfz(iid, name, category, credit, repo, path, layers=4, rr=2, max_seconds=8.0, keyswitch=None, ccs=None, gain=0.0, filt=None):
    regs = sfz.regions(sfz.Source(os.path.join(G, repo)), path, ccs, keyswitch)
    if filt: regs = [r for r in regs if filt(r)]
    regs = thin_rr(thin_layers(regs, layers), rr)
    for r in regs:
        r['repo'] = repo; r['inputs'] = [(r['sample'], 0.0)]
    instruments.append(dict(id=iid, name=name, category=category, credit=credit, regions=regs, max_seconds=max_seconds, gain=gain, one_shot=False))

def from_names(iid, name, category, credit, repo, folder, pattern, layers=3, rr=2, max_seconds=8.0, gain=0.0, vel_of=None, one_shot=False, transpose=0):
    """Instruments whose file names hold the note (and velocity layer, round robin)."""
    files = subprocess.run(['git', '-C', os.path.join(G, repo), 'ls-tree', '-r', '--name-only', 'HEAD', folder + '/'], capture_output=True, text=True).stdout.split('\n')
    rows = []
    for f in files:
        m = re.search(pattern, os.path.basename(f))
        if not m: continue
        d = m.groupdict()
        note = note_of(d['note']) if 'note' in d else None
        if note is None: continue
        v = vel_of(d.get('vel')) if vel_of else int(d.get('vel') or 1)
        rows.append((note + transpose, v, int(d.get('rr') or 1), f))
    if not rows:
        print('nothing for', iid); return
    vels = sorted(set(r[1] for r in rows))
    if len(vels) > layers:
        idx = sorted(set(round(i * (len(vels) - 1) / (layers - 1)) for i in range(layers))) if layers > 1 else [len(vels) - 1]
        vels = [vels[i] for i in idx]
    roots = sorted(set(r[0] for r in rows))
    regs = []
    for vi, v in enumerate(vels):
        lo = 1 if vi == 0 else round(vi * 127 / len(vels)) + 1
        hi = 127 if vi == len(vels) - 1 else round((vi + 1) * 127 / len(vels))
        for ri, root in enumerate(roots):
            klo = 0 if ri == 0 else (roots[ri - 1] + root) // 2 + 1
            khi = 127 if ri == len(roots) - 1 else (root + roots[ri + 1]) // 2
            cands = sorted([r for r in rows if r[0] == root and r[1] == v], key=lambda r: r[2])
            if not cands:   # (this layer lacks the note: take the nearest layer's)
                near = sorted([r for r in rows if r[0] == root], key=lambda r: abs(r[1] - v))
                if not near: continue
                cands = sorted([r for r in rows if r[0] == root and r[1] == near[0][1]], key=lambda r: r[2])
            cands = cands[:rr]
            for i, c in enumerate(cands):
                regs.append(dict(sample=c[3], lokey=klo, hikey=khi, root=root, lovel=lo, hivel=hi, tune=0.0, volume=0.0,
                                 seq_length=len(cands), seq_position=i + 1, offset=0, release=0.3, pan=0.0, group='', off_by='',
                                 repo=repo, inputs=[(c[3], 0.0)]))
    instruments.append(dict(id=iid, name=name, category=category, credit=credit, regions=regs, max_seconds=max_seconds, gain=gain, one_shot=one_shot))

def drum_kit(iid, name, credit, repo, pieces, layers=4, rr=2, gain=0.0):
    """pieces: (keys, [(folder, gainDb)...], file pattern with vl and rr, max seconds, choke group, choked by)."""
    regs = []
    for keys, mics, pattern, max_seconds, group, off_by, pgain in pieces:
        first = mics[0][0]
        files = subprocess.run(['git', '-C', os.path.join(G, repo), 'ls-tree', '--name-only', 'HEAD', first + '/'], capture_output=True, text=True).stdout.split('\n')
        rows = []
        for f in files:
            m = re.search(pattern, os.path.basename(f))
            if m: rows.append((int(m.group('vel')), int(m.group('rr')), os.path.basename(f)))
        vels = sorted(set(r[0] for r in rows))
        if not vels:
            print('no samples for', first); continue
        if len(vels) > layers:
            idx = sorted(set(round(i * (len(vels) - 1) / (layers - 1)) for i in range(layers)))
            vels = [vels[i] for i in idx]
        for vi, v in enumerate(vels):
            lo = 1 if vi == 0 else round(vi * 127 / len(vels)) + 1
            hi = 127 if vi == len(vels) - 1 else round((vi + 1) * 127 / len(vels))
            cands = sorted([r for r in rows if r[0] == v], key=lambda r: r[1])[:rr]
            for i, c in enumerate(cands):
                for key in keys:
                    regs.append(dict(sample=first + '/' + c[2], lokey=key, hikey=key, root=key, lovel=lo, hivel=hi, tune=0.0, volume=pgain,
                                     seq_length=len(cands), seq_position=i + 1, offset=0, release=0.1, pan=0.0, group=group, off_by=off_by,
                                     repo=repo, inputs=[(folder + '/' + c[2], g) for folder, g in mics], max_seconds=max_seconds))
    instruments.append(dict(id=iid, name=name, category='Drums', credit=credit, regions=regs, max_seconds=6.0, gain=gain, one_shot=True))

#==============================================================================
KAR = 'Karoryfer Samples (CC0)'

# Pianos and keys
from_sfz('grand-piano', 'Grand Piano', 'Piano', 'Salamander Grand Piano by Alexander Holm (CC-BY 3.0)', 'SalamanderGrandPiano', 'Salamander Grand Piano V3.sfz', layers=5, rr=1, max_seconds=9.0)
from_sfz('upright-piano', 'Upright Piano', 'Piano', 'Upright Piano KW, FreePats (CC0)', 'upright-piano-KW', 'UprightPianoKW-20220221.sfz', layers=2, rr=1, max_seconds=8.0)
from_sfz('rhodes', 'Electric Piano (Rhodes)', 'Keys', 'Rhodes Mark I by Jeff Learman (CC0)', 'Discord-SFZ-GM-Bank', 'Discord GM/Melodic/005-Electric Piano 1.sfz', layers=3, rr=1, max_seconds=7.0)
from_sfz('electric-grand', 'Electric Grand', 'Keys', 'Electric grand by Greg Sullivan (CC0)', 'Discord-SFZ-GM-Bank', 'Discord GM/Melodic/003-Electric Grand Piano.sfz', layers=2, rr=1, max_seconds=7.0)

# Basses
from_sfz('finger-bass', 'Finger Bass', 'Bass', 'Electric Bass YR, FreePats (CC0)', 'electric-bass-yr', 'FingerBassYR 20190930.sfz', layers=2, rr=1, max_seconds=5.0)
from_sfz('pick-bass', 'Pick Bass', 'Bass', 'Electric Bass YR, FreePats (CC0)', 'electric-bass-yr', 'PickedBassYR 20190930.sfz', layers=2, rr=1, max_seconds=5.0)
from_sfz('jazz-bass', 'Jazz Bass', 'Bass', 'Growlybass, ' + KAR, 'karoryfer.growlybass', 'growlybass_clean.sfz', layers=3, rr=1, max_seconds=5.0,
         filt=lambda r: 'noise' not in r['sample'] and 'scrape' not in r['sample'])
from_sfz('fashion-bass', 'Mirror Bass', 'Bass', 'Fashionbass, ' + KAR, 'karoryfer.fashionbass', 'fashionbass_clean.sfz', layers=3, rr=1, max_seconds=5.0,
         filt=lambda r: 'noise' not in r['sample'])
from_sfz('swag-bass', 'Studio Bass', 'Bass', 'Swagbass, ' + KAR, 'karoryfer.swagbass', 'swagbass_clean.sfz', layers=3, rr=1, max_seconds=5.0,
         filt=lambda r: 'noise' not in r['sample'])
from_sfz('killer-bass', 'Killer Finger Bass', 'Bass', 'CC0 finger bass from the Discord SFZ GM bank', 'Discord-SFZ-GM-Bank', 'Discord GM/Melodic/034-Electric Bass (finger).sfz', layers=3, rr=1, max_seconds=5.0)

# Guitars
from_sfz('nylon-guitar', 'Nylon Guitar', 'Guitar', 'Spanish Classical Guitar, FreePats (CC0)', 'spanish-classical-guitar', 'SpanishClassicalGuitar-20190618.sfz', layers=1, rr=1, max_seconds=6.0)
from_sfz('steel-guitar', 'Steel Acoustic Guitar', 'Guitar', 'Martin HD28 by Jeff Learman (CC0)', 'Discord-SFZ-GM-Bank', 'Discord GM/Melodic/026-Acoustic Guitar (steel).sfz', layers=1, rr=1, max_seconds=7.0)
from_sfz('clean-electric', 'Clean Electric Guitar', 'Guitar', 'FSBS Electric Guitar Clean, FreePats (CC0)', 'electric-guitar-FSBS-clean', 'EGuitarFSBS-clean bridge 20260807.sfz', layers=2, rr=1, max_seconds=6.0)
from_sfz('emily-guitar', 'Hollow Electric Guitar', 'Guitar', 'Emilyguitar, ' + KAR, 'karoryfer.emilyguitar', 'emily_clean.sfz', layers=3, rr=1, max_seconds=6.0,
         filt=lambda r: 'noise' not in r['sample'])

# Drum kits (General MIDI keys: 36 kick, 37 side stick, 38 snare, 40 rimshot, toms 41-50, hats 42/44/46, crash 49/57, ride 51/53)
R = 'Samples/'
drum_kit('rock-kit', 'Rock Kit', 'Big Rusty Drums, ' + KAR, 'karoryfer.big-rusty-drums', [
    ([35, 36], [(R + 'kick_24/kick/kick', 0.0), (R + 'kick_24/kick/oh', -4.0)], r'_vl(?P<vel>\d+)_rr(?P<rr>\d+)', 2.5, '', '', 0.0),
    ([37], [(R + 'snare_14/sidestick/top', 0.0), (R + 'snare_14/sidestick/oh', -4.0)], r'_vl(?P<vel>\d+)_rr(?P<rr>\d+)', 1.5, '', '', 0.0),
    ([38], [(R + 'snare_14/center/top', 0.0), (R + 'snare_14/center/btm', -9.0), (R + 'snare_14/center/oh', -4.0)], r'_vl(?P<vel>\d+)_rr(?P<rr>\d+)', 2.5, '', '', 0.0),
    ([40], [(R + 'snare_14/rimshot/top', 0.0), (R + 'snare_14/rimshot/oh', -4.0)], r'_vl(?P<vel>\d+)_rr(?P<rr>\d+)', 2.5, '', '', 0.0),
    ([41, 43], [(R + 'tom_22/center/cl', 0.0), (R + 'tom_22/center/oh', -4.0)], r'_vl(?P<vel>\d+)_rr(?P<rr>\d+)', 3.0, '', '', 0.0),
    ([45, 47], [(R + 'tom_18/center/cl', 0.0), (R + 'tom_18/center/oh', -4.0)], r'_vl(?P<vel>\d+)_rr(?P<rr>\d+)', 3.0, '', '', 0.0),
    ([48, 50], [(R + 'tom_14/center/cl', 0.0), (R + 'tom_14/center/oh', -4.0)], r'_vl(?P<vel>\d+)_rr(?P<rr>\d+)', 3.0, '', '', 0.0),
    ([42], [(R + 'hihat_14/tc/cl', -2.0), (R + 'hihat_14/tc/oh', -2.0)], r'_vl(?P<vel>\d+)_rr(?P<rr>\d+)', 1.2, '1', '1', 0.0),
    ([44], [(R + 'hihat_14/footspl/cl', -2.0), (R + 'hihat_14/footspl/oh', -2.0)], r'_vl(?P<vel>\d+)_rr(?P<rr>\d+)', 1.5, '1', '1', 0.0),
    ([46], [(R + 'hihat_14/open/cl', -2.0), (R + 'hihat_14/open/oh', -2.0)], r'_vl(?P<vel>\d+)_rr(?P<rr>\d+)', 4.0, '1', '1', 0.0),
    ([49, 57], [(R + 'crash_17/cr/cl', -3.0), (R + 'crash_17/cr/oh', -1.0)], r'_vl(?P<vel>\d+)_rr(?P<rr>\d+)', 5.0, '', '', 0.0),
    ([51, 59], [(R + 'ride_22/rd/cl', -2.0), (R + 'ride_22/rd/oh', -2.0)], r'_vl(?P<vel>\d+)_rr(?P<rr>\d+)', 4.0, '', '', 0.0),
    ([53], [(R + 'ride_22/bl/cl', -2.0), (R + 'ride_22/bl/oh', -2.0)], r'_vl(?P<vel>\d+)_rr(?P<rr>\d+)', 4.0, '', '', 0.0),
    ([52], [(R + 'china_18/cn/cl', -3.0), (R + 'china_18/cn/oh', -1.0)], r'_vl(?P<vel>\d+)_rr(?P<rr>\d+)', 4.0, '', '', 0.0),
], layers=4, rr=2)

# The orchestra (VSCO 2 Community Edition, CC0)
VS = 'Versilian Studios VSCO 2 CE (CC0)'
NOTE_RE = r'(?P<note>[A-G]#?-?\d)'
V = 'VSCO-2-CE'
P = NOTE_RE + r'_v(?P<vel>\d+)(?:_(?:rr|RR)?(?P<rr>\d+))?'
from_names('violins', 'Violin Section', 'Strings', VS, V, 'Strings/Violin Section/susVib', P, layers=2, rr=1)
from_names('violas', 'Viola Section', 'Strings', VS, V, 'Strings/Viola Section/susvib', P, layers=2, rr=1)
from_names('cellos', 'Cello Section', 'Strings', VS, V, 'Strings/Cello Section/susvib', P, layers=2, rr=1)
from_names('violin-pizz', 'Pizzicato Violins', 'Strings', VS, V, 'Strings/Violin Section/Pizz', P, layers=2, rr=2, max_seconds=2.5)
from_names('cello-pizz', 'Pizzicato Cellos', 'Strings', VS, V, 'Strings/Cello Section/pizzT', P, layers=2, rr=2, max_seconds=2.5)
from_names('upright-bass', 'Upright Bass (Jazz)', 'Bass', VS, V, 'Strings/Solo Contrabass', r'Pizz_' + P, layers=2, rr=2, max_seconds=4.0)
from_names('violin-tremolo', 'Tremolo Violins', 'Strings', VS, V, 'Strings/Violin Section/Trem', P, layers=2, rr=1)
from_names('trumpet', 'Trumpet', 'Brass', VS, V, 'Brass/Trumpet/sus', P, layers=2, rr=1)
from_names('horn', 'French Horn', 'Brass', VS, V, 'Brass/F Horn/sus', P, layers=2, rr=1)
from_names('trombone', 'Trombone', 'Brass', VS, V, 'Brass/Tenor Trombone/sus', P, layers=2, rr=1)
from_names('tuba', 'Tuba', 'Brass', VS, V, 'Brass/Tuba/sus', P, layers=2, rr=1)
from_names('trumpet-stac', 'Trumpet Staccato', 'Brass', VS, V, 'Brass/Trumpet/stac', P, layers=2, rr=2, max_seconds=2.0)
from_names('flute', 'Flute', 'Winds', VS, V, 'Woodwinds/Flute', P, layers=2, rr=1)
from_names('oboe', 'Oboe', 'Winds', VS, V, 'Woodwinds/Oboe/Sus', P, layers=2, rr=1)
from_names('clarinet', 'Clarinet', 'Winds', VS, V, 'Woodwinds/Clarinet/susLong', P, layers=2, rr=1)
from_names('bassoon', 'Bassoon', 'Winds', VS, V, 'Woodwinds/Bassoon', P, layers=2, rr=1)
from_names('glockenspiel', 'Glockenspiel', 'Mallets', VS, V, 'Percussion/Glock', r'_' + NOTE_RE + r'(?P<vel>)\.wav', layers=1, rr=1, max_seconds=4.0)
from_names('marimba', 'Marimba', 'Mallets', VS, V, 'Percussion/Marimba', r'_' + NOTE_RE + r'_(?P<vel>loud|med|soft)_(?P<rr>\d+)', layers=2, rr=1, max_seconds=3.0,
           vel_of=lambda v: {'soft': 1, 'med': 2, 'loud': 3}.get(v, 2))
from_names('harp', 'Harp', 'Strings', VS, V, 'Strings/Harp', NOTE_RE + r'_(?P<vel>mf|f|p)', layers=2, rr=1, max_seconds=5.0, vel_of=lambda v: {'p': 1, 'mf': 2, 'f': 3}.get(v, 2))
#from_names('timpani', 'Timpani', 'Percussion', VS, V, 'Percussion/Timpani', NOTE_RE + r'.*?v(?P<vel>\d+)(?:.*?rr(?P<rr>\d+))?', layers=3, rr=1, max_seconds=4.0)
#from_names('organ', 'Pipe Organ', 'Organ', VS, V, 'Keys/Organ/Loud', NOTE_RE + r'(?P<vel>)', layers=1, rr=1, max_seconds=6.0)

#==============================================================================
# Fetch what's used, write the maps and the job list
jobs = []
total = 0
for ins in instruments:
    folder = os.path.join(OUT, ins['id'])
    os.makedirs(folder, exist_ok=True)
    lines = ['#name=' + ins['name'], '#category=' + ins['category'], '#credit=' + ins['credit'], '#oneShot=' + ('1' if ins['one_shot'] else '0')]
    names = {}
    for r in ins['regions']:
        key = tuple(r['inputs'])
        if key not in names:
            names[key] = 's%03d.ogg' % len(names)
            for p, _ in r['inputs']:
                fetch[r['repo']].add(p)
            inputs = '+'.join(os.path.join(G, r['repo'], p) + '@' + str(g) for p, g in r['inputs'])
            jobs.append('|'.join([os.path.join(folder, names[key]), str(r.get('max_seconds', ins['max_seconds'])), str(r.get('offset', 0)), str(ins['gain']), inputs]))
        lines.append('|'.join(str(x) for x in [names[key], r['lokey'], r['hikey'], r['root'], r['lovel'], r['hivel'], round(r['tune'], 2),
                                                round(r['volume'], 2), r['seq_position'], r['seq_length'], round(r['release'], 3),
                                                r['group'], r['off_by'], round(r['pan'], 1)]))
    open(os.path.join(folder, 'map.txt'), 'w').write('\n'.join(lines) + '\n')
    total += len(names)
    print('%-18s %-26s %4d regions %4d samples' % (ins['id'], ins['name'], len(ins['regions']), len(names)))
print('samples:', total)
# the instruments in their fixed order (presets name them by place: only ever add at the end), and the credits
open(os.path.join(OUT, 'instruments.txt'), 'w').write('# Nemix Library 1\n' + ''.join('%s|%s|%s\n' % (i['id'], i['name'], i['category']) for i in instruments))
credits = sorted(set(i['credit'] for i in instruments))
open(os.path.join(OUT, 'CREDITS.txt'), 'w').write(
    'Nemix Library: recorded instruments from free sample libraries, converted for Nemix.\n\n'
    'Every recording is in the public domain (CC0) except the Salamander Grand Piano, which is\n'
    'Creative Commons Attribution 3.0 (CC-BY 3.0): Alexander Holm.\n\nSources:\n' + ''.join('  - %s\n' % c for c in credits))
open(JOBS, 'w').write('\n'.join(jobs) + '\n')
# the files to fetch, per repo
for repo, paths in fetch.items():
    open(os.path.join(os.path.dirname(JOBS), 'fetch-' + repo + '.txt'), 'w').write('\n'.join(sorted(paths)) + '\n')
