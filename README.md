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
- a context-sensitive 7-6-7-6 cancel word to stop recording/playback, or exit;
- outer-silence trimming for the first/master loop;
- synchronized later loops whose cycle count is set by the cancel word's first note;
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

Cancel word (scale degrees **7 6 7 6**):

```text
B A B A
```

The cancel word follows the chosen/saved scale and accepts supported octaves.
It is derived from the existing profile, so old sessions need no migration or
re-recording. It replaces the old low-E scrape; there is no scrape classifier,
spectral template, or startup scrape calibration.

| Guitar sequence | Action |
| --- | --- |
| Wake → degree 1–6 (C–A), then wait | Select an empty slot and arm recording |
| While recording → cancel word | Finish recording and keep the retained take |
| While armed → cancel word | Cancel that take, even if recording starts during the word |
| Wake → slot → degree 2 (D) | Start that slot's playback |
| Wake → slot → cancel word | Stop that slot's playback, or cancel its queued start |
| Wake → slot → degree 3 (E) | Erase that stopped slot |
| Wake → cancel word | Fade out and exit, discarding any active recording |

`Wake → slot` is a prefix of playback/erase commands. After the selection prompt
there is a two-second action window plus 350 ms recognition grace. Without an
action an **empty, idle** slot arms recording; an occupied clip never
auto-records or overwrites. Degree 1 can also explicitly arm an empty slot.
Wait for the armed prompt and start cue before playing the take.

Play four clean single notes within four seconds; one held note is not a word.
The recognizer uses the existing confirmed pitch events and ±35-cent tolerance,
not an effect-specific spectral fingerprint. Alternating notes may be legato;
legato octave jumps do not add syllables. Once degree 7 starts a cancel attempt,
its suffix is not dispatched as slot/action commands. A partial attempt may span
the normal action window; a wrong/timed-out menu attempt cancels the command
rather than silently arming a recorder. Partial/wrong words do not stop an
ongoing recording. Completed words retain their current wake/slot/take context.

Prompts and guards pause ordinary menu notes, but contextual cancel words remain
available. Choose a custom wake distinct from the cancel word; preferably use
degrees 1–5 to avoid ambiguous overlapping sequences. During recording the
reserved cancel word takes precedence over an identical/contained wake pattern.
The default wake and cancel words use disjoint notes.

Ordinary EQ/drive no longer requires a scrape template, but **not every effect
is guaranteed**: noise gates, pitch shifting, harmonizers, strong modulation or
delay can still disrupt single-note tracking. Music containing the cancel word
can also stop a take. `[d]` shows raw pitch/level/gate, note history and cancel
progress; seeing the right notes is distinct from receiving a native operation
acknowledgement. Keyboard `q`/Ctrl+C remain safety exits.

On completion, recording finalization uses the **first note's detected onset**,
not the last note or recognition time. Pitch analysis can lag the physical
attack, so this is an estimate and some attack audio may remain. Incomplete or
unrecognized words remain recorded. For later clips, keep the most recent
complete master cycles before that first onset; if no cycle is complete,
recording continues. The native 120-second capacity limit still takes precedence
if reached before word confirmation.

The first/master clip records freely. Later clips start on the next shared
master boundary without a cycle-count question. The cancel word selects the
completed-cycle prefix. At capacity the largest complete-cycle prefix is kept.
Actual stereo PCM remains unchanged; publication and persistence still wait for
the matching native acknowledgement.

Erase is direct and requires its native acknowledgement. Playback stop is queued
at the next master boundary; cancelling a queued start also waits for native
acknowledgement. `Wake → cancel word` and keyboard `q` share the safe exit fade,
without an extra confirmation. `[c]` reopens setup only when all slots are empty
and idle.

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

Quiet clips receive **playback-only makeup gain**, including clips loaded from
older sessions; re-recording is not required. Each clip uses one constant stereo
gain, targeting a 0.5 peak before the loop bus, capped at 16× (about +24 dB).
Clips with stereo RMS below −50 dBFS are not boosted. Original PCM/WAVs,
recognizer input, FX100 monitoring and spoken-prompt levels are unchanged. This
cannot improve the recording's signal-to-noise ratio. Start with low output
volume when trying the louder playback.

The loop bus still uses 0.65 gain and the FX100 software monitor uses 0.30.
Concurrent loops are normalized before the final ±0.98 limiter.

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

It covers command policy, setup/dashboard behavior, runtime dispatch, sequenced
native transport, every recorded stereo PCM sample, arbitrary sample boundaries,
silence trim, six-slot mixing, capacity handling, prompt playback, session
save/reload, atomic failure recovery, invalid-file/lock rejection and the native
`pitchamdf` lifecycle.

Cancel-word checks cover mixed octaves, duplicates, wrong/incomplete words,
context/sequence gaps, menu timeouts, prompts, queued playback cancellation races,
armed-to-recording transitions and first-note cycle selection. Actual native
pitch events from generated clean, harmonic-rich and clipped-tone fixtures drive
host finalization and matching native acknowledgements, retaining every stereo
sample. These are offline regressions, **not hardware validation across all FX100
presets**. The former recorded-scrape environment-variable checks are retired.

Playback makeup checks retain raw samples, stereo balance, exact retained-range
measurement, cache invalidation/reload, silence/noise-floor/full-scale safety,
and six coherent boosted loops with monitoring. All checks use temporary files
and open no audio device.

Static development checks:

```bash
cljstyle check guitar-audio-looper.jolt audio_looper/*.jolt
clj-kondo --cache false --lang clj --lint \
  guitar-audio-looper.jolt audio_looper/*.jolt
```

The default `cljstyle` file filter skips `.jolt`. For an effective format check,
copy touched forms to a temporary `.clj` file and check that file; a silent
`.jolt` invocation alone is not a formatting pass.

See [`AUDIO_LOOPER_PLAN.md`](AUDIO_LOOPER_PLAN.md) for the detailed
architecture, invariants, and verification record.
