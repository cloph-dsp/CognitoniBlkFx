# CognitoniBlkFx

 <img width="1260" height="729" alt="kuva" src="https://github.com/user-attachments/assets/0b0a2ded-25ee-47a8-b0cb-ce5fd2875346" />

CognitoniBlkFx is a spectral FX plugin built with JUCE, reimplementing the classic **DtBlkFx** by Darrell Tam.

## Downloads
- **TODO:** Once I get the current features adjusted correctly I'll create built versions for VST3

## Credits
- Original DtBlkFx: **Darrell Tam**
- Original public repo contributor: **skullzy**

## Effects

### AutoHarm
Automatically detects the fundamental pitch of your audio and shapes its harmonics with amplitude shaping. You can target specific harmonic types (all, odd, even, or in-between), restrict the frequency range, and set the effect intensity via the Card Mix slider.

### Contrast
Applies a nonlinear spectral curve that exaggerates differences between loud and quiet frequency components. Positive values intensify and sharpen harmonic peaks; negative values compress and smooth them.

### Saws
Shapes the harmonic content of your audio toward a sawtooth-wave harmonic profile. The value knob switches between two modes -- **Scale** (blends existing harmonics toward the sawtooth profile) and **Copy** (replaces harmonics directly from the fundamental). Uses the original DtBlkFx harmonic coefficient tables.

## Features

- Three spectral processing cards: **AutoHarm**, **Contrast**, **Saws** _(The plan is to make more features based on requests and make a layout for moving and adding cards. Currently they are static.)_
- Per-card bypass, frequency range (Freq A / Freq B), harmonic type selector, value, and mix controls
- Master Wet/Dry for global dry/processed blend
- Built-in presets including the classic DtBlkFx AutoHarm preset
- User presets saved to `%APPDATA%\CognitoniBlkFx\presets.json` (Windows)

## Build Instructions

### Prerequisites

- [JUCE](https://juce.com) and Projucer
- **Windows**: Visual Studio 2022
- **macOS**: Xcode
- **Linux**: GCC or Clang

### Windows

1. Open `CognitoniBlkFx.jucer` in Projucer.
2. Verify your JUCE module paths in Projucer global settings.
3. Save/export the project from Projucer to regenerate `Builds/` and `JuceLibraryCode/`.
4. Open `Builds/VisualStudio2022/CognitoniBlkFx.sln` in Visual Studio.
5. Build `Debug` or `Release` for `x64`.

### macOS / Linux

Follow the same Projucer -> IDE/Makefile -> build flow for your platform.

## Project Structure

- `CognitoniBlkFx.jucer` -- Projucer project file
- `Source/` -- Plugin source code
  - `SpectralEngine/` -- FFT overlap-add processing engine
  - `Cards/` -- AutoHarm, Contrast, and Saws card implementations

## License

GNU General Public License v3.0 or later. See `LICENSE`.
