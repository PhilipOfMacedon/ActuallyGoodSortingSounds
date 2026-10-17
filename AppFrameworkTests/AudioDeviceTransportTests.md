# SDL audio transport regression

`AudioDeviceTransportTests.vcxproj` is a standalone integration target. The
existing AppFramework console target remains independent of SDL. This target
uses the application's real `SdlAudioOutput` callback with SDL's process-local
dummy driver; it opens no graphics window and sends no audio to the user's
speakers. Preferences use an in-memory store.

Build with Visual Studio 2022 MSBuild from the repository root:

```powershell
MSBuild AppFrameworkTests/AudioDeviceTransportTests.vcxproj /m /p:Configuration=Release /p:Platform=x64 /p:SolutionDir="<repository-root>\"
./x64/Release/AudioDeviceTransportTests.exe
```

The target also supports Debug and Win32. Its only runtime dependency is the
same SDL2 DLL already used by the application; the build copies it locally.

Pass `--physical-device` to use the currently configured output driver and
perform a shorter audible run of 20 pause/resume cycles and 20 steps across
both timing modes and idle policies. The default invocation always uses SDL's
dummy driver.

The scenarios cover:

- Fixed duration and reference cadence, with Hold and Release selection.
- 60 pause/resume cycles and 60 previous/next event steps at 80 items.
- Another 10 pause/resume cycles and 10 steps at 800 items.
- Mid-run seeks, stable paused cursors, successful resume, and Reset.
- Zero settings writes for transport and no default-profile output overload.
- Preparation settling instead of a repeated recovery/seek loop.

This is an asynchronous callback integration regression, not a hardware latency
benchmark or a substitute for listening through a physical device. The
AppFramework suite separately checks concurrent mailbox coherence and callback
acknowledgement fencing.

On the pre-fix path, the initial mid-run seek started 440 preparation cycles in
five seconds and failed to settle. The caller had passed timeline readiness as
device health. Completing a seek consequently looked like recovering a device,
which scheduled another seek. Device availability is now independent of
timeline readiness, and feedback is gated until the callback acknowledges the
installed history.
