"""Builds the manual: Resources/Manual/ (the chapters as Markdown, index.txt, img/) and the PDF.

    python3 build.py <facts.json from `Snapshots docsdump`> [--pdf]

The hand-written chapters are in chapters/ (a 'tab:' and a 'title:' line, '---', then Markdown); the reference
chapters (every effect, the Nemix sounds, the recorded instruments, every key command) are made here from the app's
own facts. The pictures come from images.py. The Markdown keeps to what the app's manual window draws: # ## ###
headings, paragraphs, '- ' and '1. ' lists (two spaces in for a sub-list), '> ' tips, ``` blocks, **bold**, *italic*,
`keys` and ![caption](img/picture.jpg) on a line of their own."""
import json, os, re, sys, shutil, html

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.abspath(os.path.join(here, '..', '..'))
out = os.path.join(root, 'Resources', 'Manual')
facts = json.load(open(sys.argv[1]))
effects_text = json.load(open(os.path.join(here, 'effects.json')))

TABS = ['Start', 'Record & Edit', 'Mix', 'Instruments', 'Plugins', 'More']
chapters = []   # (tab, slug, title, markdown)


def slug(s):
    return re.sub(r'[^a-z0-9]+', '-', s.lower()).strip('-')


def add(tab, title, md):
    chapters.append((tab, slug(title), title, md.strip() + '\n'))


# ---------------------------------------------------------------------------------------------------------------
# Hand-written chapters
for f in sorted(os.listdir(os.path.join(here, 'chapters'))):
    if not f.endswith('.md'):
        continue
    text = open(os.path.join(here, 'chapters', f), encoding='utf-8').read()
    head, body = text.split('\n---\n', 1)
    meta = dict(l.split(': ', 1) for l in head.splitlines() if ': ' in l)
    add(meta['tab'], meta['title'], body)

# ---------------------------------------------------------------------------------------------------------------
# Plugins: one chapter per category
CATEGORY_ORDER = ['EQ', 'Dynamics', 'Delay', 'Reverb', 'Modulation', 'Distortion', 'Filter', 'Amps and Pedals', 'Pitch',
                  'Imaging', 'Multi Effects', 'Specialized', 'Utility', 'Metering', 'Legacy']
