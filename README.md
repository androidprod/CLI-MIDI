# CLI-MIDI 🎹

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![C++20](https://img.shields.io/badge/C%2B%2B-20-orange.svg)
![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)

A blazing fast, lightweight, and interactive terminal-based MIDI player written in modern C++20.

CLI-MIDI transforms your command-line interface into a highly responsive, rhythm-game-style waterfall piano roll. Experience MIDI playback natively without heavy GUI frameworks, featuring high-precision timing, parallel event dispatching, and zero-latency audio output configurations.

## ✨ Features

- **Terminal Waterfall Piano Roll:** Watch your notes fall from the future down to the exact playback moment, entirely rendered in ANSI graphics natively in your terminal.
- **Rhythm Game Visuals:** A sleek "falling block" lookahead visualizer, automatically scaled by track tempo, running smoothly at 30 FPS.
- **High-Precision Timing:** Built with `std::chrono::high_resolution_clock` and hybrid-sleep approaches. Can easily handle heavy MIDI files with thousands of simultaneous notes without jitter. 
- **Zero-Dependency Native Audio:** Direct hardware output via built-in APIs (`WinMM` on Windows, `ALSA` on Linux, `CoreMIDI` on macOS).
- **Interactive Controls:** Seek backwards/forwards, pause, speed up/slow down tempo, and tweak volume in real time with quick keyboard shortcuts.
- **Auto-Scaling Layout:** Dynamically adapts the piano roll size and seamlessly clears artifacts when you resize the terminal window.

## 🚀 Installation

### Download Pre-built Binaries
You can download the latest standalone executables for **Windows**, **Linux (x64)**, and **Linux (ARM64)** from the [Releases](../../releases) page. No installation required!

### Build from Source
If you want to build `CLI-MIDI` yourself, you'll need **CMake 3.20+** and a **C++20 compatible compiler**.

```bash
# Clone the repository
git clone https://github.com/your-username/CLI-MIDI.git
cd CLI-MIDI

# Linux / macOS
./build.sh --release

# Windows (Run in PowerShell / Command Prompt)
.\build.bat
```

> **Note for Linux:** Ensure you have the `ALSA` development headers installed before compiling:
> `sudo apt install libasound2-dev`

## 🎮 Usage

Simply pass a `.mid` file to start playback:

```bash
# Standard playback
CLI-MIDI /path/to/song.mid

# Change starting tempo (1.5x) and volume (50%)
CLI-MIDI -t 1.5 -v 50 /path/to/song.mid

# List all available MIDI Hardware output devices
CLI-MIDI --list-devices

# Select a specific MIDI OUT device (e.g. Device index 1)
CLI-MIDI -o 1 /path/to/song.mid
```

### Keyboard Shortcuts (During Playback)

| Key | Action |
|:---|:---|
| `<Space>` | Play / Pause |
| `<` / `>` | Seek backward / forward (-5s / +5s) |
| `+` / `-` | Speed up / slow down tempo |
| `0` - `9` | Adjust volume (0% ~ 90%) |
| `Q` | Quit / Stop playback |

## 🛠️ Architecture
- **Parser**: Complete SMF Format 0/1/2 parser mapping tracks into a unified absolute timeline.
- **Sequencer**: A precise hybrid-sleep (busy-spin + OS sleep) dispatcher that processes chords with microsecond precision.
- **Terminal UI**: An atomic lock-free cross-thread architecture pushing state to a terminal-raw renderer achieving consistent 30FPS without blocking the I/O timeline.

## 📄 License
This project is licensed under the MIT License - see the `LICENSE` file for details.
