# Piano Roll and MIDI

Double-click a MIDI region, or select it and press `P`, to open the **piano roll** under the tracks. `Cmd/Ctrl+4` opens it in a window of its own. Notes are bars: up and down is pitch (the keyboard on the left), left to right is time, and the length of a bar is how long the note plays.

![The piano roll](img/piano-roll.jpg)

1. **The two bars** - all the piano roll's controls (next picture).
2. **Keyboard** - click a key to hear the note.
3. **Notes** - drag a note to move it, drag its right end to make it longer or shorter.
4. **Velocity lane** - how hard each note is played (taller = louder). Drag a stalk up or down. Choose another lane in the bar: pitch bend, modulation and other controllers.

![The piano roll's bars, part by part](img/piano-roll-bars.jpg)

1. **View** - the region as a Piano Roll, as notation (**Score**) or as a grid of steps (**Step Editor**).
2. **Step In** - step input: play notes on your MIDI keyboard to enter them one grid step after another, starting at the playhead (a chord goes in together). **Rest** moves on without a note.
3. **Quantize** - snaps note starts to the grid (`Q`): the selected notes, or all of them.
4. **Quantize grid** - 1/4, 1/8, 1/16, triplets and more.
5. **Quantize strength** - how far notes move toward the grid (100% = exactly on it), and swing.
6. **Scale Q** - Scale Quantize: moves notes outside the song's key onto the nearest note of its scale.
7. **Transform** - MIDI Transform: humanize, fixed velocity, crescendo, legato, double or half speed, transpose, reverse.
8. **Lane** - what the lane under the notes shows and edits (velocity, controllers, pitch bend).
9. **Close** the piano roll.
10. **Draw** tool (`P`) - click to put in a note; it remembers the last note's length.
11. **Paint** tool (`B`) - drag to lay notes along the grid.
12. **Delete** tool (`D`) - click notes to delete them.
13. **Mute** tool - click notes to mute them.
14. **Slice** tool (`C`) - cut notes where you click.
15. **Select** tool (`E`) - drag a box round notes.
16. **Zoom** tool (`Z`) - click to zoom in; `Option/Alt`-click zooms out.
17. **Stamp** - Draw and Paint put in a whole chord (major, minor, 7th...) with the clicked note as its root.
18. **Snap** - where notes go when you draw, move and resize them (*Auto* follows the zoom). Hold `Cmd` while dragging for no snap.
19. **Scale** - highlights the rows of the song's key; the others are darker and the root is marked.
20. **Ghosts** - shows the other regions' notes faintly behind these, to write parts that fit.
21. **Tools** - the note tools menu (see below).

## The tool menu

Press `T` in the piano roll for a menu of every tool with its icon and key; the lit one is in use. Press `T` twice for Mute. Right-click (or right-drag) deletes notes whatever the tool.

![The piano roll's tool menu](img/piano-roll-tool-menu.jpg)

## Editing notes

- **Move** - drag. **Up/Down** arrows move a semitone (`Shift`: an octave), **Left/Right** a grid step.
- **Copy** - `Option/Alt`-drag, or `Cmd/Ctrl+C` / `Cmd/Ctrl+V` (pastes at the playhead). `Cmd/Ctrl+D` duplicates.
- **Select all** - `Cmd/Ctrl+A`; `Esc` deselects.
- **Zoom** - `Cmd`-scroll zooms in time; `Option`-scroll makes rows taller or shorter.

## Note tools

From the **Tools** menu, on the selected notes (or all of them). Each has a key:

- **Quick Legato** (`Cmd/Ctrl+L`) - each note lasts until the next.
- **Chop** (`Option/Alt+U`) - cuts notes into pieces at the quantize grid (rolls, trap hi-hats).
- **Glue** (`Cmd/Ctrl+G`) - joins notes of the same pitch.
- **Strum...** (`Option/Alt+S`) - spreads a chord's notes out like a guitar strum.
- **Arpeggiate...** (`Option/Alt+A`) - turns chords into arpeggios.
- **Flam** (`Option/Alt+F`) - a quick double hit.
- **Randomize...** (`Option/Alt+R`) - random notes in a range, or small random changes for a human feel.
- **Scale Levels...** (`Option/Alt+X`) - velocity up, down, flattened or ramped.
- **Limit...** (`Option/Alt+K`) - folds notes into a range of pitches.
- **Claw Machine...** (`Option/Alt+C`) - thins notes out (keeps every second, third...).
- **Articulate...** (`Option/Alt+L`) - note lengths: staccato to legato.
- **Mute / Unmute** (`Option/Alt+M`), **Flip Horizontally** (`Option/Alt+Y`, reverse) and **Flip Vertically** (`Shift+Option/Alt+Y`, invert).

Press `?` in the piano roll for a box with every piano roll key. The full list is also in *More > Piano Roll Keys*.

## Step Editor and Score

The **Step Editor** shows the region as a grid of steps - one row per drum sound or note - like a drum machine: click a step to put in a hit. The **Score** shows it as notation.

![The Step Editor](img/step-editor.jpg)

![The Score](img/score.jpg)

## Event List

**View > Event List** (`Cmd/Ctrl+Option/Alt+D`) lists the selected region's notes and controllers with their exact position, pitch, velocity and length. Type in a cell to change it.

## Quantize from the tracks area

Select MIDI regions and press `Q` to quantize them to 16th notes without opening the piano roll.
