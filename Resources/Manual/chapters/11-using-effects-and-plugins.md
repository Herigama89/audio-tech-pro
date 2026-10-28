# Using Effects and Plugins

Audio Tech Pro comes with 95 effects (all described in the **Plugins** tab), and it also runs your own **VST3**, **Audio Unit** (Mac) and **CLAP** plugins.

## Adding an effect

Click an empty **insert slot** on a mixer strip (or in the Inspector, or beside the track header with **View > Show Inserts in Edit Window**). The **plugin picker** opens:


- **Search** - type part of a name.
- **Favorites** - click the star beside any plugin to add it here.
- **Maker** - the built-in effects ("Audio Tech Pro"), then your plugins by company.
- **Category** - EQ, Dynamics, Delay, Reverb, Modulation, Distortion, Filter, Amps and Pedals, Pitch, Imaging, Utility, Specialized, Metering, Multi Effects, Legacy.
- **Format** - VST3, AU or CLAP.

Effects play top to bottom. A common order on a vocal: EQ (cleanup) > compressor > de-esser > EQ (tone) > send to reverb.

## The plugin window

Every effect opens in its own window, with a bar on top:

![The plugin window's bar](img/plugin-bar.jpg)

- **Pin** - the window stays above the main window while you work.
- **Bypass** - take the plugin out of the sound to compare (`Option/Alt`-click an insert slot does the same).
- **Print** - Print While Recording: the input is recorded through this plugin, so the recording keeps it.
- **< Preset >** - save the plugin's settings, load one, step through presets, or **Set as Default** so every new one starts that way.
- **Side Chain** - another track keys this plugin (its detector hears that track): a compressor ducking the bass under the kick, a gate opened by a drum, a vocoder's carrier.

**Right-click any knob** for its menu: Show Automation, Create Automation Point, Automation Mode, Set to Default, and Learn MIDI Controller. For third-party plugins, the bar has an **automation** key with every parameter, and a key for the last knob you touched.

Most built-in effects show **meters** (input, output and gain reduction) and an **analyzer** of the sound.

## Your own plugins

- **Plug-in Manager...** (in the View and Mix menus) - scans for VST3, Audio Unit and CLAP plugins. Do this once after installing new plugins.
- **Mix > Plugin Formats in Menus** - show all formats, or only VST3 or only AU, so each plugin appears once.
- Plugin **instruments** are chosen from the instrument key on an instrument track's header (or its mixer strip's input slot) > **Plugins**.

## Saving CPU

- **Mix > Suspend Plug-ins When Silent** - plugins on a silent track stop working until sound comes again.
- **Freeze** heavy tracks (Track > Freeze Track), or use a **bus** for reverbs instead of one per track.
- **View > Performance...** shows which tracks use the most.
