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

## Validation

- Solution builds and AppFramework checks pass for Win32/x64 Debug/Release, including new regressions for readiness/feedback ordering, collapse during preparation, natural completion, manual transport restoration, boundary navigation, intermission pause/resume, popup scrolling, and default component styles.
- The standalone audio transport project now compiles its SeedLibrary/DemoModels/DemoRunner dependencies. Its earlier executable predated the seed/demo wave. The rebuilt x64 Release target passes all callback transport regressions at 80 and 800 items using SDL's dummy driver.
- SDL was visually inspected using the Computer Use skill's `@oai/sky` runtime. Classic Five advanced while collapsed and restored its manual session on completion. Explicit demo-switch Stop also restored ordinary Event/Play/Reset controls. The editor dropdown overlays its controls correctly. Current and VGA styling were inspected; Windows 95 inspection exposed the surface contrast defect repaired in this wave.
- Browser Computer Use was stopped by its URL-policy support restriction while attempting to interact with the editor concept. No further UI input was issued afterward. Final Windows 95 contrast changes therefore have automated/build verification, without a second live inspection in this turn.

## Editor design remains a proposal

The supplied interactive concept compares a persistent queue/inspector layout with a workspace-tab layout. Shared class recipes, optional per-step overrides, preset protection, and demo transport remain the same concepts in both. The substantial editor layout redesign awaits owner feedback; the current implementation only receives shared component repairs and readable caption spacing.
