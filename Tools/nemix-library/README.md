# The Nemix Library

The recorded instruments in `Resources/NemixLibrary`, made from free sample libraries
(public domain / CC0, and the Salamander Grand Piano under CC-BY 3.0; see `CREDITS.txt` there).

To make it again:
1. Clone the source libraries (sfzinstruments/*, freepats/*, sgossner/VSCO-2-CE) into one folder.
2. `python3 build_library.py <that folder> "<out>/Nemix Library" jobs.txt` - picks the layers, writes each key map,
   `instruments.txt` (the fixed order presets use: only ever add at the end) and the credits, and lists the
   recordings to fetch (`fetch-<repo>.txt`).
3. `Snapshots nmxconvert jobs.txt` - mixes, trims and writes the recordings as Ogg Vorbis.
4. Level each instrument (`#gain=` in its map.txt; the [library] test with NEMIX_LEVELS=1 prints the levels).
