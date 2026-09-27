# Koya layer-shell integration fixture

Run `./scripts/test-koya.sh` from the repository. No separate Koya build or
JavaScript dependencies are needed. The launcher mounts Koya's base assets
(fonts/shaders) first, then this application's assets, and selects `index.js`.

The application creates these surfaces on the first output:

| Surface | Koya role | Expected behavior |
| --- | --- | --- |
| Diagnostics | `window` | XDG content beneath shell elements; reports individual creation failures |
| Background | `background` | Covers the output behind content, including beneath reserved areas (`exclusiveZone: -1`); no pointer or keyboard input |
| Panel | `bar` | Top edge, stretched horizontally, 52px high; reserves 52px; no keyboard focus |
| Overlay | `overlay` | Bottom-right, 440×200 with 24px margins; no reservation; receives pointer events; animated marker |

The dashboard uses an XDG window because it must remain visible even when
the compositor lacks layer-shell. Shell surfaces always use their actual
layer-shell roles and unique `koya-test-*` namespaces. A failure does not
count as success or trigger an XDG fallback.

## Manual checks

1. Confirm the background is below XDG content, the panel above it, and the
   overlay above both. Kiosk fullscreen policy may obscure the background
   with the diagnostic window; inspect it while the XDG surface is unmapped.
2. Resize the nested Weston window. The panel should keep its 52px height,
   the background should fill the output, and overlay margins should stay
   24px from the bottom and right edges.
3. Press **P** to destroy/recreate the panel. Verify its exclusive zone is
   removed/restored and subsequent arrangements use the correct available
   area. Kiosk fullscreen applications must fill the remaining area: with the
   52px top panel mapped, content starts at y=52 and its height is reduced by
   52px. Unmapping the panel restores the full output height.
4. Press **O** to destroy/recreate the overlay. Confirm mapping, removal,
   animation and stacking still work after repeated cycles.
5. Click the overlay and inspect `pointerDown` log entries. The panel and
   background should not intercept input. Press **E** to toggle overlay
   keyboard interactivity between `exclusive` and `none`. When exclusive,
   typed input should appear in log entries for the overlay's window ID;
   press **E** again to release it.
6. Press **R** to log `getWindowInfo()` for all current surfaces. These are
   Koya's reported dimensions/estimated positions, not independent compositor
   measurements; compare them with the visible output.
7. Press **Esc** to quit, or close the nested Weston window. Check clean
   destruction of layer resources and exclusive zones.

`CREATED` indicates only that Koya obtained a surface and initialized it;
the app does not automatically assert visual placement or protocol ordering.
With this fork all three layer surfaces should report `CREATED`. Unmodified
Weston should report `FAILED` while the XDG diagnostics still render.

Koya currently binds layer-shell version 1. This fixture uses `none` and
`exclusive` keyboard modes; it does not exercise version 4 `on_demand` mode,
popups, multiple outputs, or all protocol error paths.
