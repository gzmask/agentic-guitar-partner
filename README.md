# Agentic Guitar Learning Partner

A collection of guitar-input, pitch-detection, and looping experiments built
around Jolt, Carp, Clojure, and Csound. The current focus is a hardware-tested,
guitar-controlled looper that records and replays the **actual stereo PCM signal**
from an FX100 or other CoreAudio input.

Pitch detection controls the application; it never replaces the recorded guitar
with MIDI or a synthesized approximation.

## Current PCM looper

The entry point is [`guitar-audio-looper.jolt`](guitar-audio-looper.jolt).
It provides:

- six equivalent stereo PCM loop slots;
- one active recorder, no overdub;
- octave-independent guitar commands based on C-major scale degrees;
- sample-accurate recording and playback boundaries;
- outer-silence trimming for the first/master loop;
- synchronized later loops with a selectable 1–6 master-cycle length and automatic finalization;
- queued cycle-boundary playback;
- spoken prompts mixed by Csound, not played directly by `say`;
- a fixed ANSI dashboard and a line-oriented `--no-tui` mode;
- foreground ownership, Ctrl+C cleanup, and a 600-second OS safety alarm.

### Quick start

Run commands from the repository root:

```bash
# Complete offline suite; does not open an audio device
jolt guitar-audio-looper.jolt --check

# Inspect current CoreAudio metadata; does not open a stream
jolt guitar-audio-looper.jolt --devices

# Offline dashboard preview
jolt guitar-audio-looper.jolt

# Live looper with the full TUI
jolt guitar-audio-looper.jolt --live
```

`--live` is the only mode that opens audio devices. It requires a foreground
terminal. Select the input with the keyboard, then choose a control profile:
press Enter/1 for C major with the default wake, 2 to capture a custom scale and
wake, or 3 to keep C major and capture a custom wake. Guitar capture opens the
selected input with monitoring off and is only available in `--live`. The finite
speech cache is generated before any audio stream opens, so startup can pause for
tens of seconds.

Use `--live --no-tui` only when line-oriented logs are desired. That option
intentionally disables the fixed dashboard.

### Default guitar controls

Notes may be played in **any supported octave**. The note names below identify
scale degrees, not required registers.

Wake phrase:

```text
C D C D
```

After the wake phrase, select a loop or exit:

| Degree | Note | Action |
| ---: | --- | --- |
| 1–6 | C, D, E, F, G, A | Select loop 1–6 |
| 7 | B | Exit immediately |

After selecting a loop, choose an action:

| Degree | Note | Action |
| ---: | --- | --- |
| 1 | C | Start recording |
| 2 | D | Stop recording |
| 3 | E | Start playback |
| 4 | F | Stop playback |
| 5 | G | Erase the selected stopped loop |
| 6–7 | A, B | Reserved |

For the first/master clip, record as before and stop it with degree 2. Once a
master exists, degree 1 asks for the later clip's length: play scale degree 1–6
(C, D, E, F, G, A in any octave) to record that many full master cycles. The
recorder starts on the next shared boundary and automatically finalizes at the
selected boundary. Degree 2 remains available as an early stop; it keeps only
already-completed master cycles. Choices that exceed the 120-second slot
capacity are disabled.

Erase is a direct command and is published only after the native engine
acknowledges it. The wake phrase followed by degree 7 starts the safe exit fade
immediately; there is no second-note confirmation or hold. Keyboard `q` uses the
same graceful shutdown. `[d]` toggles pitch debug and `[c]` reopens input
selection when all slots are empty and idle.

The first finalized clip establishes the master cycle. Its leading/trailing
silence is trimmed from bounded 10 ms stereo-RMS summaries at -50 dBFS, retaining
20 ms preroll and 50 ms release tail. Later clips preserve complete master cycles
so trimming cannot destroy synchronization.

### Audio safety

- Start with low volume and preferably headphones.
- Verify the displayed input and output before playing.
- Avoid simultaneous FX100 direct monitoring and software monitoring if it sounds
  doubled or phasey.
- Never route a microphone to speakers where feedback is possible.
- Press Ctrl+C immediately if feedback, runaway level, or unexpected behavior
  occurs.

The loop bus uses 0.65 gain and the FX100 software monitor uses 0.30. Concurrent
loops are normalized before the final ±0.98 limiter.

## Repository layout

```text
.
├── README.md
├── AGENTS.md
├── guitar-audio-looper.jolt  # main application entry point
├── audio_looper/             # main policy, DSP, engine, runtime, UI, checks
├── AUDIO_LOOPER_PLAN.md      # main design and verification record
└── poc/
    ├── README.md             # previous-POC commands and requirements
    └── ...                   # earlier songs, detectors, and note loopers
```

All previous experiments live under [`poc/`](poc/README.md). The production PCM
looper remains at the repository root. Generated binaries, Csound documents, recordings, prompt assets, and rendered
WAV files are ignored and must not be committed.

## Requirements

The current PCM looper targets macOS and has been tested with Jolt v0.8.10 and
Csound 6.18/API 4.00 using 8-byte `MYFLT`:

```bash
brew install csound libsndfile
brew install jolt-lang/jolt/jolt
```

Apple command-line tools provide `/usr/bin/cc`; macOS `/usr/bin/say` generates
temporary prompt assets. Set `CSOUND_LIBRARY` or `SNDFILE_PREFIX` when those
libraries are installed outside the standard Homebrew/framework locations.

Carp is needed only for Carp-based POCs. JVM Clojure, Overtone,
SuperCollider, and sc3-plugins are needed only for the corresponding older POCs
listed in [`poc/README.md`](poc/README.md).

## Verification

The PCM looper’s complete offline gate is:

```bash
jolt guitar-audio-looper.jolt --check
```

It covers pure command policy, setup/dashboard behavior, runtime effect dispatch,
sequenced native transport, every recorded stereo PCM sample, arbitrary sample
boundaries, silence trim, six-slot mixing, capacity handling, prompt playback,
and the native `pitchamdf` lifecycle. It opens no audio device.

Static development checks:

```bash
cljstyle check guitar-audio-looper.jolt audio_looper/*.jolt
clj-kondo --cache false --lang clj --lint \
  guitar-audio-looper.jolt audio_looper/*.jolt
```

See [`AUDIO_LOOPER_PLAN.md`](AUDIO_LOOPER_PLAN.md) for the detailed
architecture, invariants, and verification record.