CATEGORY_INTRO = {
    'EQ': "EQ (equalization) turns frequency ranges up or down: less mud, more air, a clearer vocal. **How to use it:** cut narrow bands to remove problems, boost wide bands gently to add character. Cut lows below what an instrument needs with a low cut (80-100 Hz on vocals and guitars). Use the analyzer to see where the energy is, but decide with your ears.",
    'Dynamics': "Dynamics effects control the difference between loud and quiet. **How to use them:** a compressor evens a vocal or bass so it sits steady - lower the threshold until the gain reduction meter shows 3-6 dB on the loud parts, then raise the make-up gain. Slow attack keeps the punch; fast attack tames peaks. A gate cuts noise between notes; a de-esser tames harsh S sounds; a limiter stops peaks going over a ceiling (last on the master).",
    'Delay': "Delays repeat the sound: echoes, slapback, ping-pong, rhythmic patterns. **How to use them:** put a delay on an aux bus and send to it; set the time to a note value (1/4, 1/8 dotted) so the echoes follow the tempo; use low and high cut on the echoes so they don't clutter the mix.",
    'Reverb': "Reverb puts the sound in a space - a room, a hall, a plate. **How to use it:** put one or two reverbs on aux buses and send tracks to them. Short rooms add life without distance; long halls and plates make big, smooth tails. Predelay (20-60 ms) keeps a vocal clear in front of its reverb; cut the reverb's lows to avoid mud.",
    'Modulation': "Modulation effects move the sound in time: chorus and ensemble thicken, flanger and phaser sweep, tremolo and auto pan pulse, the rotor cabinet spins like an organ speaker. **How to use them:** start with the rate slow and the mix low, then raise until it's an effect you hear on purpose.",
    'Distortion': "Distortion adds harmonics: warmth and weight at low drive, grit and aggression at high drive. **How to use it:** a saturator on a bass or 808 makes it heard on small speakers; a little on drums adds punch; use Mix to blend in the clean sound (parallel distortion).",
    'Filter': "Filters remove frequencies above or below a cutoff, often moving: sweeps, wahs, rhythmic gating. **How to use them:** automate the cutoff for builds and drops, or sync the movement to the song.",
    'Amps and Pedals': "Guitar and bass amps, cabinets, microphones and stomp boxes. **How to use them:** record your guitar or bass clean through a DI (the instrument input of your interface), then choose the amp and pedals afterwards - you can change the sound at any time.",
    'Pitch': "Pitch effects tune, shift and transform voices and instruments. **How to use them:** Pitch Correction on a lead vocal (set the song's key and a response fast for the modern hard-tuned sound, slow for natural); Vocal Transformer for deep or high character voices; Pitch Shifter for octaves and harmonies.",
    'Imaging': "Imaging effects place sound in the stereo field: wider, narrower, moved left or right. **How to use them:** widen pads, backing vocals and effects; keep bass, kick and lead vocal in the centre. Check the mix in mono.",
    'Multi Effects': "Multi-effects chain several effects, moved by step sequencers, LFOs and XY pads - for sound design, builds, glitches and performance.",
    'Specialized': "Effects for special jobs: vocoding, adding brightness, a sub octave, widening.",
    'Utility': "Tools for fixing and testing: level, polarity, mono, test tones.",
    'Metering': "Meters and analyzers that show what you hear: levels, loudness, stereo image, tuning, tempo - and the Mastering Assistant.",
    'Legacy': "Older designs kept for their sound and for older projects: simpler reverbs, compressors, gates and amps.",
}
TIPS = {
    'channelEq': "Start most mixes here: a low cut, then small moves. Click the analyzer to see the sound while you EQ.",
    'linearEq': "Use it on the master or on drum buses, where phase shifts between tracks would smear the sound.",
    'matchEq': "Load a reference song as the Template, play your mix to learn Current, then set Apply to 30-60% for a gentle match.",
    'studioComp': "Vintage Opto for smooth vocals, Vintage FET for aggressive drums and rap vocals, Classic VCA for punchy drums and buses. Use Mix at 50% for parallel compression.",
    'multiband': "On a mix or a bus: compress just the band that jumps out (boomy lows, harsh highs) and leave the rest alone.",
    'deesser': "Find the S frequency (5-8 kHz for most voices) with Filter Solo, then lower the threshold until the S sounds are calm but not lisped.",
    'noiseGate': "Set the threshold just above the noise between notes. On a kick or snare mic, it cuts the spill of the other drums.",
    'brickwall': "Last on the master: set the output to -1 dB and raise the gain until it's as loud as you want, listening for the punch going away.",
    'adaptiveLimiter': "A warmer, rounder limiter for the master: raise Gain, keep Out Ceiling at -1 dB.",
    'transient': "More attack makes drums snap; less sustain makes them tight and dry.",
    'stereoDelay': "A classic vocal throw: 1/4 on the left and 1/8 dotted on the right, feedback 20-30%, low and high cut on.",
    'tapeDelay': "Warm, wobbly echoes for reggae, dub and lo-fi. Raise feedback to over 100% briefly for a runaway dub echo.",
    'delayDesigner': "Draw taps for rhythmic delays, each with its own pan, pitch and filter - great for glitchy and spacious effects.",
    'studioReverb': "Choose a Room for drums, a Plate or Chamber for vocals, a Hall for strings and pads. Turn down the low decay for a cleaner mix.",
    'convSpace': "Real spaces from impulse responses: load your own IR (Load IR...) or shape one with the knobs.",
    'plateReverb': "The smooth, bright reverb of countless vocal and snare sounds.",
    'chorus': "On clean guitars, electric pianos and pads for width and shimmer.",
    'rotary': "On organs: switch between Slow and Fast during the song (automate it) for the classic Leslie sound.",
    'saturator': "On a bass or 808: Magnetic or Retro Tube, Bypass Below at 80 Hz so the sub stays clean, drive until it's heard on a phone speaker.",
    'bitcrusher': "Lo-fi and chiptune: fewer bits for grit, a lower rate for aliasing.",
    'guitarAmp': "Choose the amp, then the cabinet and microphone - moving the mic changes the tone as much as the amp's knobs.",
    'pitchCorrection': "Set the key and scale of the song. A fast response gives the hard-tuned effect of modern trap and reggaeton; a slower one sounds natural.",
    'vocalTransformer': "Pitch down with formants kept for a deep voice; robot mode for effects.",
    'vocoder': "Put it on a synth track, set the side chain to your vocal track, and play chords: the synth talks.",
    'masteringAssistant': "Put it last on the master. Play the loudest part after Reanalyze; then choose a character and adjust Loudness by ear.",
    'loudnessMeter': "Play the whole song and read I (integrated): -14 LUFS for streaming.",
    'multiMeter': "Check the tonal balance and stereo image of your mix against a reference song.",
    'tuner': "Put it on a guitar or bass track and tune before recording.",
    'gain': "Mono on the master checks how the mix sounds on a phone speaker; Phase invert fixes two mics cancelling each other.",
    'subBass': "Adds weight under a thin bass line or kick; keep it subtle and check on headphones.",
    'stepFx': "On a pad or a loop for rhythmic gating, or automate the XY pad for builds.",
    'phatFx': "Makes a bass, drum loop or synth bigger, dirtier and more aggressive.",
    'remixFx': "Perform it live on the master or a bus: gater, filter, tape stop and scratch for transitions.",
    'beatBreaker': "Chop and rearrange a drum loop live: choose a pattern and edit slices.",
    'pedalboard': "Stack a compressor, overdrive, chorus and delay like a guitarist's pedal board.",
    'autoFilter': "Sync the LFO to 1/4 or 1/8 for a rhythmic filter; automate the cutoff for a build.",
    'stereoSpread': "On pads and backing vocals; keep it off bass and kick.",
    'exciter': "A little on a dull vocal or overheads adds air and presence.",
}


