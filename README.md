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
- persistent session folders with lossless stereo WAV tracks and versioned metadata;
- one active recorder, no overdub;
- octave-independent guitar commands based on C-major scale degrees;
- sample-accurate recording and playback boundaries;
- context-sensitive low-E string scrape to stop recording or playback, or exit;
- outer-silence trimming for the first/master loop;
- synchronized later loops whose completed master-cycle count is set by the stop scrape;
- queued cycle-boundary playback;
- spoken prompts mixed by Csound, not played directly by `say`;
- a fixed ANSI dashboard and a line-oriented `--no-tui` mode;
- foreground ownership, Ctrl+C cleanup, and a 600-second OS safety alarm.

### Quick start

Run commands from the repository root:

```bash
# Complete offline suite; does not open an audio device
jolt guitar-audio-looper.jolt --check

# Persistence-only offline checks; temporary files, no audio device
jolt guitar-audio-looper.jolt --check-sessions

# Inspect current CoreAudio metadata; does not open a stream
jolt guitar-audio-looper.jolt --devices

# Offline dashboard preview
jolt guitar-audio-looper.jolt

# Live looper with the full TUI
jolt guitar-audio-looper.jolt --live
```

`--live` is the only mode that opens audio devices. It requires a foreground
terminal. First choose **New session** (Enter/`n`) or **Load previous session**
(`l`, then its list number). Select the current input with the keyboard. A loaded
session restores its saved control profile; a new session offers Enter/1 for
C major with the default wake, 2 to capture a custom scale and wake, or 3 to keep
C major and capture a custom wake. In `--live`, play C/D/E
from the default scale to select options 1/2/3; release the note and wait for
silence. Keyboard selection remains available. Setup capture opens the selected
input with monitoring off. The finite speech cache is generated before any audio
stream opens, so startup can pause for tens of seconds.

Use `--live --no-tui` only when line-oriented logs are desired. That option
intentionally disables the fixed dashboard.

### Default guitar controls

Notes may be played in **any supported octave**. The note names below identify
scale degrees, not required registers.

Wake phrase:

```text
C D C D
```

The low-E scrape is the context-sensitive stop/exit gesture:

| Guitar sequence | Action |
| --- | --- |
| Wake → degree 1–6 (C–A), then wait | Select an empty slot and arm recording |
| While recording → scrape low E | Stop recording directly |
| Wake → slot → degree 2 (D) | Start that slot's playback |
| Wake → slot → scrape low E | Stop that slot's playback, or cancel its queued start |
| Wake → slot → degree 3 (E) | Erase that stopped slot |
| Wake → scrape low E | Fade out and exit immediately, discarding any active recording |

`Wake → slot` is a prefix of the playback and erase commands, so after the slot
selection prompt there is a two-second action window plus 350 ms recognition
grace. If no action arrives, an **empty, idle** slot arms recording; a slot with
an existing clip never auto-records or overwrites it. Degree 1 after slot
selection can also explicitly arm recording. Wait for the recording-armed prompt
and start cue before playing the take. Notes played during spoken prompts and
their guards are ignored; wait for the prompts before selecting a note.
Contextual scrapes are accepted during those prompts. A scrape also cancels an
armed recording.

The experimental scrape recognizer uses an aggregated spectral fingerprint from
six low-E scrapes with varied speed, force, and contact position through the
same FX100/pickup/preset chain. It is not a general classifier or pitch
recognizer. It calibrates input noise over the first ~0.8 seconds; different
guitars, effects, levels, or normal playing can still cause missed or false
triggers. `[d]` displays its current match and noise level. A command now requires
at least 180 ms of sustained spectral evidence (16 hits, at least 80% matching
frames within a bounded 250 ms candidate), not just a brief matching attack.
Use a deliberate roughly quarter-second-or-longer scrape; very short or quiet
scrapes may intentionally be ignored. Rearming requires 120 ms of actual input
quiet as well as a 650 ms cooldown; merely changing to a pitched note is not release.

