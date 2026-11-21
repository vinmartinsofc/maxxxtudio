<p align="center">
  <img src="app/assets/icons/app/maxxx-tudio.png" alt="MaXXXtudio visual identity" width="240">
</p>

<h1 align="center">MaXXXtudio</h1>

<p align="center">
  A digital audio workstation for creating, organizing, processing, and mixing music.
</p>

MaXXXtudio is an open-source DAW for Windows and Linux, built with C++20 and JUCE.
The project is evolving and brings essential production tools, instruments, effects,
and music editing together in a custom interface.

## Implemented Features

- **Arrangement:** MIDI and audio tracks, clips, navigation, zoom, grids, locators, loop, and transport.
- **Piano Roll:** create and edit notes, velocity, quantization, scales, fold, grooves, and selection tools.
- **Audio Editing:** split and merge clips, trim edges, fade-in and fade-out curves, transient analysis, Warp, source BPM, ×2, and ÷2.
- **Selection:** click, Shift/Ctrl time-range selection, multi-track selection, and selection operations.
- **Mixer:** volume, pan, mute, solo, peak metering, and Master channel.
- **Automation:** global envelopes for volume, pan, mute, and plugin parameters; automatic parameter selection when adjusting a control.
- **VST3 Plugins:** vendor scanning, favorites, custom folders, isolated execution, and project-saved state.
- **Browsers:** audio files, color-coded favorites, search, preview, history, and File Explorer location.
- **Drum Rack:** 16 pads, sample loading, One-Shot and Classic modes, pitch, volume, start, end, reverse, loop, and per-pad effects.
- **MIDI Learn:** map MIDI controls to volume, pan, mute, and solo.
- **Computer MIDI Keyboard:** play from the computer keyboard with octave and velocity controls.
- **Projects:** save and open `.std` files, undo/redo history, recovery, and Master WAV export.
- **Themes:** graphite/black with orange, blue, or red; white with orange; Matrix 1 and Matrix 2 with green.
- **Interface:** accessible tooltips, vector icons, active and hover states, and theme-aware readable text.

## Native Plugins

<table>
  <tr>
    <td align="center" width="50%">
      <img src="app/assets/readme/drum-rack.svg" alt="Drum Rack preview" width="300"><br>
      <strong>Drum Rack</strong><br>
      A 16-pad instrument for playing samples over MIDI. Includes audio loading, volume, pitch,
      start/end positions, reverse, loop, One-Shot mode, and independent effect chains per pad.
    </td>
    <td align="center" width="50%">
      <img src="app/assets/readme/volume-master.svg" alt="Master Volume preview" width="300"><br>
      <strong>Volume Master</strong><br>
      A utility for gain, stereo width, balance, mono/left/right/swap input, phase inversion,
      bass mono, mute, DC filtering, and configurable auto-pan.
    </td>
  </tr>
</table>

## Build

Requirements: CMake 3.22+ and a C++20 compiler.

**Windows** — Visual Studio with the “Desktop development with C++” workload:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

Executable: `build\app\MaXXXtudio_artefacts\Release\MaXXXtudio.exe`

**Linux (Ubuntu/Debian):**

```sh
sudo apt install build-essential cmake ninja-build pkg-config libasound2-dev libfreetype-dev \
  libfontconfig1-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libgl1-mesa-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
```

Run the tests with:

```sh
ctest --test-dir build -C Release --output-on-failure
```

In the plugin browser, **Folders** lets you add or remove VST3 search directories.
These paths are saved in the user settings.

## Project Structure

```text
core/    music model and sequencer
tests/   automated tests
app/     JUCE application, interface, audio, plugins, and production tools
```