def control_text(p):
    if p['choices']:
        c = p['choices']
        return ', '.join(c[:10]) + (', ...' if len(c) > 10 else '')
    lo, hi, unit = p['min'], p['max'], p['unit']
    if lo == hi:
        return ''
    def n(v):
        return ('%d' % v) if abs(v - round(v)) < 1e-6 else ('%.2f' % v).rstrip('0').rstrip('.')
    return '%s to %s%s' % (n(lo), n(hi), (' ' + unit) if unit else '')


GROUP = re.compile(r'^(Tap|Slice|Band|Step \d+|Template|Current|A|B) ([A-Z]|[\d.]+k?)(?: (.*))?$')


def controls(params):
    """The controls as short lines; repeated ones (taps, slices, bands, steps) gathered into one line each."""
    plain, groups, order = [], {}, []
    for p in params:
        m = GROUP.match(p['name']) if len(params) > 40 else None
        if m:
            g = re.sub(r' \d+$', '', m.group(1))
            rest = m.group(3) or ('On/Off' if p['choices'] else 'Level')
            if g not in groups:
                groups[g] = {'members': [], 'names': []}
                order.append(g)
            if m.group(2) not in groups[g]['members']:
                groups[g]['members'].append(m.group(2))
            if rest not in groups[g]['names']:
                groups[g]['names'].append(rest)
        else:
            t = control_text(p)
            plain.append('**%s**%s' % (p['name'], (' (%s)' % t) if t else ''))
    lines = []
    if plain:
        lines.append(' · '.join(plain))
    names = {'Tap': 'Each tap', 'Slice': 'Each slice', 'Band': 'Each band', 'Step': 'Each step', 'Template': 'The template curve',
             'Current': 'The current curve', 'A': 'Bank A', 'B': 'Bank B'}
    for g in order:
        mem = groups[g]['members']
        lines.append('%s (%s, %s to %s): %s' % (names.get(g, g), len(mem), mem[0], mem[-1], ', '.join(groups[g]['names'])))
    return lines


