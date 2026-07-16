# Procedural Sky Design

UVSR's first-party sky pass replaces Donut's two-anchor procedural backdrop
with a continuous horizon, middle, and zenith atmosphere. One time-owned
directional light remains the shared contract for the visible celestial body,
direct lighting, and screen-space GI sources.

## Time-Of-Day Cycle

**Time** is a continuous 0–24 hour control that defaults to 12:00. It wraps at
24:00 and drives the atmosphere, ambient fill, celestial orbit, active body,
stars, and directional-light angle from one state. The key visual anchors are
midnight at 00:00, sunrise at 06:00, noon at 12:00, and sunset at 18:00.

Smooth transitions connect deep night to sunrise from 04:00 through 06:00,
sunrise to the accepted day palette through 08:00, day to sunset from 16:00
through 18:00, and sunset to night through 20:00. The exact legacy day palette
and ambient values remain unchanged from 08:00 through 16:00. Night grows
progressively darker from 20:00 to midnight, then reverses toward dawn.

## Three-Band Atmosphere

The upper hemisphere uses three independent color anchors. Elevation is shaped
nonlinearly before two smooth transitions blend horizon to middle and middle to
zenith. The weights remain nonnegative and sum to one, preventing hard band
edges or luminance discontinuities.

The day palette keeps its horizon and below-horizon anchors blue. A low-energy
warm lobe remains localized around the sun instead of adding a broad warm floor
across the lower band. This preserves atmospheric depth without allowing the
bottom of the image to become gray-brown.

Sunrise and sunset have separate palettes. Each holds a cool blue zenith above
a rose or peach middle bridge and a concentrated amber-orange horizon. Sunset
is deeper and redder; sunrise is paler and peachier. Their ambient anchors are
much dimmer and less saturated than the visible backdrop, so warm color reads
in the sky without washing authored materials across the scene.

## Complementary Inspiration

The time design is independently authored for UVSR, using the high-level visual
principles documented by [Complementary Reimagined on DeepWiki](https://deepwiki.com/ComplementaryDevelopment/ComplementaryReimagined)
and visible in its official shader sources: separate upper, middle, and lower
sky anchors; broad atmospheric transitions; localized horizon warmth; a tighter
direct-light horizon fade; and a steep twilight fade for stars. No
Complementary source code, equations, comments, or numeric presets are copied.

## Ambient and Darkness Contract

At noon, ambient top and bottom preserve Donut's pre-experiment defaults of
`(0.17, 0.37, 0.65)` and `(0.62, 0.59, 0.55)`, multiplied by the existing
**Brightness** value. At deep night both ambient anchors are exactly black, so
unlit materials receive an absence of fill light instead of a blue cast.

Deep night uses near-neutral charcoal anchors at every elevation, including the
horizon, accent, and below-horizon region. Only the zenith retains a trace of
cool separation; the backdrop no longer depends on a saturated navy cast.
Earlier evening and later predawn anchors are brighter but follow the same
neutral contract, and smooth interpolation increases darkness as evening
advances. A separate time-depth multiplier reaches `0.33` at midnight, making
the fully rendered background about three times darker again without modifying
moon irradiance. **Brightness** remains the single user control for atmosphere
and star energy and reaches exact black at zero; celestial irradiance remains
independent.

## Celestial Orbit and Lighting

Each body follows one fixed upper semicircle in UVSR's Y-up world. The horizontal
axis matches the default scene framing: the body starts at the left world
horizon, passes through the world zenith, and finishes at the right world
horizon. Every point is a unit direction with nonnegative world Y in one
immutable orbit plane. Camera position, orientation, mode, field of view, and
window shape never participate in the calculation, so looking around changes
only where the body is observed and cannot relocate or flip its path. Scene
geometry naturally occludes a body at the true horizon.

Sun hours are 06:00 through 18:00; moon hours are 18:00 through 06:00. The sun
finishes on the right before the moon restarts on the left, and the moon does
the same before sunrise; neither body continues through a full 360-degree
rotation. At 00:00 the moon is at the visible apex. At both switch horizons the
directional energy smoothly reaches zero before the replacement, hiding the
large positional handoff. Time writes the resulting scene-light direction
before the scene graph refresh, so the disabled azimuth and elevation controls
in the **Lights** drawer reflect the visible orbit without a stale frame.

A frame-local override preserves authored daylight color, irradiance, and
angular size. During sun hours, **Irradiance** edits that daylight value and
time adds localized warm sunrise or sunset tint. During moon hours,
**Irradiance** edits an independent neutral `0.008` default and scales both
scene lighting and visible lunar energy. The override restores the authored
daylight scalars after every frame, preventing time changes from accumulating
editor-state drift.

## Unified Celestial Control

**Enable Celestials** defaults on. Disabling it removes deterministic stars,
the active directional illumination, and its matching disk and halo together.
Direction and authored day and moon irradiance values stay stored and return
unchanged when the control is re-enabled.

The moon is deliberately art-realistic: its one-degree apparent diameter is
close to the sun's scale while retaining enough pixels at the default view for
stable maria, crater rims, a restrained ray feature, highland variation, and
limb shading. Fine detail fades when the projected disk becomes too small to
sample stably. A moon-only `0.45` visible-radiance cap protects the markings
through AgX, while the slightly warm surface tint never reaches scene lighting.

**Glow Size** is the halo's absolute outer angular radius, not a distance added
beyond the disk. Sun and moon halos therefore both terminate at the default
five degrees even though their disk sizes differ.

## Deterministic Stars

Stars use a fixed integer hash over an octahedral world-direction grid. Their
identity depends on direction only, never camera position or frame index.
Sparse selection is modulated by two low-frequency, world-anchored density
fields. Their broad and detail scales produce gathered regions separated by
quieter gaps without changing as the camera moves. A skewed size distribution
keeps most points tiny while a restrained rare upper tail remains distinguishable
and somewhat brighter. Squared per-star brightness, analytic subpixel area coverage,
and neighboring-cell evaluation keep the field stable and avoid clipped stars
at cell boundaries. Energy fades through twilight and near the horizon. The
active halo reserves a matching clearance region; disabling celestials removes
both the stars and the now-unneeded clearance.

## Verification Contract

The procedural-sky reference test checks exact noon day parity and midnight
night anchors, separate warm sunrise and sunset bands, continuous time wrapping,
progressive rendered night depth, a unit-length fixed world orbit that never
crosses below the Y-up horizon, the visible 00:00 zenith, left/right body
handoffs, photon-direction sign, zero-energy switches, three-band weight
normalization, neutral independent moon irradiance, equal halo extents,
deterministic clustered star density, a skewed size range, and bounded subpixel
star energy. Runtime verification must still cover forward and deferred
rendering because shader compilation and GPU presentation are outside the CPU
reference test's scope.
