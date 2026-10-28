"""A small SFZ reader: #include, #define, headers with inheritance, opcodes.
Gives the playable regions (sample, key and velocity ranges, root, tune, volume, round robin...)
for the default state of the controllers and keyswitches."""
import os, re, subprocess

NOTE = {'c': 0, 'd': 2, 'e': 4, 'f': 5, 'g': 7, 'a': 9, 'b': 11}

def note_number(v):
    v = str(v).strip()
    try:
        return int(float(v))
    except ValueError:
        pass
    m = re.match(r'^([a-gA-G])([#b]?)(-?\d+)$', v)
    if not m:
        return None
    n = NOTE[m.group(1).lower()] + (1 if m.group(2) == '#' else -1 if m.group(2) == 'b' else 0)
    return (int(m.group(3)) + 1) * 12 + n


class Source:
    """Reads text from a git repo (lazily fetched) or a folder."""
    def __init__(self, repo):
        self.repo = repo

    def read(self, path):
        path = os.path.normpath(path).replace('\\', '/')
        r = subprocess.run(['git', '-C', self.repo, 'show', 'HEAD:' + path], capture_output=True)
        if r.returncode != 0:
            return None
        return r.content if False else r.stdout.decode('utf-8', 'replace')


def expand(src, path, defines, depth=0, root=None):
    root = os.path.dirname(path) if root is None else root
    text = src.read(path)
    if text is None:
        return ''
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    out = []
    base = root   # (includes are relative to the instrument file's folder, whoever includes them)
    for line in text.splitlines():
        line = re.sub(r'(^|\s)//.*$', '', line)
        # defines first (they may be used in includes)
        m = re.match(r'^\s*#define\s+(\$\w+)\s+(.*?)\s*$', line)
        if m:
            defines[m.group(1)] = m.group(2)
            continue
        for k in sorted(defines, key=len, reverse=True):
            line = line.replace(k, defines[k])
        # includes, possibly inline
        def inc(mm):
            if depth > 8:
                return ''
            p = os.path.join(base, mm.group(1))
            return ' ' + expand(src, p, defines, depth + 1, root) + ' '
        line = re.sub(r'#include\s+"([^"]+)"', inc, line)
        out.append(line)
    return '\n'.join(out)


TOKEN = re.compile(r'<(\w+)>|([A-Za-z0-9_$]+)=')

def parse(text):
    """Yields (header, opcodes) in order."""
    pos = 0
    items = []
    current = None
    matches = list(TOKEN.finditer(text))
    for i, m in enumerate(matches):
        end = matches[i + 1].start() if i + 1 < len(matches) else len(text)
        if m.group(1):
            current = (m.group(1), {})
            items.append(current)
        else:
            if current is None:
                current = ('global', {})
                items.append(current)
            value = text[m.end():end].strip()
            if m.group(2) != 'sample':
                value = value.split()[0] if value.split() else ''
            current[1][m.group(2)] = value
    return items


def regions(src, path, cc_defaults=None, keyswitch=None):
    defines = {}
    text = expand(src, path, defines)
    items = parse(text)
    control, glob, master, group = {}, {}, {}, {}
    out = []
    for header, ops in items:
        if header == 'control':
            control = dict(ops)
        elif header == 'global':
            glob = dict(ops); master = {}; group = {}
        elif header == 'master':
            master = dict(ops); group = {}
        elif header == 'group':
            group = dict(ops)
        elif header == 'region':
            r = {}
            for layer in (glob, master, group, ops):
                r.update(layer)
            r['_default_path'] = control.get('default_path', '')
            out.append(r)
    # controller defaults (set_ccN in <control>), overridden by what the caller wants
    ccs = {}
    for k, v in control.items():
        m = re.match(r'set_cc(\d+)', k)
        if m:
            try: ccs[int(m.group(1))] = float(v)
            except ValueError: pass
    if cc_defaults:
        ccs.update(cc_defaults)
    result = []
    for r in out:
        if r.get('trigger', 'attack') not in ('attack', 'first', 'legato'):
            continue
        ok = True
        for k, v in r.items():
            m = re.match(r'(lo|hi)cc(\d+)$', k)
            if m:
                val = ccs.get(int(m.group(2)), 0.0)
                lim = float(v)
                if (m.group(1) == 'lo' and val < lim) or (m.group(1) == 'hi' and val > lim):
                    ok = False
        if not ok:
            continue
        # keyswitches: keep the default (or the asked-for) one
        if 'sw_last' in r or 'sw_down' in r:
            want = keyswitch if keyswitch is not None else note_number(r.get('sw_default', control.get('sw_default', '-1')) or -1)
            have = note_number(r.get('sw_last', r.get('sw_down')))
            if want is not None and have != want:
                continue
        if 'sample' not in r or r['sample'].startswith('*'):
            continue
        key = note_number(r['key']) if 'key' in r else None
        lo = key if key is not None else note_number(r.get('lokey', 0))
        hi = key if key is not None else note_number(r.get('hikey', 127))
        root = note_number(r.get('pitch_keycenter', key if key is not None else lo))
        vol = float(r.get('volume', 0) or 0)
        amp = float(r.get('amplitude', 100) or 100)
        for k, v in r.items():
            m = re.match(r'amplitude_oncc(\d+)$', k)
            if m:
                amp *= (ccs.get(int(m.group(1)), 0.0) / 127.0) * (float(v) / 100.0) if float(v) else 1.0
            m = re.match(r'volume_oncc(\d+)$', k)
            if m:
                vol += ccs.get(int(m.group(1)), 0.0) / 127.0 * float(v)
        if amp <= 0.0:
            continue
        import math
        vol += 20 * math.log10(max(amp, 1e-4) / 100.0)
        sample = os.path.normpath(os.path.join(os.path.dirname(path), r['_default_path'], r['sample'].replace('\\', '/'))).replace('\\', '/')
        result.append(dict(
            sample=sample, lokey=lo, hikey=hi, root=root,
            lovel=int(float(r.get('lovel', 1))), hivel=int(float(r.get('hivel', 127))),
            tune=float(r.get('tune', 0) or 0) + 100 * float(r.get('transpose', 0) or 0),
            volume=vol, seq_length=int(float(r.get('seq_length', 1) or 1)), seq_position=int(float(r.get('seq_position', 1) or 1)),
            lorand=float(r.get('lorand', 0) or 0), hirand=float(r.get('hirand', 1) or 1),
            offset=int(float(r.get('offset', 0) or 0)),
            loop_mode=r.get('loop_mode', ''), loop_start=int(float(r.get('loop_start', r.get('loopstart', -1)) or -1)),
            loop_end=int(float(r.get('loop_end', r.get('loopend', -1)) or -1)),
            release=float(r.get('ampeg_release', 0.05) or 0.05),
            pan=float(r.get('pan', 0) or 0),
            group=r.get('group', ''), off_by=r.get('off_by', ''),
        ))
    return result
