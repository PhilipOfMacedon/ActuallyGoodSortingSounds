# UI preset extension contract

The application owns simulation commands and state. A preset owns presentation: its stable identity, supported appearance tokens, composition, and control styling. Current, Flat VGA/DOS, and Classic Windows 95 use the same `ControlPanel`, `UIRoot`, settings actions, algorithm browser, and `AppController`.

## Adding a preset

1. Add a stable `UiPresetId` and stable text ID in `UiAppearance.h` and register its `UiPresetDefinition` in `UiAppearance.cpp`.
2. Declare every editable appearance property in the preset token schema. A descriptor defines its stable ID, category, tagged type, factory value, allowed range or choices, help text, and invalidation class. Keep simulation limits and commands out of appearance tokens.
3. Provide a base palette in `ThemePalette.cpp` and map its color tokens to palette fields. Palette selection clears color overrides while leaving typography, metrics, and decoration overrides intact.
4. Add the preset composition in `calculateUiLayout` in `UiArchitecture.cpp`. Return bounds, hit subregions, clips, focus order, and the chart viewport together. Keep interactive subregions aligned with what the painter draws.
5. Select control presentations and appearance behavior for the new preset in `ControlPanel::updateLayout` and `ControlPanel::drawWidget`. Use semantic control roles and resolved theme values; do not infer command behavior from captions.
6. Add headless schema, layout, interaction, and profile tests to `AppFrameworkTests`. Test the normal responsive sizes and verify that bounds remain nonnegative and hit regions do not overlap.

The current paint boundary consists of `UiControlPaintStyle`, `resolveUiControlPaintStyle`, `UiPaintContext`, and `RecordingUiPaintContext`. Keep drawing decisions in preset-specific presentation code and keep pointer, keyboard, popup, and value-edit mechanics in `UIRoot` or the shared host. The recording context is intended for deterministic geometry and paint-contract checks without an SDL window.

## Registered presets

- **Current** preserves the original toolbar and full-width chart arrangement.
- **Flat VGA/DOS** uses a squared setup rail, monochrome DOS scrollbar controls, and a compact bottom transport. At narrow widths, setup controls reflow above the chart.
- **Classic Windows 95** uses a Windows 95 palette, menu row (`File`, `View`, `Tools`), beveled controls, a rail plus chart, and a large bottom transport with pixel-drawn play, step, and reset icons. Previous/next event navigation remains available on the seek-bar arrows. The `View` menu toggles borderless fullscreen; `Alt+Enter` is the keyboard equivalent and `Escape` exits fullscreen.

`File` provides Build Trace, Generate & Play, and Reset. `Tools` opens the algorithm browser and settings. These menus also remain available when the setup rail is collapsed.

## Shared behavior

`UiScreenModel` is built from a simulation snapshot, preferences, queued-play state, and application status. It owns transport captions and availability, algorithm capabilities, selected direction and duration labels, and structured telemetry. `ControlPanel` prepares this model and the `UiLayoutResult` before chart rendering. `ArrayRenderer` receives the resulting chart viewport, including its clip.

`UiSessionState` stores meaningful navigation state such as the active settings page, algorithm search and selection, and scroll positions. `UIRoot` owns transient hover, press, focus, popup, and pointer capture state. When a preset changes, close popups and release capture while preserving session navigation. Appearance preview and preset selection do not dispatch simulation commands.

Controls emit `UiEvent` values with stable control IDs and event kinds. Existing app-action bindings remain the command boundary to `AppController`; presentation code does not execute trace generation, playback, audio preparation, or simulation changes directly. Preserve `GenerateAndPlay` behavior when changing transports or layouts.

## Appearance persistence and profiles

Each preset has one `UiThemeCustomization` in `UiAppearancePreferences`. Register tokens before storing overrides; validation and the editor both consume the same descriptors. Use `UiInvalidation::Paint` for color-only changes, `Layout` for geometry changes, and `Typography` for font or text-size changes.

Settings version 8 stores the selected preset and each preset's palette and token overrides. Appearance profile files use UTF-8 `.agss-ui.ini`, `format=agss-ui`, and version 1. A profile represents one base preset only. Parse into a draft, report unknown tokens, and reject invalid known values or unsupported base/version identities without changing saved settings. Keep import/export limited to appearance data.

Font choices store family and style identities, not machine-specific paths. `WindowsFontCatalog` resolves installed local faces through DirectWrite; `TextLabel` rasterizes them through SDL_ttf and uses bundled Courier New Bold when a face cannot be resolved or opened. Keep resource creation on the application thread and key font resources by path, collection index, size, and DPI.
