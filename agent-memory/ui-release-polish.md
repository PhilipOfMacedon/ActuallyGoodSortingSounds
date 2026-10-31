# UI release polish

## Implemented

- Demo Step options are data driven from the algorithm descriptor. Canonical Merge's buffer-visualization preference is exposed as a per-step presentation option alongside execution mode and writeback, in the same scrollable pane.
- The Step options pane uses a panel-layer backdrop so it cannot paint over the controls it contains. The recipe summary stays below that pane; algorithms with no option groups do not render an empty heading.
- Compact algorithm-profile cards no longer allocate shortcut rows that overlap the complexity and capability facts.
- Demo class-input fields and labels are clipped to their scroll viewport for both drawing and hit testing. Synchronization preserves the viewport state and no longer makes clipped controls interactive again.
- Customized color sets remain per interface preset. Settings and the appearance editor expose the current palette correctly; the appearance editor uses the active draft appearance. Apply stays open, and Close uses the shared dirty-state confirmation.
- New/reset global audio defaults select Special release at 250 ms. Saved explicit Hold-selection policies remain unchanged. New demo steps use Reference cadence; persisted timing choices are preserved.
- Settings v9 and appearance-profile v2 read older formats with the documented migrations. Demo preset v3 keeps the previous Completion-scan and merge-visibility behavior for older files.

## Verification on 2026-10-30

- Visual Studio solution builds passed in x64 Debug, x64 Release, Win32 Debug, and Win32 Release (solution platform `x86` maps projects to `Win32`).
- `AppFrameworkTests.exe` passed in all four configurations after the final viewport and paint-layer changes.
- x64 Release `SortingLogicTests.exe`: 13,490,560 checks, zero failures.
- x64 Release `SoundEngineTests.exe`: 417,379 assertions passed.
- x64 Release `AudioDeviceTransportTests.exe` passed on SDL's dummy audio backend, including both playback timing modes, both idle policies, and 800-item cases.
- Native x64 Release inspection confirmed the Demo Step Merge options and scrolling, the Class inputs and seed fields, the Session tab, Appearance customization with Customized selected, and the existing saved Hold selection. The compact algorithm-profile and clipped class-input regressions were fixed during that inspection.

## Remaining delivery note

Package the verified x64 Release output with its SDL/GLEW/MSVC runtime, font, shaders, and third-party notices. The major release itself is a later, separate action.
