# GLSL actor shaders

ITGmania can apply GLSL vertex and fragment shaders to actors using its OpenGL
renderer. Shaders can be shared between actors, inherited by child actors,
or assigned to individual gameplay layers. Uniforms can be set from Lua;
ITGmania also supplies texture dimensions, song time, beat, and matrices.

## Loading shader files

Actor definitions accept `Frag` for a fragment shader, `Vert` for a vertex
shader, and `Shader` as an alias for `Frag`. If both `Frag` and `Shader` are
present, `Frag` takes precedence. Paths in these attributes resolve relative
to the actor definition. Omitted or empty stages use ITGmania's default stage.
The shader resource is available before `InitCommand` runs.

```lua
return Def.Sprite {
  Texture = "background.png",
  Frag = "effects/tint.frag",
  InitCommand = function(self)
    self:GetShader():uniform3f("tint", 1, 0.5, 0.5)
  end
}
```

For example, `effects/tint.frag` could contain:

```glsl
#version 120
uniform sampler2D sampler0;
uniform vec3 tint;
varying vec2 textureCoord;
varying vec4 color;

void main() {
  vec4 pixel = texture2D(sampler0, textureCoord) * color;
  gl_FragColor = vec4(pixel.rgb * tint, pixel.a);
}
```

XML actor definitions accept the same attributes, such as
`<Layer Type="Sprite" Frag="effects/tint.frag" .../>`.

Shaders can also be loaded or changed from Lua:

```lua
actor:GetShader()                    -- returns the shader, or nil
actor:LoadShader("effect.frag")
actor:LoadShader("effect.vert", "effect.frag")
actor:SetShader(other:GetShader())   -- shares the program and its uniforms
actor:ClearShader()
```

`LoadShader` takes ITGmania virtual-filesystem paths; these do not resolve
relative to the actor definition. Use a path returned by the theme or song
path helpers, or an absolute virtual path such as `/Themes/MyTheme/effect.frag`.
Methods other than `GetShader` return the actor. `SetShader(nil)` also clears
its shader.

## Sharing and inheritance

Actor copies share the same shader resource. Lua references keep that resource
alive after the original actor is cleared or destroyed. Changes to uniforms
are visible to every actor using that resource.

Children inherit their parent's shader unless they have their own.
`ActorFrame:SetShader(shader)` forces that shader through the child hierarchy,
overriding individual child programs. A shader loaded through actor attributes
or `LoadShader` uses ordinary inheritance. Drawing restores the previous
shader when an actor's draw scope ends.

## Gameplay layers

The gameplay `Player` actor provides separate shader selections:

```lua
player:SetArrowShader(shader)       -- taps and hold heads/tails
player:SetHoldShader(shader)        -- hold-body meshes
player:SetReceptorShader(shader)
player:SetArrowPathShader(shader)   -- ArrowPath guide lines
```

These methods return the player. Pass `nil` to clear a selection. PlayerState
reset clears all four selections. A forced parent-frame shader takes
precedence over a gameplay-layer shader.

## Shader methods

A shader returned by `GetShader()` supports:

| Method | Behavior |
| --- | --- |
| `compile(vertexSource, fragmentSource)` | Replaces the source strings and queues compilation; empty strings select default stages |
| `compile()` | Queues recompilation of the current sources and defines |
| `compileImmediate()` | Compiles now and reports errors through the Lua error reporting system |
| `define(name, value)` | Sets a preprocessor define; value defaults to `"1"` |
| `clearDefine(name)` | Removes a preprocessor define |
| `isValid()` | Returns whether a usable GPU program exists |
| `getError()` | Returns the current diagnostic string |

`compile` takes source strings, not filenames. Defines are inserted after an
authored `#version` directive, or at the start if there is none. Define names
must be identifiers, and values cannot contain line breaks.

`cleardefine` and `compileimmediate` are aliases for `clearDefine` and
`compileImmediate`. Defines also accept Lua booleans, which become GLSL
`true` or `false`.

`loadFiles(vertexPath, fragmentPath)` loads virtual-filesystem paths; the
fragment path defaults to an empty string. The `compile` methods continue to
take source strings. `compilebg(vertexSource, fragmentSource)` queues a
background program, and `compileimmediatebg()` immediately compiles the current
program in background mode. Background mode skips automatic song `time` and
`beat` uploads while retaining resolution and explicit uniforms.
`setBackground(false)` restores automatic song clocks. These operations do
not create worker threads. The global `Shader` table aliases
`RageShaderProgram` for capability checks.

Uniform setters take a uniform name followed by these arguments:

| Methods | Arguments after the name |
| --- | --- |
| `uniform1f`, `uniform2f`, `uniform3f`, `uniform4f` | One to four float components |
| `uniform1i`, `uniform2i`, `uniform3i`, `uniform4i` | One to four integer components |
| `uniform1fv`, `uniform2fv`, `uniform3fv`, `uniform4fv` | A flat Lua table whose length is a multiple of the vector width |
| `uniform1iv`, `uniform2iv`, `uniform3iv`, `uniform4iv` | A flat Lua table of integers whose length is a multiple of the vector width |
| `uniformMatrix2fv`, `uniformMatrix3fv`, `uniformMatrix4fv` | A flat column-major table in multiples of 4, 9, or 16 values |
| `uniformTexture` | A texture such as `sprite:GetTexture()`, or `nil` to remove the binding |

