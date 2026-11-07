# The Compressor's photographed faces

One script per circuit cuts its photo (in `photos/`): the makers' names taken off, the moving parts (knob
pointers, whole knobs, meter needles, lamps, lit keys, bars, arcs) saved as pictures, the panel filled in under
them. Then `tools/gen.py` writes their places into `Source/ui/CompressorFaceData.h`:

    cd Tools/compressor_skins && mkdir -p out
    for f in platinum studiovca studiofet classicvca vintagevca vintagefet vintageopto; do python3 $f.py; done
    python3 tools/gen.py out ../../Source/ui/CompressorFaceData.h
    cp out/*.png out/*_panel.jpg ../../Resources/effects/

`addCompressorFaces` in `Source/ui/EffectDesigns.cpp` lays the controls on them.