by_cat = {}
for e in facts['effects']:
    by_cat.setdefault(e['category'], []).append(e)
for cat in CATEGORY_ORDER:
    if cat not in by_cat:
        continue
    title = {'EQ': 'EQ Effects', 'Dynamics': 'Dynamics Effects', 'Delay': 'Delay Effects', 'Reverb': 'Reverb Effects',
             'Modulation': 'Modulation Effects', 'Distortion': 'Distortion Effects', 'Filter': 'Filter Effects',
             'Amps and Pedals': 'Amps and Pedals', 'Pitch': 'Pitch Effects', 'Imaging': 'Imaging Effects',
             'Multi Effects': 'Multi Effects', 'Specialized': 'Specialized Effects', 'Utility': 'Utility Effects',
             'Metering': 'Meters and Mastering', 'Legacy': 'Legacy Effects'}[cat]
    md = ['# ' + title, '', CATEGORY_INTRO[cat], '', 'In this chapter: ' + ', '.join('**%s**' % (e['name'] + (' (simple)' if e['type'] == 'limiter' else '')) for e in by_cat[cat]) + '.', '']
    for e in by_cat[cat]:
        md += ['## ' + (e['name'] + ' (simple)' if e['type'] in ('limiter',) else e['name']), '']
        if os.path.exists(os.path.join(out, 'img', 'fx-%s.jpg' % e['type'])):
            md += ['![%s](img/fx-%s.jpg)' % (e['name'], e['type']), '']
        md += [effects_text.get(e['type'], ''), '']
        if e['type'] in TIPS:
            md += ['> **How to use it:** ' + TIPS[e['type']], '']
        if e['params']:
            md += ['### Controls', ''] + [l + '\n' for l in controls(e['params'])]
    add('Plugins', title, '\n'.join(md))

# ---------------------------------------------------------------------------------------------------------------
# Nemix's sounds and recorded instruments
presets = facts['nemixPresets']
cats = []
for p in presets:
    if p['category'] not in cats:
        cats.append(p['category'])
md = ['# Nemix Sounds', '', "Every sound that comes with Nemix, by type, with the styles it's made for. Sounds marked **(recorded)** play the recorded instruments. Find them in Nemix's **BROWSE** page by type, style or name.", '']
for c in cats:
    md += ['## ' + c, '']
    for p in presets:
        if p['category'] == c:
            md.append('- **%s**%s - %s' % (p['name'], ' (recorded)' if p['recorded'] else '', p['genres']))
    md.append('')
add('Instruments', 'Nemix Sounds', '\n'.join(md))

lib = os.path.join(root, 'Resources', 'NemixLibrary')
md = ['# Recorded Instruments', '', "The recorded instruments of the **Nemix Library**, played by Nemix's SAMPLE layer. Each note was recorded by real players on real instruments, at several strengths, and many with alternating takes (round robins).", '']
groups = {}
for line in open(os.path.join(lib, 'instruments.txt'), encoding='utf-8'):
    line = line.strip()
    if not line or line.startswith('#'):
        continue
    iid, name, cat = line.split('|')
    credit = ''
    mp = os.path.join(lib, iid, 'map.txt')
    if os.path.exists(mp):
        for l in open(mp, encoding='utf-8'):
            if l.startswith('#credit='):
                credit = l.split('=', 1)[1].strip()
    groups.setdefault(cat, []).append((name, credit))