Mutators return the shader for chaining. Numeric arguments must be finite,
and integer inputs must be integral and fit in a C++ `int`. Uniform arrays
must be nonempty and contain no more than 1,048,576 values. Texture bindings
retain their textures while assigned. Custom samplers start at texture unit 2;
units 0 and 1 are reserved for the renderer, and the driver limits the total.

Changing sources or defines queues a rebuild. Updating uniforms does not.
Compilation is normally deferred until drawing. A failed rebuild retains the
previous working GPU program, if one exists; check `getError()` for the failure
message even if `isValid()` remains true. Uniforms optimized out by the compiler
are ignored.

## Default stages and automatic uniforms

The default vertex stage supplies `position`, `normal`, `color`, `textureCoord`,
and `imageCoord` varyings, along with `gl_TexCoord[0]` and `gl_FrontColor`.
The default fragment stage multiplies the primary texture sample by the vertex
color. Compiled models also support the `TextureMatrixScale` attribute.

ITGmania supplies these uniforms when applying the program:

| Uniform | Value |
| --- | --- |
| `sampler0`, `sampler1` | Texture units 0 and 1 |
| `textureSize` | Allocated primary texture width and height |
| `imageSize` | Primary image width and height |
| `time`, `beat` | Global song time in seconds and song beat |
| `resolution` | Logical theme screen width and height |
| `modelMatrix`, `viewMatrix`, `projectionMatrix`, `textureMatrix` | Current renderer matrices |
| `perspectiveMatrix` | Alias for the projection matrix |

Texture dimensions use 1x1 when there is no primary texture. Explicit uniform
setters can override automatic values. `resolution` describes the theme's
coordinate system, not the physical framebuffer size.

When the OpenGL context is recreated, GPU handles and uniform-location caches
are discarded. Sources, defines, uniforms, and texture references remain
available for lazy recompilation. Shutdown releases GPU resources and sampler
textures before the texture and display managers are destroyed, including
resources still referenced from Lua.

## Renderer support and compatibility

Use `DISPLAY:SupportsShaders()` to check availability. GLSL programs require
ITGmania's OpenGL renderer and a driver supporting the necessary shader
extensions. The Direct3D and Null renderers do not execute these programs.
Unsupported renderers, compilation failures, and link failures report
diagnostics.

The default stages use OpenGL's compatibility interface. Authored `#version`
directives are preserved; supported GLSL versions depend on the active driver
and context. ITGmania does not downgrade shader versions or translate source
syntax. A source without `#version` uses the compiler's default version.

The actor methods, gameplay-layer selections, and shader inputs include
NotITG-compatible names. Existing shaders still need to fit the available
GLSL version and inputs; this feature does not supply other NotITG chart,
actor, XML, or template APIs.

## Automatic draw context

Gameplay draws supply zero-based `iCol` and `iPlayfield`, plus `isReceptor`,
`isHold` (hold bodies and caps), and `isMine`. Tap, head, and tail draws supply
`fYOffset`; applicable note draws also supply `fNoteBeat` and `fNoteBeatAdj`.
In ITGmania, the adjusted beat currently equals the actual note beat.
Hold bodies and ArrowPath guides supply `fColOffset`, the style column's X
translation already included in their vertices. Modifiers and splines can
further displace those vertices.

Models supply `materialId` per mesh. `isEnvMap` reflects sphere environment
mapping on the primary texture stage. Non-note draws default to column and
playfield -1, false flags, and zero note values; non-model draws default to
material -1. These values belong to each draw and are restored when a nested
draw ends, including when several actors share a shader. Explicit uniforms
override the automatic values. There is no per-vertex `fYOffset` attribute.

## NoteField shader controls

Actors provide `AddShader(shader)` as an alias of `SetShader` and
`StopShader()` as an alias of `ClearShader`. These select one shader rather
than creating a sequence of passes.

The NoteField actor exposes Get/Set/Add/Stop/Clear methods for `NoteShader`,
`HoldShader`, `ReceptorShader`, and `NotePathShader`. For example:

```lua
field:SetNoteShader(shader)
field:GetNoteShader()
field:AddHoldShader(shader)
field:StopNotePathShader()
field:ClearReceptorShader()
```

These use the same player-state layer selections as Player's shader methods:
NoteShader corresponds to ArrowShader, and NotePathShader to ArrowPathShader.
Stop/Clear only detach the selection; another actor can keep using the same
shader resource. Get returns nil before the field has player state. Set/Add
require initialized player state. Mutators return the NoteField.

## Disabling custom shaders

`CustomShadersDisabled` defaults to false. Set it to true through PREFSMAN or
set `CustomShadersDisabled=1` in the `[Options]` section of `Preferences.ini`
to disable custom shader loading, compilation, updates, and drawing globally.
Attempts log warnings instead of showing shader error dialogs. Repeated
warnings for the same operation are suppressed while disabled. Shader objects
remain available to chart Lua, and `isValid()` returns false. Hardware shader
capability and this preference are separate checks. Built-in rendering effects
are unaffected. Reload the screen or song after re-enabling custom shaders to
load any skipped sources.
