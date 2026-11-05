# Distance-based note paths

ITGmania's gameplay `Player` actors support distance-based X/Y/Z paths for
notes, receptors, and hold bodies. Each axis is a curve of control points
whose offsets are measured along the incoming scroll distance. The curves
add to the normal ArrowEffects positions.

Existing beat-based `get_pos_handler()` column splines remain available.
Their Position mode replaces ArrowEffects, including these distance-based
paths. Use [ArrowPath guides](ArrowPath.md) to draw the resulting trajectory.

## Defining a path

```lua
local player = SCREENMAN:GetTopScreen():GetChild('PlayerP1')
local options = GAMESTATE:GetPlayerState(PLAYER_1):GetPlayerOptions('ModsLevel_Song')

player:ResetXSplines()
-- Arguments: point index, column, value, offset, approach speed.
-- Point and column indices are zero-based; -1 selects all active columns.
player:SetXSpline(0, -1,    0,    0, -1)
player:SetXSpline(1, -1,  200,  400, -1)
player:SetXSpline(2, -1, -200,  800, -1)
player:SetXSpline(3, -1,    0, 1200, -1)
options:SplineXType(2, -1)
```

This adds a smooth horizontal curve to every active column. `SetYSpline`
and `SetZSpline` take the same arguments for vertical and depth offsets.
The setters return the player for chaining.

Values and offsets use percentages of a 64-pixel arrow: `100` is 64 pixels.
To express a distance in pixels, multiply it by `100 / 64`. The query coordinate
is the incoming scroll distance, so scroll speed affects how quickly notes
travel through a curve. A nonzero value at offset zero also moves the receptor.

Each axis supports up to 40 points per column. Columns must belong to the
active style, with a maximum of 16. Invalid indices and nonfinite values,
offsets, or speeds raise Lua errors.

## Interpolation and timing

`SplineXType`, `SplineYType`, and `SplineZType` are available on PlayerOptions.
Calling one without arguments returns its current value. Passing `(value,
speed)` changes it and returns the PlayerOptions object for chaining; speed
defaults to `1`.

| Type value | Interpolation |
| --- | --- |
| <= 0 | Linear |
| > 0 and <= 1 | Cubic ease-in/out |
| > 1 | Uniform Catmull-Rom |

These are normalized values, so `SplineXType(2, -1)` is also expressible as
`*-1 200% SplineXType` in a modifier string. Fractional values select an
interpolation method; they do not blend methods.

A point speed of `-1`, any other negative speed, or a speed >= 999 applies the
new value on the next options update. Other nonnegative speeds approach the
value in normalized units per second: speed `1` is 64 pixels per second and
speed `0` holds the current value. Offsets update immediately on that update.
RateModsAffectTweens determines whether music rate scales the update speed.

## Resetting and preserving paths

Use ascending offsets. A descending offset, or two consecutive points whose
values and offsets are all zero, ends a curve. Queries before the first point
or after the last point use the respective endpoint value. Catmull-Rom
interpolation clamps its endpoint neighbors to the available points.

Call `ResetXSplines()`, `ResetYSplines()`, or `ResetZSplines()` to clear an axis.
Pass a column to clear only that column. Clear an axis before replacing a path
with fewer points so its old control points do not remain active.

`player:NoClearSplines(true)` preserves curves when current/song options are
initialized. Passing `false` restores normal clearing. Interpolation types
reset to linear on initialization even when curves are preserved.

Point data belongs to ModsLevel_Song and affects current rendering through the
normal options update. It is runtime chart state, not a profile preference or
serialized point modifier.

## Rendering and scope

Hold bodies are sampled more densely when paths are active. Vertical offsets
are applied to the sampled mesh, and the path tangent contributes to strip
orientation. Active depth paths enable the depth buffer.

The methods use NotITG-compatible names for X/Y/Z distance paths. Rotation,
size, skew, and stealth spline channels are not provided by this API, nor
are global or per-column path-query-offset modifiers.

ITGmania's normal draw bounds still apply, so paths looping back from far
outside the draw distance may be clipped. The inverse-Y helper uses the
ordinary sampling coordinate; it does not invert a nonmonotonic vertical path.