for cat, items in groups.items():
    md += ['## ' + cat, '']
    md += ['- **%s**%s' % (n, (' - ' + c) if c else '') for n, c in items]
    md.append('')
md += ['## Credits', '', 'The recordings are free to use. The Salamander Grand Piano is by Alexander Holm (Creative Commons Attribution 3.0); the others are in the public domain (CC0), from Versilian Studios (VSCO 2 CE), Karoryfer Samples, FreePats and other free libraries. The full list is in the Nemix Library folder (CREDITS.txt).', '']
add('Instruments', 'Recorded Instruments', '\n'.join(md))

# ---------------------------------------------------------------------------------------------------------------
# Key commands and the piano roll's keys
def pretty_key(k):
    k = k.replace('CURSOR ', '').replace('SPACEBAR', 'Space').replace('BACKSPACE', 'Backspace').replace('DELETE', 'Delete').replace('RETURN', 'Return')
    k = re.sub(r'\b(LEFT|RIGHT|UP|DOWN)\b', lambda m: m.group(1).title(), k)
    return k


menus = []
for c in facts['commands']:
    if c['menu'] not in menus:
        menus.append(c['menu'])
md = ['# Key Commands', '', "Every command, by menu, with its keys. `Cmd/Ctrl` is Cmd on a Mac and Ctrl on Windows; `Option/Alt` is Option on a Mac and Alt on Windows. Change any of them in **Key Commands...** (`Option/Alt+K`), or choose the keys of another program there.", '']
for m in ['File', 'Edit', 'Track', 'Transport', 'Mix', 'View', 'Help']:
    if m not in menus:
        continue
    md += ['## ' + m, '']
    for c in facts['commands']:
        if c['menu'] == m:
            keys = ', '.join('`%s`' % pretty_key(k) for k in c['keys'])
            md.append('- **%s**%s - %s' % (c['name'], (' ' + keys) if keys else '', c['description']))
    md.append('')
add('More', 'Key Commands', '\n'.join(md))

prkeys = open(os.path.join(here, 'pianoroll-keys.txt'), encoding='utf-8').read().splitlines()
md = ['# Piano Roll Keys', '', 'The keys of the piano roll (press `?` in the piano roll to see them there too).', '']
for l in prkeys:
    l = l.rstrip()
    if not l:
        md.append('')
    elif l.isupper() or l.endswith(')') and l.split(' (')[0].isupper():
        md += ['## ' + l.title().replace('(On', '(on').replace(', Or', ', or'), '']
    else:
        for part in re.split(r'\s{3,}', l.strip()):
            md.append('- ' + part.replace('ctrl + ', 'Cmd/Ctrl+').replace('alt + ', 'Option/Alt+').replace('shift + ', 'Shift+'))
add('More', 'Piano Roll Keys', '\n'.join(md))

# ---------------------------------------------------------------------------------------------------------------
# Write it out: chapters in tab order
chapters.sort(key=lambda c: TABS.index(c[0]))
cdir = os.path.join(out, 'chapters')
shutil.rmtree(cdir, ignore_errors=True)
os.makedirs(cdir)
with open(os.path.join(out, 'index.txt'), 'w', encoding='utf-8') as idx:
    idx.write('# Audio Tech Pro manual: tab|file|title (in order)\n')
    for i, (tab, s, title, md) in enumerate(chapters):
        name = '%02d-%s.md' % (i + 1, s)
        open(os.path.join(cdir, name), 'w', encoding='utf-8').write(md)
        idx.write('%s|%s|%s\n' % (tab, name, title))

# Check every picture a chapter shows is there
missing = [m for _, _, _, md in chapters for m in re.findall(r'\]\((img/[^)]+)\)', md) if not os.path.exists(os.path.join(out, m))]
if missing:
    sys.exit('Missing pictures: %s' % missing)
print('%d chapters' % len(chapters))

