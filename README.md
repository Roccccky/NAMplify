# NAMplify

**NAM, made simple.**

![NAMplify — a virtual pedalboard with a drive pedal, an amp capture, a cab IR, EQ and reverb](assets/board-hero.webp)

A **free virtual pedalboard** for macOS, **powered by the [TONE3000](https://www.tone3000.com) API**.
Amps, pedals and IRs drop straight into the right stompbox — and you play.
No DAW, no plugin host, no routing, no import dialog.

## Get started

**[⬇ Download the latest release](https://github.com/Roccccky/NAMplify/releases/latest)** — **0.5.3** · macOS 13+ · Apple Silicon & Intel

1. Open the DMG and drag **NAMplify** into your **Applications** folder
2. First launch: **right-click the app → Open**, then **Open Anyway**
3. macOS warns that the app "can't be verified" — that only means it isn't enrolled in Apple's paid developer program. On newer macOS the button sits in **System Settings → Privacy & Security**. Needed once.
4. Allow **microphone access** so the app can hear your guitar
5. Pick your audio interface in the settings (⚙️), then **Connect TONE3000**, sign in free and pull down an amp

> You need an audio interface to get the guitar into the Mac (iRig, Focusrite Scarlett or similar).

## What's on the board

**Four slots take NAM captures.** `PEDAL` for overdrives, fuzzes and boosts in front of the
amp · `AMP` for amp captures and full rigs · `CAB` for IRs — load an amp without a cab and
NAMplify pairs one automatically · `OUTBOARD` for preamps and studio gear after the cab
(still experimental). Each has its own NAM engine.

**Around them, a normal board.** Compressor, drive (two voicings + tone), EQ, chorus, phaser,
tremolo, delay and reverb. Drag to reorder, right-click to remove, `+` to add.

**Tools.** Stompbox tuner that mutes the output, drum machine, four-track looper and a
recorder that writes WAVs.

**Top bar.** Input gain with meter, noise gate, stereo SPREAD and output level — always in reach.

**TONE3000, inside the app.** Browse, favourite and download captures without leaving
NAMplify. Downloads bundle into banks you flip through like channels; the dial on the pedal
steps through the variants. Save the whole rig as a preset and export it as a `.nampset`.

## Privacy

NAMplify talks to exactly two addresses: `tone3000.com` when you browse or download captures,
and (in the shipped 0.5.x builds) an update endpoint once a day. No analytics, no
telemetry, no crash reporting, no account with me, no background process.

## About this repository

This repo is the **download and the description — the source code is not published.**
NAMplify is free to download and use, but it is not an open-source project, and there is no
issue tracker here.

## Credits

NAMplify models nothing itself. The captures are the work of the NAM project and of the
people who make and share them, and they reach the app through the **TONE3000 API** — that
is what makes a library this size usable from inside a program at all. Without those two
there would be nothing to plug in. NAMplify is the board you play them on.

It is **free to download and use**. Built on:

- **NeuralAmpModelerCore** © Steven Atkinson — the amp-modeling engine (MIT)
- **[Eigen](https://eigen.tuxfamily.org)** — math library (MPL2)
- **nlohmann/json** — JSON library (MIT)

Their licence notices ship inside the app.

If it saves you time, you can [buy me a coffee](https://paypal.me/martinhentze/3EUR). It stays free either way.

NAMplify is an independent app. Not affiliated with, endorsed or certified by TONE3000
(Hot, Inc.), the Neural Amp Modeler project or Steven Atkinson. All captures are created
and shared by the TONE3000 community and are downloaded through the TONE3000 API under
that platform's terms.
