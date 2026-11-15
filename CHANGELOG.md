# Changelog

## 2.0.0-beta.2 - 2026-11-15

Previews you can keep, suggested preset names, and pictures that stay after downloading.

### Added

- **STORE** next to `PREVIEW` in the slot card keeps a previewed capture in your library, without downloading it again. The capture keeps playing and your knob settings stay
- Saving a preset stores every capture on the board that is still a preview, so the preset only points to files in your library
- **Preset names.** Saving a new board suggests a name from its amp, gain and effects; the wand in the name field offers more

### Changed

- A previewed capture is marked `PREVIEW` in the slot card's model row too, not only on its tile
- The `TONE3000` and `LIBRARY` tabs sit directly on the browser card, side by side at the same height

### Fixed

- Captures from TONE3000 keep their picture on the tile and in the slot card right after downloading, for every kind. A new bank used to show the plain NAM symbol until the library was read again
- Auditioning an amp with cab on a board that already has an amp (or the other way round) swaps that amp instead of adding a second one in front of it. The selected amp slot changes first, so with two lanes you choose which amp you're replacing

## 2.0.0-beta.1 - 2026-11-15

NAMplify 2 is rebuilt from the engine up. It now runs on macOS, Windows and Linux, as a standalone app, VST3 and (on macOS) AU, and its source is open under the AGPL-3.0.

### Added

- **Windows and Linux.** Installers for macOS (universal `.pkg`), Windows x64 (`_setup.exe`) and Linux x64 (`.tar.gz` with `install.sh`)
- **Plugins.** VST3 on all three systems, AU on macOS
- **A chain of twelve cells.** One lane, two lanes side by side, or a split with a common head and tail. NAM captures, IRs and effects go in any cell
- **NAM slots by role.** Pedal, amp, amp with cab, outboard and space, each with its own engine (`FULL` or `LITE`)
- **IR slots.** An amp without a cab brings one along
- **Effects.** Gate, compressor, drive, EQ, chorus, flanger, phaser, tremolo, delay and reverb
- **TONE3000 preview.** Click a capture and hear it in a `PREVIEW` slot until you keep it
- **Library index card.** Your downloads as banks and variants, next to the TONE3000 card, offline
- **Output modes.** `RAW`, calibrated or loudness-matched, so switching amps doesn't jump in volume
- **Tuner** always on in the corner, as an LED row or a strobe, and full size with one click
- **MIDI.** Footswitch and knob assignments, program change, delay and tremolo synced to MIDI clock or the host's tempo
- **Settings.** Global EQ, capture oversampling, and where the library lives

### Changed

- The four fixed slots of 0.5 (pedal, amp, cab, outboard) are now roles of free cells in the chain
- The board starts empty; add what you need from the `+` cells
- Presets save the whole board, including the chain layout

### Removed

- The daily update check. NAMplify now only talks to TONE3000
- Looper, drum machine and recorder are hidden for now: `TOOLS` is shown as coming soon and returns in a later beta

## 0.5.3 - 2026-08-05

macOS only.

- A2 engine: `FULL` or `LITE`, switchable in the settings
- Artwork cache, so browsing doesn't hit the API limit
- Minor tweaks
