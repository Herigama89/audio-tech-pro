"""The manual's pictures: the app's screenshots (from Tools/Snapshots) with numbered call-outs, as JPEGs.
Usage: python3 images.py <snapshots dir> <Resources/Manual/img>. Writes keys.json too (each picture's numbered parts)."""
import json, os, sys
from PIL import Image
from annotate import annotate

shots, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
keys = {}

def R(label, x, y, w, h): return {'label': label, 'x': x, 'y': y, 'w': w, 'h': h}

def plain(src, name, crop=None, max_width=1400, q=84):
    im = Image.open(os.path.join(shots, src)).convert('RGB')
    if crop: im = im.crop(crop)
    if im.width > max_width: im = im.resize((max_width, int(im.height * max_width / im.width)), Image.LANCZOS)
    im.save(os.path.join(out, name), quality=q)

def called(src, name, parts, **kw):
    keys[name] = annotate(os.path.join(shots, src), parts, os.path.join(out, name), **kw)

# Main window: the parts the app reports
arr = json.load(open(os.path.join(shots, 'arrange.layout.json')))
called('arrange.png', 'main-window.jpg', arr)

# Mixer: its bar (numbers above it) and the kinds of strip
top = lambda p: dict(p, dot=[p['x'] + p['w'] / 2, 292])
mixer = [top(p) for p in [
    R('Which tracks to show', 70, 316, 106, 22), R('Narrow strips', 187, 316, 62, 22), R('EQ/Dyn: channel strip thumbnails', 256, 316, 62, 22),
    R('Bus compressor on the master', 326, 316, 76, 22), R('Automix', 410, 316, 66, 22), R('Comments under each strip', 484, 316, 80, 22),
    R('Zones (show or hide parts of the strips)', 571, 316, 62, 22), R('Console: the channel strip model', 1118, 316, 236, 22),
    R('Close the mixer', 1372, 316, 64, 22)]] + [
    R('A track channel strip (see the next picture)', 2, 343, 72, 552), R('Aux (bus) strip', 1289, 343, 74, 552), R('Master strip', 1367, 343, 72, 552)]
called('mixer.png', 'mixer.jpg', mixer, crop=(0, 312, 1440, 900), pad=(0, 40, 0, 0))

# One channel strip, part by part (numbers left and right of it)
L = lambda p: dict(p, dot=[-24, p['y'] + p['h'] / 2]); Rt = lambda p: dict(p, dot=[98, p['y'] + p['h'] / 2])
strip = [L(R('Track number and On/Off', 2, 343, 32, 18)), Rt(R('EQ thumbnail: click to open the channel strip', 36, 343, 34, 18)),
    Rt(R('Pan', 8, 365, 62, 30)), L(R('Mute and Solo', 6, 404, 64, 18)), Rt(R('Setting: load or save a patch', 4, 437, 66, 14)),
    L(R('Input (and mono/stereo)', 4, 454, 66, 14)), Rt(R('Inserts: click an empty slot to add an effect', 4, 470, 66, 100)),
    L(R('Sends to aux buses (knob = send level)', 4, 578, 66, 50)), Rt(R('Output', 4, 628, 66, 14)),
    L(R('Preamp (PRE) and its drive', 4, 647, 66, 16)), Rt(R('Tape and its drive', 4, 668, 66, 16)),
    L(R('Record, Input monitor, Automation mode', 4, 697, 66, 16)), L(R('Fader (Option-click: 0 dB)', 6, 722, 26, 120)),
    Rt(R('Level meter (peak hold)', 52, 716, 20, 128)), Rt(R('Fader level', 30, 852, 40, 14)), L(R('Track name and colour', 4, 872, 66, 22))]
called('mixer.png', 'channel-strip-parts.jpg', strip, crop=(0, 336, 76, 898), zoom=1.6, pad=(80, 0, 80, 0))

# Piano roll: the two bars (numbers above and below them), then the whole editor
pr = json.load(open(os.path.join(shots, 'pianoroll.layout.json'))) + [R('Close', 1372, 593, 64, 22)]
pr.sort(key=lambda p: (p['y'] > 610, p['x']))
pr = [dict(p, dot=[p['x'] + p['w'] / 2, 568 if p['y'] < 610 else 680]) for p in pr]
called('pianoroll.png', 'piano-roll-bars.jpg', pr, crop=(0, 590, 1440, 660), pad=(0, 40, 0, 40))
called('pianoroll.png', 'piano-roll.jpg', [R('The two bars (see the picture above)', 0, 592, 1440, 56), R('Keyboard: click to hear a note', 0, 656, 56, 152),
       R('Notes: drag to move, drag the end to lengthen', 400, 656, 900, 152), R('Velocity (or another lane: choose it in the bar)', 0, 812, 1440, 84)],
       crop=(0, 588, 1440, 900))

