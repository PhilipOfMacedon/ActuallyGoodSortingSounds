# GUI scrolling refactor

## Why

The app has a retained widget root, but several large panes handle scrolling themselves. The family/variant browser has its own integer offsets, settings rows use a separate helper, combo boxes cap popups at eight entries, and the appearance editor relies on those combo popups. That works for the present catalog, but it creates multiple hit-test and clipping implementations just before the catalog grows substantially.

## Proposed shared contract

Add a reusable `UiScrollViewport` model to the UI layer. Each viewport owns a clip rectangle, content extent, row extent, and clamped offset. It supplies wheel handling that consumes input only inside its bounds, visible row ranges, row hit testing, row bounds, and an ensure-visible operation for keyboard focus. A pane still owns its content and paint order; the viewport only provides reusable layout and input geometry.

Keep each pane's offset by stable pane identity. Recalculate its extent when content, layout size, or DPI changes, then clamp the old offset. Search/filter changes reset the affected list offset. Modal panes capture input as they do today; wheel events outside the active viewport do not scroll another pane behind them.

## Apply in this order

1. Move settings-page row scrolling onto the shared viewport. Keep tab/footer controls fixed and clip only the content area.
2. Move algorithm-family and variant panes onto the same viewport. Preserve search, selection, keyboard focus, per-pane scrolling, and visible-row clipping.
3. Route combo-box popup scrolling through the same viewport model used by pane lists, so dropdowns and pane lists agree on wheel direction, clipping, and keyboard visibility.
4. Use the same contract for future long algorithm-option lists and workspace inspectors. Do not make the chart itself scroll until a real workspace layout needs a viewport; most current views are intended to resize into the available chart rectangle.
5. Add geometry tests for pointer capture, wheel boundaries, clamping after content/resize changes, hit testing after scroll, and keyboard ensure-visible. Visually verify the family browser, settings, nested dropdowns, resize behavior, and dense catalog.

## Current implementation status

- Shared `UiScrollViewport` geometry and wheel model: implemented.
- Settings-page content and algorithm family/variant lists: migrated to the shared model.
- Combo-box popups: migrated to per-widget `UiScrollViewport` state; mouse wheel changes the visible option window and keyboard selection ensures the selected row stays in view.
- The algorithm profile uses wrapped, fixed-size text in a clipped scroll viewport. Mouse-wheel input scrolls inside the pane, and an overflow scrollbar supports track clicks and thumb dragging while the keyboard shortcuts remain pinned below it.
- Workspace inspector: not present yet, so no extra scroll surface was added speculatively.
- Automated geometry checks cover viewport wheel bounds/clamping, row hit testing after scroll, keyboard visibility, long popup scrolling, and clicking a scrolled row. `AppFrameworkTests` passes in x64 and Win32 Debug/Release. Live inspection also confirmed independent mouse-wheel scrolling in the family list, the long Sorting Networks variant list (down through Matrix Sort), and the General Audio Advanced settings pane. The tab row and footer stayed fixed while list content moved within its clip.
- Live dense-layout verification: launched the app with 800 bars at the small window size, maximized it, built and played the 59,364-event Bitonic Parallel trace, and observed the full 10-second playback finish at event 59,364/59,364 with 5,977 network layers and a sorted array. All bars remained visible in the resized chart; dense bars narrowed as expected, with no clipping, crash, or layout failure.
- For live testing, launch the app with its build output directory as the working directory; shader and font asset paths are relative to the process working directory.
