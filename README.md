# Impact

A **multiband transient shaper** in the tradition of SPL Transient Designer,
Native Instruments Transient Master and Bitwig's Transient Control — with the
Attack / Sustain controls of Slate Digital and Kilohearts' transient shapers,
split across three frequency bands.

Built with JUCE and delivered as **VST3** + **CLAP** (plus **AU** on macOS).

## What's inside

- **3 bands** (Low / Mid / High) each with its own **Attack** and **Sustain**
  control:
  - **Attack** −100 % … +100 % — soften or punch the transient.
  - **Sustain** −100 % … +100 % — shorten or lengthen the body.
- A global **Punch** control (a faster transient emphasis on top).
- A live **oscilloscope** showing the input waveform with the transient
  component highlighted, plus holographic particles that react to transient
  energy. Each band has its own level meter.

## Parameters

| Group  | Control        | Meaning |
|--------|----------------|---------|
| Low    | Attack / Sustain | Low band transient shaping |
| Mid    | Attack / Sustain | Mid band transient shaping |
| High   | Attack / Sustain | High band transient shaping |
| Global | Punch          | Fast transient emphasis |
| Global | Mix            | Dry / wet |
| Global | Output         | Master gain (soft-limited) |

## Requirements

- **Windows 10/11, x64**
- **macOS 10.15+** (Apple Silicon or Intel; universal 2 binary)
- Any VST3 / AU / CLAP host.

## Building

Everything is vendored in-tree (JUCE 8.0.8 + clap-juce-extensions + CLAP SDK).

- **Windows**: `build.bat` (CMake + Visual Studio 2022), then
  `installer\build_installer.bat` for the Inno Setup `.exe`.
- **macOS**: `./build_mac.sh` then `./make_pkg.sh` (Xcode CLT + CMake).

## Continuous integration

`.github/workflows/build-macos.yml` builds the macOS universal2 VST3 + AU + CLAP
bundles and packages a `.pkg` installer on every push to `main`, every tag, and
on demand.

## License

MIT — see the project's license terms. JUCE and clap-juce-extensions retain
their own licenses.
