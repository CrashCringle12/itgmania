# ArrowPath guides

ITGmania can draw a colored guide along each note column's approach path.
Guides follow the same positions as notes, including distance-based
[X/Y/Z note paths](NotePath.md), existing column splines, and ArrowEffects.
They are hidden by default; enable them with the `arrowpath` modifier or the
`PlayerOptions:ArrowPath()` method.

## Setting up a guide

Load this code from a song's foreground Lua actor during gameplay:

```lua
local player = SCREENMAN:GetTopScreen():GetChild('PlayerP1')
local options = GAMESTATE:GetPlayerState(PLAYER_1):GetPlayerOptions('ModsLevel_Song')

player:SetArrowPathBlendMode('add')
player:SetNumPathGradientPoints(-1, 3)
player:SetPathGradientPoint(0, -1, 0)
player:SetPathGradientPoint(1, -1, 3)
player:SetPathGradientPoint(2, -1, 6)
player:SetPathGradientColor(0, -1, 1, 0, 0, 1)
player:SetPathGradientColor(1, -1, 1, 1, 0, 0.7)
player:SetPathGradientColor(2, -1, 0, 1, 0, 0)

options:ArrowPath(1, -1)
options:ArrowPathWidth(3, -1)
options:ArrowPathDrawSizeBack(-1, -1)
GAMESTATE:ForceSmoothLines(0)
```

This draws a four-pixel guide that fades from red through yellow to transparent
green. It starts at the receptors and fades out 384 pixels along the incoming
scroll distance. Values passed to PlayerOptions are normalized: `1` corresponds
to `100%` in a modifier string. The speed argument `-1` applies the change on
the next options update.

See [ArrowPathExample.lua](ArrowPathExample.lua) for an example covering all
enabled players and the active style's columns.

## Player methods

```lua
player:SetArrowPathBlendMode(name)
player:SetNumPathGradientPoints(column, count)
player:SetPathGradientColor(point, column, r, g, b, a)
player:SetPathGradientPoint(point, column, position)
```

All four methods return the player for chaining. Columns and points are
zero-based. Column `-1` selects all active columns. Set the count before
setting positions and colors. Changing the count resets every stop to position
zero and white; setting the same count preserves the existing stops. Count
zero removes the gradient and uses solid white. Counts are limited to 65,536.
Invalid indices and nonfinite numeric arguments raise Lua errors.

Positions are measured in units of 64 pixels: a stop at `2` is at distance
128. Stops are evaluated in the order they were set, without sorting. A
decreasing position, or two consecutive zero positions, ends the gradient
at the preceding color. The final increasing segment extrapolates beyond its
last stop. Colors and opacity are clamped when converted to render vertices.
Equal nonzero positions use the outgoing segment without dividing by zero.

Supported blend names are `normal`, `add`, `subtract`, `modulate`, `copysrc`,
`alphamask`, `alphaknockout`, `alphamultiply`, `weightedmultiply`, `invertdest`,
and `noeffect`. Other names raise a Lua error. Gradient and blend settings
are cleared when PlayerState is reset.

## Modifiers and PlayerOptions

| Modifier | Behavior |
| --- | --- |
| `arrowpath` | Global opacity; defaults to 0 |
| `arrowpath0` ... `arrowpath15` | Opacity added to the global value for that column |
| `arrowpathwidth` / `arrowpathgirth` | Global width adjustment |
| `arrowpathwidthN` / `arrowpathgirthN` | Width adjustment for column N, zero-based |
| `arrowpathgrain` | Sampling step in 1/48 beats; values <= 0.05 use 4 |
| `arrowpathdrawsize` | Scales the incoming draw distance by 1 + value |
| `arrowpathdrawsizeback` | Scales the draw distance behind the receptors by 1 + value |

Modifier strings use the usual percentages. Width in local pixels is
`(1 + globalWidth) * (1 + columnWidth)`, so `1000% arrowpathgirth` gives
11 pixels at the default column width. `-100% arrowpathdrawsizeback` starts
the guide at the receptors. Opacity combines the gradient alpha, global and
column opacity, and the column actor's diffuse alpha. Hidden and sudden do
not hide the guide.

PlayerOptions provides these methods:

- `ArrowPath`, `ArrowPathWidth`, `ArrowPathGrain`, `ArrowPathDrawSize`, and
  `ArrowPathDrawSizeBack` accept `(value, speed, chain)`.
- `ArrowPathCol` and `ArrowPathWidthCol` accept `(column, value, speed, chain)`.

Omitting a value or speed leaves it unchanged. These methods return the
previous value and speed; a final `true` returns the PlayerOptions object
instead, for chaining. Negative speeds and speeds >= 999 apply immediately
on the next options update.

`GAMESTATE:ForceSmoothLines(0)` makes the OpenGL renderer use polygon strips
for lines, avoiding hardware smooth-line width limits. `1` enables smooth
lines; `-1` follows the video preference. The override remains until changed
or the display is recreated.

## Limits and compatibility

The guide API uses NotITG-compatible method and modifier names for the features
described here. The `alpha` blend mode, path-query offsets, fade-top/bottom
controls, and additional path draw modes are not supported.

Guides use ITGmania's displayed-beat searches and draw bounds. Paths that loop
back from far outside those bounds can be clipped. Geometry is limited to
16,384 samples per column; very small grain values increase the sampling step
when needed to stay within that limit. This feature does not add shader support.
