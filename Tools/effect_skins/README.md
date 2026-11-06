# Photographed effect faces

The Graphic EQ, Tube EQ and Console EQ windows are photos of the hardware's front panels (in `photos/`), edited:
the maker's names and logos taken off, the parts that move cut out, and the panel filled in under them.

    cd Tools/effect_skins && mkdir -p out
    python3 graphic.py && python3 tube.py && python3 console.py

writes `out/` (the panels as PNG, the moving parts with their alpha) and the layouts (`*_layout.json`: each part's
pivot, size and the angle it points at in the photo). Copy them into `Resources/effects/` (the panels as JPEG):
the app compiles that folder in, and `addPhotoDesigns` in `Source/ui/EffectDesigns.cpp` places the parts with
those numbers, in the photo's pixels.

- Knobs whose shape and lighting don't change as they turn (the big black skirted knobs, grey knurled knobs,
  silver frequency rings) keep the photo: only their pointer line is cut out and turned.
- Knobs with a fin or bar (chicken heads, the red and blue knobs) turn whole.
- Fader caps slide along their slots; lamps, LEDs and lit buttons are drawn over the panel when on.

The channel strip window (`Source/ui/ConsoleStripWindow.cpp`) is the same idea: `python3 strip.py` cuts
`photos/strip.png` (cropped to the panel, doubled in size): every knob's pointer line (the cap filled in from
its own surface turned round), every lamp unlit in the panel and lit as its own picture, the power key lit and
unlit. The lamps' and knobs' places are in `strip_layout.json`.
