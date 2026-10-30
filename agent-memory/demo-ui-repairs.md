# Demo transport and shared UI repairs — 2026-10-30

## Observed defects and causes

- Demo presentation mutated transport actions in place. Ordinary synchronization restored only part of the captions and left demo actions installed after completion or Stop.
- Audio readiness could start queued playback after the bridge had already published a paused transport for that frame. Matching paused feedback could immediately stop that newly started simulation, consuming its queued playback intent.
- The algorithm button's specialized painter read saved manual preferences instead of its authoritative widget label. This displayed the manual algorithm during a different demo step.
- The setup profile used an obsolete seed-readout anchor and overlapped the new seed management row.
- Demo controls painted their own fills and labels, bypassing the preset-aware widget painter. Popup fills were below ordinary editor text, causing compounded text.
- Reassigning unchanged combo options and selection reset the option viewport on every frame. Popup sizing was implemented only for the main controls.

## Implemented boundaries

- Readiness changes only update the preparation gate. The controller update starts queued playback before the next audio publication, avoiding the paused-feedback handoff race.
- Demo pause intent is explicit and survives preparation, navigation, and intermissions.
- Main controls first synchronize a complete manual presentation, then apply the active demo transport presentation. A switch enables the selected demo or stops it and restores the manual session. Previous/Next boundaries are disabled explicitly.
- All retained combo boxes share choice styling, viewport placement, measured popup width, clipping, focus, keyboard interaction, and scrolling through `UIRoot`.
- Demo buttons and combos submit standard Control paint commands. Typed Panel commands and the shared surface painter apply preset borders, shadows, padding, and foreground rules. New panel/control decoration tokens are available through the existing appearance editor and INI format.
- The Windows 95 UI surface palette is distinct from its dark chart background. Selection text uses a readable surface foreground. The chart palette remains independent.
- Profile placement follows the actual setup controls; seed favorites and history have separate rows in rail layouts.

## Prior transport/UI slice validation

- Solution builds and AppFramework checks passed for Win32/x64 Debug/Release, including readiness/feedback ordering, collapse during preparation, natural completion, manual transport restoration, boundary navigation, intermission pause/resume, popup scrolling, and default component styles.
- The standalone audio transport project compiles its SeedLibrary/DemoModels/DemoRunner dependencies. The rebuilt x64 Release target passed callback transport regressions at 80 and 800 items using SDL's dummy driver.
- SDL was visually inspected using `@oai/sky`: Classic Five advanced while collapsed and restored its manual session on completion. Explicit demo Stop restored ordinary Event/Play/Reset controls. The editor dropdown overlays its controls correctly. Current and VGA styling were inspected; Windows 95 inspection exposed the surface contrast defect repaired in that slice.
- At the time of that earlier review, Browser Computer Use had been stopped by its URL-policy support restriction while attempting to interact with the editor concept. The later native `@oai/sky` inspection below supersedes the old “no further UI input” state.

## Approved editor implementation — 2026-10-30

- Replaced the compact command rows with the approved persistent queue and three-tab inspector: **Step**, **Class inputs**, and **Session**. Queue selection is draft inspection only; it does not navigate or mutate the active demo step.
- The Step page edits direction, timing, duration, Completion scan, algorithm options, and an optional full input override. Class inputs always edit the shared recipe and report which non-overridden steps use it. Session edits name, intermission, and looping.
- Reused the family-organized algorithm browser for manual selection, Add algorithm, and Change algorithm. Each caller supplies its own typed selection callback; the nested overlay restores the editor context when dismissed.
- The controller owns the draft, saved baseline, dirty state, pending preset destination, and Save/Discard/Cancel transition. Closing the editor retains the draft; launch from either the editor or main Demo switch uses that same draft. Runtime preset snapshots stay independent.
- Added per-step Completion scan to the model and app-side preparation. Demo preset v1 entries load as scan-off; v2 writes the explicit flag. Existing manual settings and sound synthesis are unchanged.
- Added reusable retained tabs, virtualized queue list, and editable fields. Redundant input-capture updates no longer clear focus, popup state, or a press in progress.

## SDL pointer diagnosis and regression

The automated click was reaching SDL. The raw event trace showed a left-button down and up at the expected app coordinates. The down was handled, but `UIRoot::setInputCaptured(false)` erased the pressed widget before the next-frame release. `ControlPanel::prepareFrame()` refreshes overlay capture every frame, so this happened on ordinary frame-spanning clicks too. The same setter also closed combo popups and discarded keyboard focus each frame.

`UIRoot::setInputCaptured` now returns immediately when the requested state is unchanged. A transition from captured to uncaptured still closes transient UI and releases state. AppFramework regressions cover a press/release spanning a frame, popup persistence, selected-label refresh, and focus retention. The temporary event logger has been removed from source.

Live verification used the x64 Debug SDL window: clicking **Edit Demo** opened the editor, and pointer clicks switched between **Step**, **Class inputs**, and **Session**. The earlier pointer-injection confusion was therefore an app state-reset defect, not an SDL or computer-use input limitation.

## Validation

The approved design is implemented. On 2026-10-30:

- Visual Studio solution builds passed for x64 Debug, x64 Release, x86-mapped Win32 Debug, and x86-mapped Win32 Release.
- AppFramework checks passed in all four configurations, including the frame-spanning press, retained popup, dropdown-label refresh, editor draft, nested browser, and preset-switch regressions.
- x64 Release SortingLogic passed 13,490,560 checks; SoundEngine passed 417,379 assertions.
- Audio device transport passed using SDL's dummy backend for both timing modes, idle/non-idle callbacks, and 80/800 items.
- Live x64 Debug inspection confirmed the real C++ editor opens by click and navigates among Step, Class inputs, and Session. The clean, non-instrumented rebuild reproduced the fix. The editor was left open on Session for the owner.

The low-level SDL trace was temporary and has been removed. It recorded down/up coordinates only in `%LOCALAPPDATA%\Temp` during diagnosis; the project has no added diagnostic logging.