On a recognized scrape, finalization uses its estimated onset, not the later
recognition time. The onset is an analysis-window estimate, not a guarantee of
exact physical timing. An unrecognized scrape remains in the take. For later
clips the stop is rounded back to the most recent completed master cycle; if no
cycle is complete, recording continues. Verify results before relying on this
experimental control.

For the first/master clip, record freely and stop with a scrape. Later clips arm
without asking for a cycle count, start on the next shared master boundary, and
continue until another low-E scrape. That scrape determines the length by keeping
all complete master cycles before its estimated onset. If it arrives before one
complete cycle, recording continues. At the 120-second capacity limit, the
looper keeps the largest complete-cycle prefix that fits.

Erase is a direct command and is published only after the native engine
acknowledges it. Playback stop occurs at the next master boundary; cancelling a
queued playback start also waits for native acknowledgement. `Wake → scrape`
and keyboard `q` use the same safe exit fade, without additional confirmation.
`[d]` toggles pitch debug and `[c]` reopens input
selection when all slots are empty and idle.

The first finalized clip establishes the master cycle. Its leading/trailing
silence is trimmed from bounded 10 ms stereo-RMS summaries at -50 dBFS, retaining
20 ms preroll and 50 ms release tail. Later clips preserve complete master cycles
so trimming cannot destroy synchronization.

### Persistent sessions

`--live` stores sessions under `sessions/` at the repository root (git-ignored).
Set `AUDIO_LOOPER_SESSIONS_DIR` to use another location. New folder names use
readable **machine-local time**, for example `2026-10-30_09-14-23-<uuid>`; `TZ`
can explicitly override the timezone. The UUID prevents same-second/DST
collisions. Existing Unix-timestamp-named sessions still load without renaming.
Offline preview/checks never create production sessions or open audio devices.

```text
sessions/YYYY-MM-DD_HH-mm-ss-<uuid>/
├── session.edn                 # schema version, ID, timestamps, profile, master, tracks
├── track-1-<uuid>.wav           # finalized stereo PCM; other occupied slots follow
└── .lock                       # OS lock, automatically released on process exit
```

Each accepted native finalization saves the retained PCM as a 44.1 kHz stereo
**float64 WAV**, preserving the engine's samples without resynthesis or gain
changes. The manifest records each occupied slot and its exact frame count;
cycle counts follow from the master length. Erasing a track commits the new
manifest before removing its old WAV. In-progress/cancelled recordings are not
saved. Quit/Ctrl+C keeps previously committed clips, not an unfinished take.

Loading restores occupied slots, the master period and the custom scale/wake.
All tracks start **stopped**, with no queued operations; the master sample-clock
origin is reset to zero. Input/output devices are selected afresh, never opened
using stale saved `adcN`/`dacN` identifiers. Recording continues in the same folder.

WAV files are committed before the manifest, using same-directory atomic renames
and file/directory sync. A write error stops the session with a nonzero exit
rather than reporting a successful save. A crash can leave an unreferenced file,
which loading ignores. Missing/corrupt/non-finite WAVs, invalid metadata and
concurrent writers are rejected; do not edit files while a session is open.
Storage runs on the host thread, never the real-time audio thread. Sessions are
not automatically backed up or pruned.

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
session save/reload (every retained stereo sample), atomic failure recovery,
invalid-file/lock rejection, the native `pitchamdf` lifecycle, and the native scrape recognizer against six
synthetic profile fixtures plus negative tone/noise, short-attack and interrupted-burst
fixtures. It also checks quiet-only rearming, local-time names and legacy session
compatibility. It opens no audio device.

Static development checks:

```bash
cljstyle check guitar-audio-looper.jolt audio_looper/*.jolt
clj-kondo --cache false --lang clj --lint \
  guitar-audio-looper.jolt audio_looper/*.jolt
```

See [`AUDIO_LOOPER_PLAN.md`](AUDIO_LOOPER_PLAN.md) for the detailed
architecture, invariants, and verification record.