# ---------------------------------------------------------------------------------------------------------------
# The PDF: one HTML page, printed by Chromium
if '--pdf' in sys.argv:
    import markdown
    parts = []
    toc = []
    for i, (tab, s, title, md) in enumerate(chapters):
        if i == 0 or chapters[i - 1][0] != tab:
            toc.append('<li class="tab">%s</li>' % html.escape(tab))
            parts.append('<section class="tabpage"><div class="tabname">%s</div></section>' % html.escape(tab))
        toc.append('<li><a href="#%s">%s</a></li>' % (s, html.escape(title)))
        body = markdown.markdown(md, extensions=['fenced_code', 'sane_lists'])
        body = body.replace('<h1>', '<h1 id="%s">' % s, 1)
        parts.append('<section class="chapter">%s</section>' % body)
    page = '''<!doctype html><html><head><meta charset="utf-8"><title>Audio Tech Pro Manual</title><style>
@page { size: A4; margin: 16mm 14mm 18mm 14mm; }
body { font-family: "DejaVu Sans", Helvetica, Arial, sans-serif; font-size: 10.5pt; line-height: 1.45; color: #1d1f24; }
.cover { height: 250mm; display: flex; flex-direction: column; justify-content: center; page-break-after: always; }
.cover h1 { font-size: 40pt; margin: 0; color: #111; } .cover p { font-size: 15pt; color: #555; }
.cover img { width: 100%%; border-radius: 6px; margin-top: 12mm; }
.toc { page-break-after: always; columns: 2; } .toc li { list-style: none; margin: 1mm 0; } .toc li.tab { font-weight: bold; margin-top: 4mm; color: #e0701a; }
.toc a { color: #1d1f24; text-decoration: none; }
.tabpage { page-break-before: always; height: 240mm; display: flex; align-items: center; justify-content: center; }
.tabname { font-size: 36pt; font-weight: bold; color: #e0701a; }
.chapter { page-break-before: always; }
h1 { font-size: 22pt; border-bottom: 3px solid #e0701a; padding-bottom: 2mm; }
h2 { font-size: 14pt; margin-top: 7mm; color: #222; page-break-after: avoid; } h3 { font-size: 11.5pt; page-break-after: avoid; }
img { max-width: 100%%; max-height: 200mm; display: block; margin: 3mm auto; border: 1px solid #ccc; page-break-inside: avoid; }
code { background: #f0f0f2; padding: 0 3px; border-radius: 3px; font-size: 9.5pt; }
pre { background: #f0f0f2; padding: 3mm; white-space: pre-wrap; word-break: break-all; } pre code { background: none; }
blockquote { margin: 3mm 0; padding: 2mm 4mm; background: #fff4e8; border-left: 4px solid #e0701a; }
li { margin: 0.6mm 0; }
</style></head><body>
<div class="cover"><h1>Audio Tech Pro</h1><p>The Manual - the DAW and its plugins, and how to use them</p><img src="img/main-window.jpg"></div>
<div class="toc"><h1>Contents</h1><ul>%s</ul></div>
%s</body></html>''' % ('\n'.join(toc), '\n'.join(parts))
    htmlfile = os.path.join(out, 'manual.html')
    open(htmlfile, 'w', encoding='utf-8').write(page)
    from playwright.sync_api import sync_playwright
    with sync_playwright() as pw:
        b = pw.chromium.launch()
        pg = b.new_page()
        pg.goto('file://' + htmlfile)
        pg.wait_for_load_state('networkidle')
        pg.pdf(path=os.path.join(out, 'Audio Tech Pro Manual.pdf'), format='A4', print_background=True, display_header_footer=True,
               header_template='<div></div>',
               footer_template='<div style="font-size:8px;width:100%;text-align:center;color:#888;">Audio Tech Pro Manual - <span class="pageNumber"></span></div>',
               margin={'top': '16mm', 'bottom': '18mm', 'left': '14mm', 'right': '14mm'})
        b.close()
    os.remove(htmlfile)
    print('PDF written')