# Nemix: the parts placed by hand (the window's own layout)
nm = [
    R('Logo', 20, 4, 60, 82), R('Previous / next sound', 104, 18, 64, 28), R('INIT: a plain starting sound', 104, 52, 64, 22),
    R('Sound name, type and styles', 178, 14, 300, 50), R('SAVE your own sound', 510, 18, 66, 28), R('Pages', 592, 20, 454, 30),
    R('MASTER volume', 1126, 6, 66, 80), R('Oscillator A', 10, 100, 292, 352), R('Oscillator B', 310, 100, 292, 352),
    R('Oscillator C', 610, 100, 292, 352), R('Recorded instruments, sub and noise', 910, 100, 280, 352),
    R('Filter 1', 10, 460, 382, 198), R('Filter 2', 400, 460, 382, 198), R('Macros', 790, 460, 400, 198),
    R('Envelopes (drag a tab onto a knob)', 10, 666, 590, 236), R('LFOs (drag a tab onto a knob)', 608, 666, 582, 236),
    R('Other modulation sources', 858, 862, 324, 26),
]
called('nemix-synth/nemix-sound.png', 'nemix-sound.jpg', nm)
called('nemix-sample/nemix-sound.png', 'nemix-sample.jpg', [R('Recorded instrument on', 922, 142, 40, 24), R('Choose the instrument', 974, 142, 204, 24),
       R('Level', 926, 180, 72, 72), R('Pan', 1014, 180, 72, 72), R('Start', 1102, 180, 72, 72), R('Tune (semitones, cents)', 926, 272, 160, 66),
       R('Route: direct or through the filters', 1100, 306, 78, 24)], crop=(900, 90, 1200, 460), max_width=600)
for p in ['matrix', 'fx', 'arp', 'browse']:
    plain('nemix-synth/nemix-%s.png' % p, 'nemix-%s.jpg' % p, max_width=1100)

# Other windows as they are
for src, name, crop in [
    ('smart-controls.png', 'smart-controls.jpg', (0, 600, 1440, 920)), ('live-loops.png', 'live-loops.jpg', None),
    ('drummer.png', 'drummer.jpg', None), ('generator.png', 'generator.jpg', None), ('audio-editor.png', 'audio-editor.jpg', None),
    ('audio-editor-flex-time.png', 'flex-time.jpg', None), ('pitch-editor.png', 'pitch-editor.jpg', None),
    ('step-editor.png', 'step-editor.jpg', None), ('score.png', 'score.jpg', None), ('strip-window.png', 'channel-strip.jpg', None),
    ('mixer-console.png', 'mixer-console.jpg', (0, 312, 1440, 920)), ('automix.png', 'automix.jpg', None), ('multimeter.png', 'multimeter.jpg', None),
    ('assistant-panel.png', 'assistant.jpg', (0, 0, 760, 900)),
    ('theme-panel.png', 'theme.jpg', None), ('performance.png', 'performance.jpg', None), ('keycommands.png', 'key-commands.jpg', None),
    ('xenon-buscomp.png', 'bus-comp.jpg', None), ('screen-record.png', 'screen-record.jpg', None), ('audiosuite.png', 'audiosuite.jpg', None),
    ('regions-edit.png', 'regions.jpg', None), ('inspector-wide.png', 'inspector.jpg', None), ('insert-bar.png', 'plugin-bar.jpg', None),
    ('tool-icons.png', 'tools.jpg', None), ('pianoroll-toolmenu.png', 'piano-roll-tool-menu.jpg', None),
    ('xenon-arrange.png', 'look-xenon.jpg', None), ('white-arrange.png', 'look-white.jpg', None), ('flat-arrange.png', 'look-flat.jpg', None),
    ('multimeter-goniometer.png', 'goniometer.jpg', None), 
]:
    plain(src, name, crop)

# Every effect's window
fxdir = os.path.join(shots, 'fx')
for f in sorted(os.listdir(fxdir)):
    if f.endswith('.png'):
        plain(os.path.join('fx', f), f.replace('.png', '.jpg'), max_width=1000, q=80)

json.dump(keys, open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'keys.json'), 'w'), indent=1)
print('done', len(os.listdir(out)))
