# Pause, resume, and single-step transport defect

## Report and evidence

The user supplied a 59.114646-second screen recording demonstrating a stuck
mid-run pause. The reported 14 successes / 16 failures in 30 pause trials were
performed separately; they are not trials counted from that recording. The
user additionally reported reliable failure after previous/next event actions.
Reset or seeking to a boundary recovered the application.

The recording contains 48 kHz stereo capture audio. That capture format alone
does not imply that the source synthesizer or its sample-rate conversion is
the cause.

## Reproduction and cause

A new standalone SDL integration target reproduces the app frame loop with
the real controller, worker, mailbox, callback, and synthesizers. Its default
driver is SDL's dummy driver; preferences are in memory.

Before the fix, the initial middle-of-trace seek caused 440 preparations in
five seconds, with transport revision reaching 443. Preparation never settled.
The caller passed `device available && timeline ready` as device availability.
Seek preparation temporarily made readiness false; finishing the seek made
it true. The bridge interpreted that false/true transition as device recovery
and requested another seek. This is a feedback loop rather than an intrinsic
DSP cost or a random oscillator failure.

Pause enters seek reconstruction when its visual cursor differs from the
bridge's prior cursor. Whether the last callback advanced that cursor depends
on frame timing. A previous/next event necessarily changes the cursor. This
explains the reported timing-sensitive pause outcome and deterministic stepping
failure without attributing a measured probability to the video.

During preparation, the callback receives a temporary stop/drain transport.
It can report that stopped state with the current transport revision. The
earlier revision-only feedback check therefore did not reject it. A completed
prepared snapshot also needs explicit callback acknowledgement before its
playhead can be used.

## Changes

- Pass raw open-device availability to `AudioPresentationBridge::update`.
  Track it independently of timeline or seek readiness.
- Add `playheadReady`, requiring the correct timeline generation and callback
  acknowledgement of the current seek snapshot. Use it for the playback clock,
  pending-preparation gate, and feedback acceptance in the application loop.
- On snapshot installation, apply a newer coalesced transport command to the
  incoming synthesizer. Installing paused history must not discard a new Play.
- Repair the SPSC mailbox: an empty poll now leaves the reader-owned slot
  alone. Previously an unconditional exchange could place that slot back in
  the producer pool without updating consumer ownership, allowing concurrent
  writes to a slot the reader might copy.
- Add `transport_revision` and actual `playing` columns to F9 callback CSVs.
- Add concurrent mailbox and preparation/acknowledgement regressions, plus
  the real SDL callback integration target. It supports an explicit audible
  `--physical-device` mode as well as the default dummy driver.

The frozen sound profile and synthesis equations were not retuned. Existing
working-tree changes for 1,024-item bounds, completion scans, Merge-buffer
visualization, and editable setup controls were preserved. One old settings
test used 900 items as an invalid value; its input now derives from
`MaxItemCount + 1` so it continues testing the current supported bound.

## Validation artifacts

Build and execution logs are under ignored
`x64/Release/PauseDebug20261017/`. The standalone integration target is
`AppFrameworkTests/AudioDeviceTransportTests.vcxproj`; its usage and scenario
matrix are documented alongside it.

Each complete dummy-driver run checks 70 pause/resume cycles and 70 single
steps: both timing modes and idle policies at 80 items, plus dense 800-item
Insertion cases. It checks successful advancement after resume, stable paused
cursors, no repeating preparation, Reset, no transport persistence writes,
and no default-profile output overload.

The x64 Release physical-device run used WASAPI at 44.1 kHz stereo and passed
20 additional pause/resume cycles and 20 steps across both timing modes and
idle policies. A live GUI pass also resumed from a mid-run seek followed by a
single-event step and reached completion. Those checks establish the exercised
transport behavior; they are not a subjective listening score or a device
latency benchmark.

Final validation passed in all four supported configurations:

| Configuration | AppFramework | SortingLogic | SoundEngine | SDL transport |
| --- | --- | --- | --- | --- |
| x64 Release | Pass | Pass | Pass | Pass |
| x64 Debug | Pass | Pass | Pass | Pass |
| Win32 Release | Pass | Pass | Pass | Pass |
| Win32 Debug | Pass | Pass | Pass | Pass |

Each configuration passed 106,832 SortingLogic checks and 417,371 SoundEngine
assertions. Across these four configurations the dummy-driver integration
exercised 280 pause/resume cycles and 280 steps, in addition to the 20 cycles
and 20 steps on WASAPI. The dense Debug setup deadline is 60 seconds because
unoptimized 800-item trace construction exceeded the original five-second
fixture deadline; transport settling checks remain unchanged.

Portable packaging is deferred to the next major milestone at the user's
request. This debugging wave delivers the normal Visual Studio executables.
