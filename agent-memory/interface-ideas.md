# Interface ideas

Ideas in this file are visual proposals, not implementation requirements. Promote one to a task only after the user asks to build or evaluate it.

## Flat VGA/DOS grid, glass panels, and custom title bar

- **Status:** interface idea recorded on 2026-10-29; no runtime change requested.
- **Reference:** [Flat VGA/DOS grid, glass, and custom-caption concept](../docs/interface-concepts/flat-vga-dos-grid-glass-custom-caption.png).
- **Observed source:** the reference is an image-generation edit based on a live Flat VGA/DOS application capture. Small labels and proportions may have been re-rendered; it is a visual direction, not a pixel-accurate UI specification.

### Proposed visual direction

- Carry the low-contrast mathematical grid across the full application background.
- Keep interactive buttons fully opaque (`alpha = 1`) with the preset's square DOS styling.
- Use dark translucent, softly blurred surfaces for the setup rail and compact status panels. Start around 72–78% opacity, then tune for legibility.
- Give small value fields and selectors a restrained frosted surface, around 82–86% opacity.
- Make the large algorithm-description area the most opaque frosted panel, around 90–93%, with stronger background diffusion so text remains easy to read.
- Replace the operating system's default caption with a themed app-owned title strip containing the selected app icon, title, and minimize, maximize/restore, and close controls.

Opacity ranges above are exploratory starting points from the visual mockup, not measured contrast requirements. Review text contrast, focus states, hover states, and display scaling before implementation.

### Feasibility notes

- `SDL2BarGraphics/Window.cpp` creates a normal resizable SDL window without `SDL_WINDOW_BORDERLESS`, so Windows currently draws the native caption and frame.
- `Window::setClassicWindows95Caption` sets the native DWM caption and text colors; it does not draw custom title-bar controls.
- A fully themed caption is feasible with the existing SDL/OpenGL application, but a borderless custom frame would need app-drawn window controls plus reliable drag, resize, minimize, maximize/restore, and close behavior. Keep keyboard shortcuts, high-DPI layout, and normal Windows window management behavior in scope during a prototype.
- Whether custom chrome applies only to Flat VGA/DOS or follows every interface preset is still open.
